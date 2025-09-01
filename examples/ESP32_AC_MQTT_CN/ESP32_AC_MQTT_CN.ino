/*
 * ESP32 空调控制器 MQTT版本
 * 
 * 本程序使用ESP32微控制器控制空调，具有以下功能：
 * - BC7215A 红外收发模块发送空调控制信号
 * - TFT显示屏用户界面 (TTGO T-Display)
 * - WiFi连接MQTT通信，通过MQTT控制空调
 * - 物理按钮手动控制，并将最新空调状态上传至MQTT
 * 
 * 功能特点：
 * - 离线空调码库, 任意品牌/型号
 * - 一键学习(配对)
 * - 控制温度、模式和风速
 * - MQTT连接可用于物联网远程控制
 * - 中文界面和状态指示器
 */

#include <Preferences.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include "Dengl16.h"
#include <WiFi.h>
#include <bc7215.h>
#include <bc7215ac.h>

// WiFi和设备配置 - 请替换为您自己的值
// #define MY_WIFI_SSID     "你的WiFi名称"    // 替换为您的WiFi名称
// #define MY_WIFI_PASSWORD "你的WiFi密码"    // 替换为您的WiFi密码
// #define MY_UUID          "你的UUID"        // 使用UUID生成器创建唯一设备ID

// 编译检查必要配置
#if !defined(MY_WIFI_SSID) || !defined(MY_WIFI_PASSWORD) || !defined(MY_UUID)
#    error "请取消注释第28-30行并替换为您自己的SSID/密码和UUID"
#endif

// ======= 硬件引脚配置 (TTGO T-Display) =======
#define BUTTON_LEFT    0         // 左按钮 (-)
#define BUTTON_RIGHT   35        // 右按钮 (+)
#define BC7215_MOD     27        // BC7215模块控制引脚
#define BC7215_BUSY    26        // BC7215忙状态引脚
#define BC7215_PORT_RX 25        // ESP32 RX，连接到BC7215 TX
#define BC7215_PORT_TX 33        // ESP32 TX，连接到BC7215 RX

// ========= WiFi & MQTT 配置 =============
const char*    WIFI_SSID = MY_WIFI_SSID;
const char*    WIFI_PASSWORD = MY_WIFI_PASSWORD;
const char*    MQTT_HOST = "broker.emqx.io";        // 公共MQTT代理（可使用其他）
const uint16_t MQTT_PORT = 1883;
const char*    MQTT_CLIENT_ID = MY_UUID;
const char*    MQTT_LWT = "BC7215A/" MY_UUID "/status";           // 遗嘱主题
const char*    TEMP_TOPIC = "BC7215A/" MY_UUID "/var/temp";       // 温度控制主题
const char*    MODE_TOPIC = "BC7215A/" MY_UUID "/var/mode";       // 空调模式控制主题
const char*    FAN_TOPIC = "BC7215A/" MY_UUID "/var/fan";         // 风速控制主题
const char*    POWER_TOPIC = "BC7215A/" MY_UUID "/var/power";     // 电源开关主题
const char*    REPORT_TOPIC = "BC7215A/" MY_UUID "/var/report";   // 状态报告主题

// ======= 显示常量 (竖屏 135x240) =======
#define SCREEN_W 135
#define SCREEN_H 240

// ======= 主题颜色 =======
static const uint16_t COLOR_BG = TFT_WHITE;                // 背景色
static const uint16_t COLOR_TEXT = TFT_BLACK;              // 文字色
static const uint16_t COLOR_ONLINE_BADGE = TFT_DARKCYAN;   // 在线状态标识背景色
static const uint16_t COLOR_LABEL_BG = TFT_DARKCYAN;   	   // 在线状态标识背景色
static const uint16_t COLOR_BADGE_FG = TFT_WHITE;          // 标识文字色
static const uint16_t COLOR_ACTION_BG = TFT_LIGHTGREY;     // 底部按钮背景色
static const uint16_t COLOR_ACTION_FG = TFT_BLACK;         // 底部按钮文字色
static const uint16_t COLOR_ACTIVE_BADGE = TFT_RED;        // 活动/发送时的标识色

// ======= 顶部标识布局（等分布，统一大小）=======
#define BADGE_FONT        1              // 标识字体大小
#define BADGE_SIZE        1              // 标识文字缩放
#define BADGE_RADIUS      2              // 标识圆角半径
#define BADGE_Y           6              // 标识Y位置
#define BADGE_HEIGHT      12             // 标识高度（文字+上下约2px）
#define BADGE_SIDE_MARGIN 3              // 左右边距
#define BADGE_GAP         6              // 标识间隙
#define BADGE_WIDTH       ((SCREEN_W - 2 * BADGE_SIDE_MARGIN - 2 * BADGE_GAP) / 3)

// ======= 布局参数（仅基本定位）=======
struct Layout
{
    // 顶部三个标识X位置
    int badgeX0, badgeX1, badgeX2;

    // 中央大数字显示
    int numberY = 50;
    int numberAreaY = 40;
    int numberAreaH = 60;

    // 中间左右标签
    int middleLabelY = 145;
    int middleLabelPad = 8;
} L;

// ====== 显示内容数组 =======
const String MODES[] = { " 自动 ", " 制冷 ", " 制热 ", " 除湿 ", " 送风 " };        // 空调运行模式
const String FANSPEED[] = { " 自动 ", " 低速 ", " 中速 ", " 高速 " };               // 风速级别
const String EXTRA_KEY[] = { "温度+/-", "模式", "风速" };                           // 需额外信号捕获的遥控器按键
const char*  MAIN_MENU[] = { "采集并初始化", "查找下一匹配", "加载预定义", "退出" };

// ======= 状态机枚举 =======

// 主应用状态 (一级)
enum L1_STATE
{
    START,          // 初始启动状态
    INIT,           // 空调库初始化
    TEMP_CTL,       // 温度控制模式（主要操作）
    ON_OFF_CTL,     // 电源开关控制
    MENU_MAIN,      // 主菜单显示
    MENU_PREDEF,    // 预定义空调遥控协议菜单
    FIND_NEXT,      // 查找下一个匹配的空调协议
    IR_SENDING,     // 正在发送红外信号
    NOT_CONNECTED   // BC7215模块未连接
};

// 复杂操作的子状态 (二级)
enum L2_STATE
{
    STEP1, STEP2, STEP3, STEP4, STEP5, STEP6, STEP7
};

// 按钮按下检测状态
enum KEYBOARD_STATE
{
    BOTH_RELEASED,      // 无按钮按下
    ONE_PRESSED,        // 单个按钮按下
    BOTH_PRESSED,       // 两个按钮都按下
    ONE_RELEASED,       // 一个按钮释放，另一个仍按下
    ONE_LONG_PRESSED,   // 单个按钮长按
    BOTH_LONG_PRESSED   // 两个按钮长按
};

// 从状态机生成的按钮事件
enum KEY_EVENT
{
    NO_KEY,             // 无按钮事件
    LEFT_KEY_SHORT,     // 左按钮短按
    RIGHT_KEY_SHORT,    // 右按钮短按
    LEFT_KEY_LONG,      // 左按钮长按
    RIGHT_KEY_LONG,     // 右按钮长按
    BOTH_KEY_SHORT,     // 两按钮短按
    BOTH_KEY_LONG       // 两按钮长按
};

// 网络连接状态
enum NET_STATUS
{
    WAITING,       // 等待连接
    CONNECTING,     // 连接中
    CONNECTED       // 已连接
};

// ======= 全局对象和变量 =======
Preferences    savedData;                           // 空调库初始化信息的非易失性存储
TFT_eSPI       tft = TFT_eSPI();                   // TFT显示对象
WiFiClient     esp32WiFi;                          // WiFi客户端
PubSubClient   mqtt(esp32WiFi);                    // MQTT客户端
HardwareSerial BC7215_SERIAL(1);                  // BC7215通信串口使用串口1
BC7215         bc7215Board(BC7215_SERIAL, BC7215_MOD, BC7215_BUSY);  // BC7215控制对象
BC7215AC       ac(bc7215Board);                    // 空调控制库对象

// 状态变量
L1_STATE                  mainState;               // 当前主状态
L1_STATE                  retState;                // 红外发送后的返回状态
L2_STATE                  l2State;                 // 当前子状态
NET_STATUS                wifiState;               // WiFi连接状态
NET_STATUS                mqttState;               // MQTT连接状态
KEYBOARD_STATE            keyboardState;           // 当前按钮状态
KEY_EVENT                 keyEvent;                // 最后按钮事件

// 控制变量
int                       interval;                // 主循环延迟间隔
int                       temp = 25;               // 当前温度设置 (25°C)
int                       mode = 1;                // 当前空调模式 (1 = 制冷)
int                       fan = 1;                 // 当前风速 (1 = 低速)
int                       remoteBtn = 0;           // 最后按下的遥控器按钮 (0 = 温度-)
const char**              menuItems;               // 当前菜单项数组
const char**              preDefs;                 // 预定义空调型号数组
int                       currentMenuSelection;    // 当前选中的菜单项
bool                      usingCelsius = true;    // 温度单位标志
unsigned int              timerMs;                // 通用定时器
int                       msgCnt;                 // 消息计数器
int                       idleCntr;               // 空闲计数器
int                       extra;                  // 需要的额外红外信号
int                       savedTemp;              // 临时温度存储
int8_t                    matchCnt;               // 找到的空调协议匹配数
bool                      mqttCmd = false;        // 标识命令来自MQTT
bool					  irSending = false;	  // 标识红外正在发射
bool					  initOK = false;		  // 标识已成功初始化

// BC7215数据结构
const bc7215DataVarPkt_t* dataPkt;                // 红外数据包指针
const bc7215FormatPkt_t*  formatPkt;              // 红外格式包指针
bc7215DataMaxPkt_t        sampleData[4];          // 捕获的红外数据存储
bc7215FormatPkt_t         sampleFormat[4];        // 捕获的红外格式存储

// ================== 函数声明 ==================
void powerup();
void initAC();
void tempCtrl();
void onOffCtrl();
void mainMenu();
void predefMenu();
void findNext();
void acDispUpdate();
void acStatUpdate();
void saveInitInfo();
void flashNoConn();
void wifiConnect();
void mqttConnect();
void mqttOnlineAction();
void processMqtt(char* topic, byte* payload, unsigned int length);
void updateKeyStatus();

// 显示函数
void computeTopBadgesLayout();
void drawBadge(int xStart, uint16_t bg, const char* text);
void clearNumberArea();
void drawBigNumber(const String& value);
void drawModeLabel(int mode);
void drawFanLabel(int fan);
void clearCentralArea();
void drawTempButtons();
void drawMenuButtons();
void drawOKButton();
void drawEscButton();
void drawSELButton();
void drawOnOffButtons();
void clearLeftBtn();
void clearRightBtn();
void showMenu(const char* items[], int cnt);
void updateMenu(int selected);
void drawIrBadge(uint16_t color);
void drawWiFiBadge(uint16_t color);
void drawMqttBadge(uint16_t color);
void showInitScrn1();
void showInitScrn2(const String& keyText);
void showInitScrn3(const String& keyText);
void showInitScrn4();
void showInitScrn5();
void showInitScrn6();
void showInitOKMsg();
void showInitFailMsg();
void showNextMatchScrn(int8_t matchCnt);
void showNextFailScrn();
void showNoInitScrn();

// ================== Arduino标准函数 ==================

void setup()
{
    // 初始化串口通信用于调试
    Serial.begin(115200);
    
    // 初始化TFT显示
    tft.init();
    tft.setRotation(0);                // 竖屏方向 (135x240)
    tft.setTextDatum(TC_DATUM);        // 顶部居中文字对齐
	tft.loadFont(Dengl16);
    
    // 配置按钮引脚
    pinMode(BUTTON_LEFT, INPUT_PULLUP);
    pinMode(BUTTON_RIGHT, INPUT);

    // 初始化BC7215串口通信
    BC7215_SERIAL.begin(19200, SERIAL_8N2, BC7215_PORT_RX, BC7215_PORT_TX);
    Serial.println("BC7215 串口已初始化");
    
    // 初始化预定义空调协议数组
    preDefs = new const char*[ac.cntPredef()];
    for (int i = 0; i < ac.cntPredef(); i++)
    {
        preDefs[i] = ac.getPredefName(i);
    }
    
    // 初始化状态变量
    mainState = START;
    l2State = STEP1;
    keyboardState = BOTH_RELEASED;
    keyEvent = NO_KEY;
    interval = 10;
}

void loop()
{
    // 主状态机
    switch (mainState)
    {
    case START:
        powerup();              // 处理启动序列
        break;
    case INIT:
        initAC();              // 处理空调库初始化
        break;
    case TEMP_CTL:
        tempCtrl();            // 处理温度控制界面
        break;
    case ON_OFF_CTL:
        onOffCtrl();           // 处理电源开关界面
        break;
    case MENU_MAIN:
        mainMenu();            // 处理主菜单导航
        break;
    case MENU_PREDEF:
        predefMenu();          // 处理预定义空调型号菜单
        break;
    case FIND_NEXT:
        findNext();            // 处理查找下一个空调协议匹配
        break;
    case IR_SENDING:           // 等待红外传输完成
        if (!ac.isBusy())
        {
			irSending = false;
            drawIrBadge(COLOR_BG);        // 清除红外活动指示器
            if (mqttCmd)
            {
				mqttCmd = false;
                drawMqttBadge(COLOR_ONLINE_BADGE);        // 恢复MQTT标识为正常颜色
            }
            mainState = retState;         // 返回到之前的状态
        }
		else
		{
			timerMs += interval;
			if (timerMs > 3000)				// 发送超时
			{
				irSending = false;
	            drawIrBadge(COLOR_BG);        // 清除红外活动指示器
	            if (mqttCmd)
	            {
					mqttCmd = false;
	                drawMqttBadge(COLOR_ONLINE_BADGE);        // 恢复MQTT标识为正常颜色
	            }
	            mainState = retState;         // 返回到之前的状态
			}
		}
        break;
    case NOT_CONNECTED:
        flashNoConn();         // 显示未找到BC7215A错误
        break;
    default:
        break;
    }
    
    // 处理网络连接和输入
    wifiConnect();             // 管理WiFi连接
    mqttConnect();             // 管理MQTT连接
    updateKeyStatus();         // 处理按钮输入
    delay(interval);
}

/*
 * 启动序列处理器
 * 初始化硬件并加载保存的空调配置
 */
void powerup()
{
    switch (l2State)
    {
    case STEP1:
        // 清屏并初始化BC7215
        tft.fillScreen(COLOR_BG);
        bc7215Board.setTx();           // 设置BC7215为发送模式
        delay(50);
        bc7215Board.setShutDown();     // 让BC7215进入关机模式
        timerMs = 0;
	    // 初始化WiFi连接
	    WiFi.mode(WIFI_STA);
	    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
	    
	    // 初始化MQTT客户端
	    mqtt.setServer(MQTT_HOST, MQTT_PORT);
	    mqtt.setCallback(processMqtt);
	    wifiState = CONNECTING;
	    mqttState = WAITING;

        l2State = STEP2;
        break;
        
    case STEP2:
        // 等待BC7215准备就绪
        if (!bc7215Board.isBusy())
        {
            bc7215Board.setRx();       // 唤醒BC7215并设置为接收模式
            delay(50);
            l2State = STEP3;
        }
        else
        {
            // 超时处理
            timerMs += interval;
            if (timerMs > 1000)
            {
                mainState = NOT_CONNECTED;
                timerMs = 0;
            }
        }
        break;
        
    case STEP3:
        // 从闪存加载保存的空调配置
        savedData.begin("bc7215 init", true);
        savedData.getBytes("format", &sampleFormat[0], sizeof(bc7215FormatPkt_t));
        savedData.getBytes("data", &sampleData[0], sizeof(bc7215DataMaxPkt_t));
        savedData.getBytes("matchIndex", &matchCnt, sizeof(matchCnt));
        
        // 尝试用保存的数据初始化空调
        if (ac.init(sampleData[0], sampleFormat[0]))
        {
			initOK = true;
            // 如果有多个匹配，导航到正确的那个
            if (matchCnt > 0)
            {
                bool result;
                for (int i = 0; i < matchCnt; i++)
                {
                    result = ac.matchNext();
                }
                if (!result)        // 如果找不到那个匹配，使用原始的
                {
                    ac.init(sampleData[0], sampleFormat[0]);
                    matchCnt = 0;
                }
            }
            
            // 如果初始化成功，但需加载额外的格式信息
            if (ac.extraSample())
            {
                savedData.getBytes("extraFormat", &sampleFormat[1], sizeof(bc7215FormatPkt_t));
                savedData.getBytes("extraData", &sampleData[1], sizeof(bc7215DataMaxPkt_t));
                ac.saveExtra((bc7215DataVarPkt_t*)&sampleData[1], &sampleFormat[1]);
            }
            
            // 初始化默认空调设置
            temp = 25;
            mode = 1;
            fan = 1;
            mainState = TEMP_CTL;
            
            // 显示初始界面
            drawBigNumber(String(temp));
            drawModeLabel(mode);
            drawFanLabel(fan);
            drawTempButtons();
        }
        else
        {
            // 初始化不成功，显示主菜单
			initOK = false;
            mainState = MENU_MAIN;
            showMenu(MAIN_MENU, sizeof(MAIN_MENU) / sizeof(char*));
            currentMenuSelection = 0;
            updateMenu(currentMenuSelection);
            drawMenuButtons();
        }
        savedData.end();
        break;
    default:
        break;
    }
}

/*
 * 空调初始化和红外信号学习过程
 * 引导用户从空调遥控器捕获红外信号
 */
void initAC()
{
    switch (l2State)
    {
    case STEP1:
        // 显示初始指导屏幕
        drawOKButton();
        drawEscButton();
        showInitScrn1();
        l2State = STEP2;
        break;
        
    case STEP2:
        // 等待用户开始捕获或取消
        if (keyEvent == RIGHT_KEY_SHORT)
        {
            clearRightBtn();
            showInitScrn2("<风速>");  // 显示捕获指导
            ac.startCapture();              // 开始红外信号捕获
            l2State = STEP3;
        }
        if (keyEvent == LEFT_KEY_SHORT)
        {
            mainState = START;              // 取消并返回开始
            l2State = STEP1;
        }
        keyEvent = NO_KEY;
        break;
        
    case STEP3:
        // 等待红外信号捕获并处理结果
        if (ac.signalCaptured())
        {
            ac.stopCapture();
            if (ac.init())                  // 尝试用捕获的信号初始化
            {
				initOK = true;
                matchCnt = 0;
                extra = ac.extraSample();   // 检查是否需要额外采样信号
                if (extra > 0)              // 需要额外捕获
                {
                    if (extra > 3) extra = 3;  // 防止任何原因extra值超过3
                    showInitScrn3(EXTRA_KEY[extra - 1]);  // 显示下一步要按什么按钮
                    drawOKButton();
                    l2State = STEP7;
                }
                else                        // 初始化成功且无需额外采样
                {
                    saveInitInfo();
                    showInitOKMsg();
					retState = TEMP_CTL;
                    l2State = STEP6;
                }
            }
            else
            {
				initOK = false;
                showInitFailMsg();          // 初始化失败
                l2State = STEP5;
            }
        }
        if (keyEvent == LEFT_KEY_SHORT)
        {
            mainState = START;              // 取消并返回开始
            l2State = STEP1;
        }
        keyEvent = NO_KEY;
        break;
        
    case STEP4:
        // 为完整的空调控制捕获额外的红外信号
        if (ac.signalCaptured((bc7215DataVarPkt_t*)&sampleData[msgCnt], &sampleFormat[msgCnt]))
        {
            if (msgCnt < 3) msgCnt++;
            idleCntr = 0;
            if (extra < 3)					// 如果extra<3，仅需采集1次指定按键
            {
                ac.stopCapture();
                ac.saveExtra(sampleData[0], sampleFormat[0]);
                saveInitInfo();
                showInitOKMsg();
				retState = TEMP_CTL;
                l2State = STEP6;
            }
        }
        else
        {
            // 如果没有检测到更多信号，处理超时
            if ((msgCnt != 0) && (!ac.isBusy()))		// 开始采样后，计时信号的空闲时间
            {
                idleCntr += interval;
                if (idleCntr >= 200)        // 空闲超过200ms，认为采样结束
                {
                    ac.stopCapture();
                    if (ac.init(msgCnt, sampleData, sampleFormat))	// 使用采样到的所有信号初始化
                    {
						initOK = true;
                        matchCnt = 0;
                        saveInitInfo();
                        showInitOKMsg();
                    }
                    else
                    {
						initOK = false;
                        showInitFailMsg();
                    }
                    retState = START;
                    l2State = STEP6;
                }
            }
        }
        if (keyEvent == LEFT_KEY_SHORT)
        {
            mainState = START;
            l2State = STEP1;
        }
        keyEvent = NO_KEY;
        break;
        
    case STEP5:
        // 处理初始化失败
        if (keyEvent == RIGHT_KEY_SHORT)
        {
            showInitScrn1();               // 重试初始化
            l2State = STEP2;
        }
        else if (keyEvent == LEFT_KEY_SHORT)
        {
            mainState = START;             // 返回开始
            l2State = STEP1;
        }
        keyEvent = NO_KEY;
        break;
        
    case STEP6:
        // 等待确定按钮继续
        if (keyEvent == RIGHT_KEY_SHORT)
        {
			acDispUpdate();
			drawTempButtons();
            l2State = STEP1;
            mainState = retState;
        }
        keyEvent = NO_KEY;
        break;
        
    case STEP7:
        // 等待确定按钮然后开始额外捕获
        if (keyEvent == RIGHT_KEY_SHORT)
        {
            showInitScrn2(EXTRA_KEY[extra - 1]);
            ac.startCapture();
            l2State = STEP4;
        }
        if (keyEvent == LEFT_KEY_SHORT)
        {
            mainState = START;
            l2State = STEP1;
        }
        keyEvent = NO_KEY;
        break;
    default:
        break;
    }
}

/*
 * 主温度控制界面
 * 处理温度调节、模式更改和风速控制
 */
void tempCtrl()
{
    switch (keyEvent)
    {
    case LEFT_KEY_SHORT:
        // 降低温度（仅适用于自动、制冷、制热模式）
        if (mode <= 2)
        {
            if (temp > 16)
            {
                temp--;
            }
            remoteBtn = 0;                 // 温度-按钮
			irSending = true;
            acDispUpdate();                // 更新显示
            acStatUpdate();                // 发送红外命令和MQTT更新
        }
        break;
        
    case RIGHT_KEY_SHORT:
        // 提高温度（仅适用于自动、制冷、制热模式）
        if (mode <= 2)
        {
            if (temp < 30)
            {
                temp++;
            }
            remoteBtn = 1;                 // 温度+按钮
			irSending = true;
            acDispUpdate();
            acStatUpdate();
        }
        break;
        
    case LEFT_KEY_LONG:
        // 更改空调模式
        mode++;
        if (mode >= 5)
        {
            mode = 0;
            temp = savedTemp;              // 回到自动模式时恢复温度
        }
        if (mode == 3)                     // 进入除湿模式
        {
            savedTemp = temp;              // 保存当前温度
            temp = 16;                     // 除湿模式通常使用固定低温
        }
        remoteBtn = 2;                     // 模式按钮
		irSending = true;
        acDispUpdate();
        acStatUpdate();
        break;
        
    case RIGHT_KEY_LONG:
        // 更改风速
        fan++;
        if (fan >= 4)
        {
            fan = 0;
        }
        remoteBtn = 3;                     // 风速按钮
		irSending = true;
        acDispUpdate();
        acStatUpdate();
        break;
        
    case BOTH_KEY_SHORT:
        // 切换到电源开关控制
        mainState = ON_OFF_CTL;
        clearCentralArea();
        drawOnOffButtons();
        break;
        
    case BOTH_KEY_LONG:
        // 进入主菜单
        mainState = MENU_MAIN;
        showMenu(MAIN_MENU, sizeof(MAIN_MENU) / sizeof(char*));
        currentMenuSelection = 0;
        updateMenu(0);
        drawMenuButtons();
        break;
    default:
        break;
    }
    keyEvent = NO_KEY;
}

/*
 * 更新显示以显示当前空调状态并指示红外传输
 */
void acDispUpdate()
{
	if (irSending)
	{
    	drawIrBadge(COLOR_ACTIVE_BADGE);       // 显示红外活动
	}
    if (mqttCmd)
    {
        drawMqttBadge(COLOR_ACTIVE_BADGE); // 如果命令来自MQTT显示MQTT活动
    }
    
    // 更新温度显示（除湿和送风模式隐藏）
    if (mode < 3)
    {
        drawBigNumber(String(temp));
    }
    else
    {
        drawBigNumber("    ");             // 除湿和送风模式清除温度显示
    }
    drawModeLabel(mode);
    drawFanLabel(fan);
	timerMs = 0;
    mainState = IR_SENDING;
    retState = TEMP_CTL;
}

/*
 * 发送红外命令并更新MQTT状态
 */
void acStatUpdate()
{
    ac.setTo(temp, mode, fan, remoteBtn);  // 向空调发送红外命令
    if (mqttState == CONNECTED)
    {
        // 发布当前状态到MQTT
        String s = "temp=" + String(temp) + ", mode=" + MODES[mode] + ", fan=" + FANSPEED[fan];
        mqtt.publish(REPORT_TOPIC, s.c_str());
    }
}

/*
 * 电源开关控制界面
 */
void onOffCtrl()
{
    switch (keyEvent)
    {
    case LEFT_KEY_SHORT:
        // 电源开
		irSending = true;
        acDispUpdate();
        ac.on();
        if (mqttState == CONNECTED)
        {
            mqtt.publish(REPORT_TOPIC, "开机");
        }
		timerMs = 0;
        mainState = IR_SENDING;
        retState = TEMP_CTL;               // 发送后返回温度控制
        break;
        
    case RIGHT_KEY_SHORT:
        // 电源关
		irSending = true;
        drawIrBadge(COLOR_ACTIVE_BADGE);
        ac.off();
        if (mqttState == CONNECTED)
        {
            mqtt.publish(REPORT_TOPIC, "关机");
        }
		timerMs = 0;
        mainState = IR_SENDING;
        retState = ON_OFF_CTL;             // 发送后保持在电源控制
        break;
        
    case BOTH_KEY_SHORT:
        // 返回温度控制
        acDispUpdate();
        drawTempButtons();
        break;
        
    case BOTH_KEY_LONG:
        // 进入主菜单
        mainState = MENU_MAIN;
        showMenu(MAIN_MENU, sizeof(MAIN_MENU) / sizeof(char*));
        currentMenuSelection = 0;
        updateMenu(0);
        drawMenuButtons();
        break;
    default:
        break;
    }
    keyEvent = NO_KEY;
}

/*
 * 主菜单导航
 */
void mainMenu()
{
    int newIndex;
    switch (keyEvent)
    {
    case LEFT_KEY_SHORT:
        // 导航菜单项
        newIndex = currentMenuSelection + 1;
        if (newIndex >= sizeof(MAIN_MENU) / sizeof(char*))
        {
            newIndex = 0;
        }
        updateMenu(newIndex);
        break;
        
    case RIGHT_KEY_SHORT:
        // 选择菜单项
        switch (currentMenuSelection)
        {
        case 0:        // 捕获并初始化
            mainState = INIT;
            l2State = STEP1;
            break;
        case 1:        // 查找下一匹配
            l2State = STEP1;
            mainState = FIND_NEXT;
            break;
        case 2:        // 加载预定义
            mainState = MENU_PREDEF;
            showMenu(preDefs, ac.cntPredef());
            currentMenuSelection = 0;
            updateMenu(0);
            break;
        case 3:        // 退出
            mainState = START;
            l2State = STEP1;
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
    keyEvent = NO_KEY;
}

/*
 * 预定义空调型号菜单导航
 */
void predefMenu()
{
    int newIndex;
    switch (keyEvent)
    {
    case LEFT_KEY_SHORT:
        // 导航预定义型号
        newIndex = currentMenuSelection + 1;
        if (newIndex >= ac.cntPredef())
        {
            newIndex = 0;
        }
        updateMenu(newIndex);
        break;
        
    case RIGHT_KEY_SHORT:
        // 选择预定义型号
        if (ac.initPredef(currentMenuSelection))
        {
            saveInitInfo();
            clearCentralArea();
            mainState = TEMP_CTL;
            // 重置为默认值
            temp = 25;
            mode = 1;
            fan = 1;
            mainState = TEMP_CTL;
            drawBigNumber(String(temp));
            drawModeLabel(mode);
            drawFanLabel(fan);
            drawTempButtons();
        }
        else
        {
            clearCentralArea();
            mainState = MENU_MAIN;         // 如果失败返回主菜单
        }
        break;
    default:
        break;
    }
    keyEvent = NO_KEY;
}

/*
 * 查找下一个匹配的空调协议
 */
void findNext()
{
    switch (l2State)
    {
    case STEP1:
        if (initOK)
        {
            if (ac.matchNext())            // 尝试查找下一个匹配协议
            {
                matchCnt++;
                // 保存新的匹配计数
                savedData.begin("bc7215 init", false);
                savedData.putBytes("matchIndex", &matchCnt, sizeof(matchCnt));
                savedData.end();
                showNextMatchScrn(matchCnt);
                retState = TEMP_CTL;
            }
            else
            {
                matchCnt = 0;
                showNextFailScrn();        // 未找到更多匹配
                retState = START;
            }
        }
        else
        {
            showNoInitScrn();              // 空调未初始化
            retState = START;
        }
        clearLeftBtn();
        drawOKButton();
        l2State = STEP2;
        break;
        
    case STEP2:
        // 等待确定按钮
        if (keyEvent == RIGHT_KEY_SHORT)
        {
            l2State = STEP1;
            mainState = retState;
        }
        keyEvent = NO_KEY;
        break;
    default:
        break;
    }
}

/*
 * 保存空调初始化数据到非易失性存储
 */
void saveInitInfo()
{
    formatPkt = ac.getFormatPkt();
    dataPkt = ac.getDataPkt();
    savedData.begin("bc7215 init", false);
    savedData.putBytes("format", formatPkt, sizeof(bc7215FormatPkt_t));
    savedData.putBytes("data", dataPkt, sizeof(bc7215DataMaxPkt_t));
    savedData.putBytes("matchIndex", &matchCnt, sizeof(matchCnt));
    
    // 如果必要，保存额外的红外数据和格式信息
    if (ac.extraSample())
    {
        savedData.putBytes("extraFormat", ac.getExtra().body.msg.fmt, sizeof(bc7215FormatPkt_t));
        savedData.putBytes("extraData", (bc7215DataMaxPkt_t*)ac.getExtra().body.msg.datPkt, sizeof(bc7215DataMaxPkt_t));
    }
    savedData.end();
}

/*
 * 当BC7215未响应时显示闪烁的"未连接"消息
 */
void flashNoConn()
{
    static bool showMessage = true;
    
    if (timerMs >= 1000)
    {
        timerMs = 0;
        showMessage = !showMessage;  // 切换显示状态
    }
    
    if (showMessage)
    {
        // Show error message
        tft.setTextColor(COLOR_TEXT, COLOR_BG);
        String str = "BC7215A";
        tft.drawString(str, SCREEN_W / 2, SCREEN_H / 2 - tft.fontHeight());
        str = "未连接....";
        tft.drawString(str, SCREEN_W / 2, SCREEN_H / 2 + 4);
    }
    else
    {
        clearCentralArea();  // 清除消息以产生闪烁效果
    }
    
    timerMs += interval;
}

/*
 * 管理WiFi连接及视觉反馈
 */
void wifiConnect()
{
    static int connTimer;
    static int progressPercent = 0;
    
    switch (wifiState)
    {
    case CONNECTING:
        // 显示连接进度
        progressPercent = (connTimer * 100) / 800;  // 计算进度百分比
        if (progressPercent > 100) progressPercent = 100;
        
        // 使用进度条显示连接状态
        if (connTimer == 0)
        {
            drawWiFiBadge(COLOR_ONLINE_BADGE);  // 显示标识
        }
        else if (connTimer == 400)
        {
            drawWiFiBadge(COLOR_BG);       // 隐藏标识（使用背景色）
        }
        connTimer += interval;
        if (connTimer >= 800)
        {
            connTimer = 0;
        }
        
        // 检查连接是否建立
        if (WiFi.status() == WL_CONNECTED)
        {
            drawWiFiBadge(COLOR_ONLINE_BADGE);
            wifiState = CONNECTED;
            Serial.println("WiFi已连接");
            progressPercent = 0;  // 重置进度
        }
        break;
        
    case CONNECTED:
        // 监控连接状态
        if (WiFi.status() != WL_CONNECTED)
        {
            drawWiFiBadge(COLOR_BG);
            wifiState = CONNECTING;
            drawMqttBadge(COLOR_BG);
            mqttState = WAITING;        // 连接丢失，尝试重连
            connTimer = 0;
            Serial.println("WiFi已断开");
        }
        break;
    }
}

/*
 * 管理MQTT连接及视觉反馈
 */
void mqttConnect()
{
    static int connTimer;
    switch (mqttState)
    {
    case WAITING:
        // 在尝试MQTT之前等待WiFi连接
        if (wifiState == CONNECTED)
        {
            if (mqtt.connect(MQTT_CLIENT_ID, NULL, NULL, MQTT_LWT, 0, true, "离线"))
            {
                mqttOnlineAction();        // 连接成功
            }
            else
            {
                mqttState = CONNECTING;    // 开始连接尝试
            }
        }
        break;
        
    case CONNECTING:
        // 连接时闪烁MQTT标识
        if (connTimer == 0)
        {
            drawMqttBadge(COLOR_BG);       // 隐藏标识
        }
        else if (connTimer == 500)
        {
            drawMqttBadge(COLOR_ONLINE_BADGE);  // 显示标识
        }
        connTimer += interval;
        if (connTimer >= 1000)
        {
            connTimer = 0;
            // 重试连接
            mqtt.connect(MQTT_CLIENT_ID, NULL, NULL, MQTT_LWT, 0, true, "离线");
        }
        
        if (mqtt.connected())
        {
            mqttOnlineAction();            // 连接成功
        }
        break;
        
    case CONNECTED:
        // 监控连接并处理消息
        if (!mqtt.connected())
        {
            drawMqttBadge(COLOR_BG);
            mqttState = CONNECTING;        // 连接丢失，尝试重连
            Serial.println("MQTT已断开");
        }
        else
        {
            mqtt.loop();                   // 处理传入的MQTT消息
        }
        break;
    }
}

/*
 * 建立MQTT连接时执行的操作
 */
void mqttOnlineAction()
{
    Serial.println("MQTT已连接");
    mqtt.publish(MQTT_LWT, "在线", true);    // 发布在线状态

    // 订阅控制主题
    mqtt.subscribe(TEMP_TOPIC);
    mqtt.subscribe(MODE_TOPIC);
    mqtt.subscribe(FAN_TOPIC);
    mqtt.subscribe(POWER_TOPIC);
    
    mqttState = CONNECTED;
    drawMqttBadge(COLOR_ONLINE_BADGE);
}

/*
 * MQTT消息回调处理器
 * 处理传入的MQTT命令并更新空调设置
 */
void processMqtt(char* topic, byte* payload, unsigned int length)
{
    Serial.println("MQTT主题: " + String(topic));
    
    // 将载荷转换为以null结尾的字符串
    char val[length + 1];
    memcpy(val, payload, length);
    val[length] = '\0';
    int value = atoi(val);
    
    if (strcmp(topic, TEMP_TOPIC) == 0)        // 温度控制
    {
        if ((value >= 16) && (value <= 30))
        {
            temp = value;
            Serial.print("新温度 = ");
            Serial.println(value);
			irSending = true;
            mqttCmd = true;                    // 标记为MQTT命令
            acDispUpdate();
            acStatUpdate();
        }
        else
        {
            Serial.println("无效的温度值");
        }
    }
    else if (strcmp(topic, MODE_TOPIC) == 0)   // 模式控制
    {
        if ((value >= 0) && (value <= 4))
        {
            mode = value;
            Serial.print("新模式 = ");
            Serial.println(MODES[mode]);
			irSending = true;
            mqttCmd = true;
            acDispUpdate();
            acStatUpdate();
        }
        else
        {
            Serial.println("无效的模式值");
        }
    }
    else if (strcmp(topic, FAN_TOPIC) == 0)    // 风速控制
    {
        if ((value >= 0) && (value <= 3))
        {
            fan = value;
            Serial.print("新风速 = ");
            Serial.println(FANSPEED[fan]);
			irSending = true;
            mqttCmd = true;
            acDispUpdate();
            acStatUpdate();
        }
        else
        {
            Serial.println("无效的风速值");
        }
    }
    else if (strcmp(topic, POWER_TOPIC) == 0)  // 电源控制
    {
        if ((value >= 0) && (value <= 1))
        {
			irSending = true;
            drawIrBadge(COLOR_ACTIVE_BADGE);
            mqttCmd = true;
            if (value == 1)                    // 电源开
            {
                Serial.println("开机");
                acDispUpdate();
                drawTempButtons();
                if (mqttState == CONNECTED)
                {
                    mqtt.publish(REPORT_TOPIC, "开机");
                }
                ac.on();
				timerMs = 0;
                mainState = IR_SENDING;
                retState = TEMP_CTL;
            }
            if (value == 0)                    // 电源关
            {
                Serial.println("关机");
                if (mqttState == CONNECTED)
                {
                    mqtt.publish(REPORT_TOPIC, "关机");
                }
                clearCentralArea();
                drawOnOffButtons();
                ac.off();
				timerMs = 0;
                mainState = IR_SENDING;
                retState = ON_OFF_CTL;
            }
        }
        else
        {
            Serial.println("无效的电源值");
        }
    }
}

/*
 * 按钮状态机和事件检测
 * 处理去抖动、长按检测和多按钮组合
 */
void updateKeyStatus()
{
    bool        leftPressed = (digitalRead(BUTTON_LEFT) == LOW);
    bool        rightPressed = (digitalRead(BUTTON_RIGHT) == LOW);
    static bool previousLeft = false;
    static bool previousRight = false;
    static int  keyTimer = 0;

    switch (keyboardState)
    {
    case BOTH_RELEASED:
        // 无按钮按下 - 等待按钮按下
        if (leftPressed || rightPressed)
        {
            if (leftPressed && rightPressed)
            {
                keyboardState = BOTH_PRESSED;
            }
            else
            {
                keyboardState = ONE_PRESSED;
            }
            keyTimer = 0;
        }
        break;
        
    case ONE_PRESSED:
        // 单个按钮按下 - 检查长按或第二个按钮
        if ((leftPressed && previousLeft) || (rightPressed && previousRight))
        {
            keyTimer += interval;
            if (keyTimer > 2000)           // 长按阈值2秒
            {
                keyboardState = ONE_LONG_PRESSED;
                if (leftPressed)
                {
                    keyEvent = LEFT_KEY_LONG;
                }
                else
                {
                    keyEvent = RIGHT_KEY_LONG;
                }
            }
        }
        else if ((leftPressed != previousLeft) || (rightPressed != previousRight))
        {
            keyTimer = 0;                  // 按钮变化时重置定时器
        }
        
        if (leftPressed && rightPressed)
        {
            keyTimer = 0;
            keyboardState = BOTH_PRESSED;
        }
        else if (!leftPressed && !rightPressed)
        {
            // 按钮释放 - 生成短按事件
            keyboardState = BOTH_RELEASED;
            if (previousLeft)
            {
                keyEvent = LEFT_KEY_SHORT;
            }
            else
            {
                keyEvent = RIGHT_KEY_SHORT;
            }
        }
        break;
        
    case BOTH_PRESSED:
        // 两个按钮都按下 - 检查长按
        if (leftPressed && rightPressed)
        {
            keyTimer += interval;
            if (keyTimer > 2000)
            {
                keyEvent = BOTH_KEY_LONG;
            }
        }
        else
        {
            if (!leftPressed && !rightPressed)
            {
                keyboardState = BOTH_RELEASED;
            }
            else
            {
                keyboardState = ONE_RELEASED;
            }
            if (keyTimer <= 2000)
            {
                keyEvent = BOTH_KEY_SHORT;
            }
        }
        break;
        
    case ONE_RELEASED:
        // 一个按钮释放而另一个仍按下
        if (leftPressed && rightPressed)
        {
            keyboardState = BOTH_PRESSED;
            keyTimer = 0;
        }
        else if (!leftPressed && !rightPressed)
        {
            keyboardState = BOTH_RELEASED;
        }
        break;
        
    case ONE_LONG_PRESSED:
        // 检测到长按 - 等待释放
        if (!leftPressed && !rightPressed)
        {
            keyboardState = BOTH_RELEASED;
        }
        break;
    default:
        break;
    }
    previousLeft = leftPressed;
    previousRight = rightPressed;
}

/***********************************************************************************
 * 显示和用户界面函数
 * 
 * 这些函数处理所有显示操作包括：
 * - 顶部状态标识（WiFi、MQTT、红外）
 * - 主数字显示
 * - 模式和风速标签
 * - 按钮布局
 * - 菜单系统
 * - 初始化屏幕
 ***********************************************************************************/

/*
 * 计算顶部标识的等分布位置
 */
void computeTopBadgesLayout()
{
    L.badgeX0 = BADGE_SIDE_MARGIN;
    L.badgeX1 = L.badgeX0 + BADGE_WIDTH + BADGE_GAP;
    L.badgeX2 = L.badgeX1 + BADGE_WIDTH + BADGE_GAP;
}

/*
 * 绘制带居中文字的顶部状态标识
 */
void drawBadge(int xStart, uint16_t bg, const char* text)
{
    tft.fillRoundRect(xStart, BADGE_Y, BADGE_WIDTH, BADGE_HEIGHT, BADGE_RADIUS, bg);
	tft.unloadFont();
    tft.setTextFont(BADGE_FONT);
    tft.setTextSize(BADGE_SIZE);
    tft.setTextColor(COLOR_BADGE_FG);
    
    // 在标识中居中文字
    int fh = tft.fontHeight();
    int tx = xStart + BADGE_WIDTH / 2;
    int ty = BADGE_Y + (BADGE_HEIGHT - fh) / 2;
    tft.drawString(text, tx, ty);
	tft.loadFont(Dengl16);
}

/*
 * 清除主数字显示区域
 */
void clearNumberArea() 
{ 
    tft.fillRect(0, L.numberAreaY, SCREEN_W, L.numberAreaH, COLOR_BG); 
}

/*
 * 在屏幕中央显示大号温度数字
 */
void drawBigNumber(const String& value)
{
    clearNumberArea();
	tft.unloadFont();
    tft.setTextFont(8);                    // 大数字所用字体 
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.drawString(value, SCREEN_W / 2, L.numberY);
	tft.loadFont(Dengl16);
}

/*
 * 在左侧显示空调模式标签
 */
void drawModeLabel(int mode)
{
    tft.setTextDatum(TL_DATUM);
//    tft.setTextFont(2);
//    tft.setTextSize(1);
    tft.setTextColor(COLOR_BG, COLOR_LABEL_BG);
    int y = L.middleLabelY;
	int p = L.middleLabelPad;
    tft.fillRect(0, y - 6, SCREEN_W / 2, tft.fontHeight() + 8, COLOR_BG);
    tft.fillRect(0, y - 6, tft.textWidth(MODES[mode])+2*p, tft.fontHeight() + 8, COLOR_LABEL_BG);
    tft.drawString(MODES[mode], p, y);
    tft.setTextDatum(TC_DATUM);
}

/*
 * 在右侧显示风速标签
 */
void drawFanLabel(int fan)
{
    tft.setTextDatum(TR_DATUM);
//    tft.setTextFont(2);
//    tft.setTextSize(1);
    tft.setTextColor(COLOR_BG, COLOR_LABEL_BG);
    int y = L.middleLabelY;
	int p = L.middleLabelPad;
    tft.fillRect(SCREEN_W / 2, y - 6, SCREEN_W / 2, tft.fontHeight() + 8, COLOR_BG);
    tft.fillRect(SCREEN_W -tft.textWidth(FANSPEED[fan])-2*p, y - 6, tft.textWidth(FANSPEED[fan])+2*p, tft.fontHeight() + 8, COLOR_LABEL_BG);
    tft.drawString(FANSPEED[fan], SCREEN_W - p, y);
    tft.setTextDatum(TC_DATUM);
}

/*
 * 清除中央显示区域（标识和按钮之间）
 */
void clearCentralArea() 
{ 
    tft.fillRect(0, L.numberAreaY, SCREEN_W, 177 - L.numberAreaY, COLOR_BG); 
}

/*
 * 绘制温度控制按钮（-和+带模式/风速标签）
 */
void drawTempButtons()
{
	// 左按钮 (TEMP -)
    tft.fillRoundRect(4, 178, 61, 58, 8, COLOR_ACTION_BG);
    // 右按钮 (TEMP +)
    tft.fillRoundRect(70, 178, 61, 58, 8, COLOR_ACTION_BG);
    
    // 按钮上文字
//    tft.setTextFont(2);
//    tft.setTextSize(1);
    tft.setTextColor(COLOR_ACTION_FG, COLOR_ACTION_BG);
    tft.drawString("模式", 34, 216);       // 长按左键改变模式
    tft.drawString("风速", 100, 216);      // 长按右键改变风速
    
    // 按钮上+/-符号
	tft.unloadFont();
    tft.setTextFont(4);
    tft.setTextSize(3);
    tft.setTextColor(COLOR_ACTION_FG);
    tft.drawString("-", 34, 160);          // 短按左键温度降低
    tft.drawString("+", 100, 160);         // 短按右键温度升高
	tft.loadFont(Dengl16);
}

/*
 * 绘制菜单导航按钮
 */
void drawMenuButtons()
{
    drawSELButton();
    drawOKButton();
}

/*
 * 绘制确定按钮（右侧）
 */
void drawOKButton()
{
    tft.fillRoundRect(70, 178, 61, 58, 8, COLOR_ACTION_BG);
	tft.unloadFont();
    tft.setTextFont(4);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_ACTION_FG);
    tft.drawString("OK", 100, 180 + (58 - tft.fontHeight()) / 2);
	tft.loadFont(Dengl16);
}

/*
 * 绘制退出按钮（左侧）
 */
void drawEscButton()
{
    tft.fillRoundRect(4, 178, 61, 58, 8, COLOR_ACTION_BG);
	tft.unloadFont();
    tft.setTextFont(4);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_ACTION_FG);
    tft.drawString("ESC", 34, 180 + (58 - tft.fontHeight()) / 2);
	tft.loadFont(Dengl16);
}

/*
 * 绘制选择按钮（左侧）
 */
void drawSELButton()
{
    tft.fillRoundRect(4, 178, 61, 58, 8, COLOR_ACTION_BG);
	tft.unloadFont();
    tft.setTextFont(4);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_ACTION_FG);
    tft.drawString("SEL", 34, 180 + (58 - tft.fontHeight()) / 2);
	tft.loadFont(Dengl16);
}

/*
 * 绘制电源控制按钮（开/关）
 */
void drawOnOffButtons()
{
    tft.fillRoundRect(4, 178, 61, 58, 8, COLOR_ACTION_BG);
    tft.fillRoundRect(70, 178, 61, 58, 8, COLOR_ACTION_BG);
	tft.unloadFont();
    tft.setTextFont(4);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_ACTION_FG);
    tft.drawString("ON", 34, 180 + (58 - tft.fontHeight()) / 2);
    tft.drawString("OFF", 100, 180 + (58 - tft.fontHeight()) / 2);
	tft.loadFont(Dengl16);
}

/*
 * 清除左按钮区域
 */
void clearLeftBtn()
{
    tft.fillRoundRect(4, 178, 61, 58, 8, COLOR_BG);
}

/*
 * 清除右按钮区域
 */
void clearRightBtn()
{
    tft.fillRoundRect(70, 178, 61, 58, 8, COLOR_BG);
}

/*
 * 显示带项目列表的菜单
 */
void showMenu(const char* items[], int cnt)
{
    menuItems = items;
    clearCentralArea();
    tft.setTextDatum(TL_DATUM);
//    tft.setTextFont(2);
//    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
	tft.setTextWrap(false);
	int y = L.numberAreaY+tft.fontHeight()+8;
    tft.drawString("菜单 :", 4, L.numberAreaY+4);
    
    // 显示所有菜单项
    for (int i = 0; i < cnt; i++)
    {
        tft.drawString(" " + String(menuItems[i]) + " ", 8, y + i * tft.fontHeight());
    }
    currentMenuSelection = 0;
    
    // 在底部显示库版本
	tft.unloadFont();
    tft.setTextFont(1);
    tft.drawString(String(ac.getLibVer()), 2, 177 - tft.fontHeight());
    tft.setTextDatum(TC_DATUM);
	tft.loadFont(Dengl16);
}

/*
 * 更新菜单选择高亮
 */
void updateMenu(int selected)
{
    tft.setTextDatum(TL_DATUM);
//    tft.setTextFont(2);
//    tft.setTextSize(1);
    
    // Clear previous selection
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
	tft.setTextWrap(false);
	int y = L.numberAreaY+tft.fontHeight()+8;
	tft.fillRect(8, y-2+tft.fontHeight()*currentMenuSelection, SCREEN_W-8*2, tft.fontHeight(), COLOR_BG);
    tft.drawString(" " + String(menuItems[currentMenuSelection]) + " ", 8,
        y + currentMenuSelection * tft.fontHeight());
    
    // Highlight new selection
    tft.setTextColor(COLOR_TEXT, COLOR_ACTION_BG);
	tft.fillRect(8, y-2+tft.fontHeight()*selected, SCREEN_W-8*2, tft.fontHeight(), COLOR_ACTION_BG);
    tft.drawString(" " + String(menuItems[selected]) + " ", 8, y + selected * tft.fontHeight());
    
    currentMenuSelection = selected;
    tft.setTextDatum(TC_DATUM);
}

/*
 * 状态标识绘制函数
 */
void drawIrBadge(uint16_t color) 
{ 
    drawBadge(BADGE_WIDTH * 2 + BADGE_GAP * 2 + BADGE_SIDE_MARGIN, color, " IR "); 
}

void drawWiFiBadge(uint16_t color) 
{ 
    drawBadge(BADGE_SIDE_MARGIN, color, "WiFi"); 
}

void drawMqttBadge(uint16_t color) 
{ 
    drawBadge(BADGE_WIDTH + BADGE_GAP + BADGE_SIDE_MARGIN, color, "MQTT"); 
}

/*
 * 初始化屏幕函数
 */

/*
 * 显示初始设置指导屏幕
 */
void showInitScrn1()
{
    clearCentralArea();
//    tft.setTextFont(2);
//    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
	int y = L.numberAreaY + 4;
    tft.drawString(" 采集并初始化", SCREEN_W / 2, y);
    tft.drawString("将遥控器设置为", SCREEN_W / 2, y + tft.fontHeight());
    tft.drawString("制冷模式 25°C", SCREEN_W / 2, y + tft.fontHeight() * 3);
    tft.drawString("准备好后按OK", SCREEN_W / 2, y + tft.fontHeight() * 5);
}

/*
 * 显示红外信号捕获指导屏幕
 */
void showInitScrn2(const String& keyText)
{
    clearCentralArea();
//    tft.setTextFont(2);
//    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
	int y = L.numberAreaY + 4;
    tft.drawString("请对准接收器", SCREEN_W / 2, y);
    tft.drawString("按下 ", SCREEN_W / 2, y + tft.fontHeight());
    tft.drawString(keyText, SCREEN_W / 2, y + tft.fontHeight() * 3);
    tft.drawString(" 键采集信号", SCREEN_W / 2, y + tft.fontHeight() * 5);
}

/*
 * 显示需要额外捕获的屏幕
 */
void showInitScrn3(const String& keyText)
{
    clearCentralArea();
    tft.setTextDatum(TL_DATUM);
//    tft.setTextFont(2);
//    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
	int y = L.numberAreaY + 4;
    tft.drawString("还需采集", 4, y);
    tft.drawString(keyText, SCREEN_W / 2 - tft.textWidth(keyText) / 2, y + tft.fontHeight() * 2);
    tft.drawString("按键的信号，", 4, y + tft.fontHeight() * 4);
    tft.drawString("按OK开始采集", 4, y + tft.fontHeight() * 5);
    tft.setTextDatum(TC_DATUM);
}

/*
 * 显示初始化失败消息
 */
void showInitScrn4()
{
//    tft.setTextFont(2);
//    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
	int y = L.numberAreaY + 4;
    tft.drawString("  初始化失败  ", SCREEN_W / 2, y);
    tft.drawString("请检查遥控器设置", SCREEN_W / 2, y + tft.fontHeight());
    tft.drawString("并再次尝试", SCREEN_W / 2, y + tft.fontHeight() * 2);
}

/*
 * 显示初始化成功消息
 */
void showInitScrn5()
{
//    tft.setTextFont(2);
//    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
	int y = L.numberAreaY + 4;
    tft.drawString("  初始化成功  ", SCREEN_W / 2, y);
    tft.drawString("可以控制空调了", SCREEN_W / 2, y + tft.fontHeight());
}

/*
 * 占位函数（未使用）
 */
void showInitScrn6() { }

/*
 * 显示带确定按钮的初始化成功屏幕
 */
void showInitOKMsg()
{
    clearCentralArea();
    drawOKButton();
    showInitScrn5();        // 显示成功消息
    clearLeftBtn();
}

/*
 * 显示带重试选项的初始化失败屏幕
 */
void showInitFailMsg()
{
    clearCentralArea();
    drawOKButton();
    showInitScrn4();        // 显示失败消息
}

/*
 * 显示找到成功匹配的消息
 */
void showNextMatchScrn(int8_t matchCnt)
{
    clearCentralArea();
//    tft.setTextFont(2);
//    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.drawString("匹配成功", SCREEN_W / 2, L.numberAreaY);
    tft.drawString("匹配数: " + String(matchCnt), SCREEN_W / 2, L.numberAreaY + tft.fontHeight());
    tft.drawString("按OK继续", SCREEN_W / 2, L.numberAreaY + tft.fontHeight() * 2);
}

/*
 * 显示未找到更多匹配的消息
 */
void showNextFailScrn()
{
    clearCentralArea();
//    tft.setTextFont(2);
//    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.drawString("匹配失败", SCREEN_W / 2, L.numberAreaY);
    tft.drawString("程序将重启", SCREEN_W / 2, L.numberAreaY + tft.fontHeight());
    tft.drawString("按OK继续", SCREEN_W / 2, L.numberAreaY + tft.fontHeight() * 2);
}

/*
 * 显示未初始化错误消息
 */
void showNoInitScrn()
{
    clearCentralArea();
//    tft.setTextFont(2);
//    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.drawString("尚未初始化", SCREEN_W / 2, L.numberAreaY);
    tft.drawString("初始化后才可使用", SCREEN_W / 2, L.numberAreaY + tft.fontHeight());
    tft.drawString("按OK继续", SCREEN_W / 2, L.numberAreaY + tft.fontHeight() * 2);
}
