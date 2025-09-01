/*
 * ESP32_AC_Serial_Monitor.ino
 *
 * 描述：ESP32空调红外控制器
 * 功能：使用BC7215红外模块控制空调，支持红外信号采样、参数设置、
 *       状态保存/恢复、多种控制模式等功能
 * 硬件：ESP32 (TTGO T-Display)、BC7215红外模块
 * 依赖：bc7215.h, bc7215ac.h, SoftwareSerial.h, EEPROM.h
 * 作者：Bitcode
 * 日期：2025-08-05
 */

#include <EEPROM.h>
#include <bc7215.h>
#include <bc7215ac.h>

// 引脚和常量定义
const int    LED = 4;                                                             // 板载LED引脚
const String MODES[] = { "自动", "制冷", "制热", "除湿", "通风", "保持" };        // 空调模式
const String FANSPEED[] = { "自动", "低", "中", "高", "保持" };                   // 风速等级
const String EXTRA_KEY[] = { "温度加或减", "模式", "风力调节" };                  // 额外采样按键

// 状态机枚举
enum L1_STATE
{
    MAIN_MENU,      // 主菜单
    CAPTURE,        // 信号采样
    AC_CONTROL,     // 空调控制
    BACKUP,         // 备份数据
    RESTORE,        // 恢复数据
    FIND_NEXT,      // 查找下一协议
    LOAD_PREDEF     // 加载预定义
};
enum L2_STATE
{
    STEP1, STEP2, STEP3, STEP4, STEP5, STEP6, STEP7    // 二级状态步骤
};

// BC7215通信设置
HardwareSerial bc7215Serial(1);                    // BC7215连接到UART1
BC7215         bc7215Board(bc7215Serial, 27, 26);   // mod=GPIO27, busy=GPIO26
BC7215AC       ac(bc7215Board);                    // 空调控制对象

// 全局变量
char                      choice;           // 用户选择
int                       counter;          // 计数器
int                       idleCntr;         // 空闲计数器
int                       msgCnt;           // 消息计数
int                       extra;            // 额外采样标志
int                       timeMs;           // 时间计数
int                       interval;         // 循环间隔
bool                      initialized = false;  // 初始化状态
L1_STATE                  mainState;        // 主状态
L2_STATE                  l2State;          // 二级状态
L2_STATE                  goBackState;      // 返回状态
String                    newParam = "";    // 新参数字符串
const bc7215DataVarPkt_t* dataPkt;         // 数据包指针
const bc7215FormatPkt_t*  formatPkt;       // 格式包指针
bc7215DataMaxPkt_t        sampleData[4];   // 采样数据缓冲区
bc7215FormatPkt_t         sampleFormat[4]; // 采样格式缓冲区

/*
 * 初始化：设置EEPROM、串口通信和LED
 */
void setup()
{
    EEPROM.begin(33 + sizeof(bc7215DataMaxPkt_t));        // 初始化EEPROM
    Serial.begin(115200);                                 // 调试串口
    bc7215Serial.begin(19200, SERIAL_8N2, 25, 33);       // BC7215串口，RX=GPIO25, TX=GPIO33
    pinMode(LED, OUTPUT);                                 // LED输出模式
    ledOff();                                             // 关闭LED
    mainState = MAIN_MENU;
    l2State = STEP1;
    interval = 10;
}

/*
 * 主循环：状态机处理各种功能模块
 */
void loop()
{
    switch (mainState)
    {
    case MAIN_MENU:
        mainMenuJob();
        break;
    case CAPTURE:
        captureJob();
        break;
    case AC_CONTROL:
        acControlJob();
        break;
    case BACKUP:
        backupJob();
        break;
    case RESTORE:
        restoreJob();
        break;
    case FIND_NEXT:
        findNextJob();
        break;
    case LOAD_PREDEF:
        loadPredefJob();
        break;
    default:
        break;
    }
    delay(interval);
    // 其他任务代码可在此处添加
}

/*
 * 主菜单处理：显示菜单并处理用户选择
 */
void mainMenuJob()
{
    switch (l2State)
    {
    case STEP1:
        showMainMenu();
        clearSerialBuf();
        l2State = STEP2;
        break;
    case STEP2:
        if (Serial.available())
        {
            switch (Serial.read())
            {
            case '1':  // 采样并初始化
                mainState = CAPTURE;
                l2State = STEP1;
                break;
            case '2':  // 控制空调
                if (initialized)
                {
                    mainState = AC_CONTROL;
                    l2State = STEP1;
                }
                else
                {
                    Serial.println("\n空调控制库尚未初始化，请先进行初始化");
                }
                break;
            case '3':  // 保存数据
                if (initialized)
                {
                    mainState = BACKUP;
                    l2State = STEP1;
                }
                else
                {
                    Serial.println("\n空调控制库尚未初始化，仅在初始化成功后才可保存数据");
                }
                break;
            case '4':  // 恢复数据
                mainState = RESTORE;
                l2State = STEP1;
                break;
            case '5':  // 查找下一协议
                if (initialized)
                {
                    mainState = FIND_NEXT;
                    l2State = STEP1;
                }
                else
                {
                    Serial.println("\n仅在初始化后才可使用此功能");
                }
                break;
            case '6':  // 加载预定义
                mainState = LOAD_PREDEF;
                l2State = STEP1;
                break;
            default:
                mainState = MAIN_MENU;
                l2State = STEP1;
                break;
            }
            clearSerialBuf();
        }
        break;
    default:
        break;
    }
}

/*
 * 信号采样处理：捕获遥控器红外信号并初始化空调控制库
 */
void captureJob()
{
    switch (l2State)
    {
    case STEP1:  // 提示用户准备
        Serial.println("\n现在进行红外信号采样及空调控制库初始化，请将空调遥控器调节至< 制冷模式，25°C "
                       ">，准备好后输入任意内容继续...");
        clearSerialBuf();
        l2State = STEP2;
        break;
    case STEP2:  // 等待用户确认
        if (Serial.available())
        {
            Serial.println("现在请对准红外接收头按遥控器<风力调节>按键，接收信号后将自动转至下一步...");
            initialized = false;
            ac.startCapture();        // 开始红外信号捕获
            l2State = STEP3;
        }
        break;
    case STEP3:  // 处理第一次采样结果
        if (ac.signalCaptured())
        {
            ac.stopCapture();
            
            if (ac.init())  // 尝试初始化
            {
                initialized = true;
                ledOn();
            	Serial.print("收到数据：");
            	dataPkt = ac.getDataPkt();
            	printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
                Serial.println("使用所接收数据初始化空调控制库  **成功** !!! ");
                extra = ac.extraSample();
                if (extra > 0)  // 需要额外采样
                {
                    if (extra > 3) extra = 3;
                    Serial.print("此空调格式较特殊，需要进一步采样原遥控器信号，现在请再按遥控器上 <<");
                    Serial.println(EXTRA_KEY[extra - 1] + ">> 按键进一步采样..");
                    Serial.println("键盘输入任意内容进入采样步骤...");
                    clearSerialBuf();
                    l2State = STEP4;
                }
            }
            else  // 初始化失败
            {
                ledOff();
                Serial.println("使用所接收数据初始化空调控制库**失败**, "
                               "可能是遥控器状态设置错误或接收解码错误，请检查遥控器设置后重新尝试");
            }
            l2State = STEP6;
        }
        break;
    case STEP4:  // 准备额外采样
        if (Serial.available())
        {
            Serial.println("开始采样...");
            ac.startCapture();
            idleCntr = 0;
            msgCnt = 0;
            l2State = STEP5;
        }
        break;
    case STEP5:  // 额外采样处理
        if (ac.signalCaptured((bc7215DataVarPkt_t*)&sampleData[msgCnt], &sampleFormat[msgCnt]))
        {
            if (msgCnt < 3) msgCnt++;
            idleCntr = 0;
            if (extra < 3)
            {
                ac.saveExtra(sampleData[0], sampleFormat[0]);
                Serial.println("采集信号完成！");
                l2State = STEP6;
            }
        }
        else
        {
            if ((msgCnt != 0) && (!ac.isBusy()))
            {
                idleCntr += 10;
                if (idleCntr >= 200)  // 采样超时
                {
                    ac.stopCapture();
                    Serial.println("采集信号完成！,共采集 " + String(msgCnt) + " 个信号");
                    for (int i = 0; i < msgCnt; i++)
                    {
                        printData(sampleData[i].data, (sampleData[i].bitLen + 7) / 8);
                    }
                    if (ac.init(msgCnt, sampleData, sampleFormat))
                    {
                        Serial.println("使用新采集信号重新初始化成功！");
                        ledOn();
                    }
                    else
                    {
                        Serial.println("使用新采集信号重新初始化失败...");
                        ledOff();
                    }
                    l2State = STEP6;
                }
            }
        }
        // 等待时LED闪烁
        counter++;
        if (counter & 0x10)
        {
            ledOn();
        }
        else
        {
            ledOff();
        }
        break;
    case STEP6:  // 采样完成提示
        ac.stopCapture();
        Serial.println("现在请输入任意内容，程序将返回主菜单，可开始空调控制...");
        clearSerialBuf();
        l2State = STEP7;
        break;
    case STEP7:  // 等待用户确认返回
        if (Serial.available())
        {
            mainState = MAIN_MENU;
            l2State = STEP1;
        }
        break;
    default:
        break;
    }
}

/*
 * 空调控制处理：设置参数、开关机等操作
 */
void acControlJob()
{
    int comma1, comma2, temp, mode, fan;
    char inputChar;
    
    switch (l2State)
    {
    case STEP1:  // 显示控制菜单
        showCtrlMenu();
        clearSerialBuf();
        l2State = STEP2;
        break;
    case STEP2:  // 处理控制选择
        if (Serial.available())
        {
            switch (Serial.read())
            {
            case '1':  // 设置参数
                showParamMenu();
                clearSerialBuf();
                l2State = STEP3;
                break;
            case '2':  // 开机
                Serial.println("发送空调开机指令");
                dataPkt = ac.on();
                Serial.print("发送数据：");
                printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
                timeMs = 0;
                goBackState = STEP2;
                l2State = STEP4;
                break;
            case '3':  // 关机
                Serial.println("发送空调关机指令");
                dataPkt = ac.off();
                Serial.print("发送数据：");
                printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
                timeMs = 0;
                goBackState = STEP2;
                l2State = STEP4;
                break;
            case '4':  // 返回主菜单
                mainState = MAIN_MENU;
                l2State = STEP1;
                break;
            default:
                break;
            }
            clearSerialBuf();
        }
        break;
    case STEP3:  // 参数设置处理
        if (Serial.available())
        {
            newParam = "";
            do
            {
                inputChar = Serial.read();
                if ((inputChar == '\n') || (inputChar == '\r')) break;
                newParam += inputChar;
            } while (Serial.available());
            
            newParam.trim();
            Serial.println(newParam);
            
            if (newParam.equalsIgnoreCase("exit"))
            {
                Serial.println("退出");
                l2State = STEP1;
            }
            else
            {
                comma1 = newParam.indexOf(',');
                comma2 = newParam.indexOf(',', comma1 + 1);
                
                if (comma1 != -1 && comma2 != -1 && comma2 > comma1)  // 三参数格式
                {
                    temp = newParam.substring(0, comma1).toInt();
                    mode = newParam.substring(comma1 + 1, comma2).toInt();
                    fan = newParam.substring(comma2 + 1).toInt();
                    
                    if (mode < 0 || mode > 4) mode = 5;  // 保持模式
                    if (fan < 0 || fan > 3) fan = 4;     // 保持风速
                    
                    if (temp >= 16 && temp <= 30)
                    {
                        Serial.println("发送指令设置空调为： " + String(temp) + "°C, 模式：" + MODES[mode] + "  风力：" + FANSPEED[fan]);
                    }
                    else
                    {
                        Serial.println("发送指令设置空调为： 温度不变， 模式：" + MODES[mode] + "  风力：" + FANSPEED[fan]);
                    }
                    dataPkt = ac.setTo(temp, mode, fan, -1);
                    Serial.print("发送数据：");
                    printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
                    timeMs = 0;
                    goBackState = STEP3;
                    l2State = STEP4;
                }
                else if (comma1 == -1)  // 仅温度参数
                {
                    temp = newParam.toInt();
                    if (temp >= 16 && temp <= 30)
                    {
                        Serial.println("发送指令设置空调为： " + String(temp) + "°C");
                    }
                    else
                    {
                        Serial.println("发送指令设置空调为： 温度不变");
                    }
                    dataPkt = ac.setTo(temp);
                    Serial.print("发送数据：");
                    printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
                    timeMs = 0;
                    goBackState = STEP3;
                    l2State = STEP4;
                }
                else
                {
                    Serial.println("输入格式错误，请重新输入");
                }
            }
        }
        break;
    case STEP4:  // 等待发送完成
        timeMs += interval;
        if (!ac.isBusy() || (timeMs > 3000))
        {
            if (timeMs > 3000) Serial.println("发送超时");
            Serial.println("发送结束！");
            Serial.println("");
            Serial.println("请继续输入");
            clearSerialBuf();
            l2State = goBackState;
        }
        break;
    default:
        break;
    }
}

/*
 * 数据备份：将初始化数据保存到EEPROM
 */
void backupJob()
{
    switch (l2State)
    {
    case STEP1:
        formatPkt = ac.getFormatPkt();
        dataPkt = ac.getDataPkt();
        EEPROM.put(0, *formatPkt);                              // 保存格式包
        EEPROM.put(33, *((bc7215DataMaxPkt_t*)dataPkt));        // 保存数据包
        EEPROM.commit();
        Serial.println("\n格式信息: ");
        printData(formatPkt, 33);
        Serial.print("数据: ");
        printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
        Serial.println("信息已保存");
        l2State = STEP2;
        break;
    case STEP2:  // 等待用户确认
        if (Serial.available())
        {
            mainState = MAIN_MENU;
            l2State = STEP1;
            clearSerialBuf();
        }
        break;
    default:
        break;
    }
}

/*
 * 数据恢复：从EEPROM读取数据并初始化
 */
void restoreJob()
{
    switch (l2State)
    {
    case STEP1:
        EEPROM.get(0, sampleFormat[0]);                              // 读取格式包
        EEPROM.get(33, sampleData[0]);        // 读取数据包
        Serial.println("\n使用");
        Serial.print("格式信息: ");
        printData(&sampleFormat[0], sizeof(bc7215FormatPkt_t));
        Serial.print("数据: ");
        printData(&sampleData[0].data, (sampleData[0].bitLen + 7) / 8);
        
        if (ac.init(sampleData[0], sampleFormat[0]))
        {
            Serial.println("初始化空调控制库  ***成功*** !");
            initialized = true;
            ledOn();
        }
        else
        {
            Serial.println("初始化空调控制库 失败...");
            initialized = false;
            ledOff();
        }
        Serial.println("输入任意内容继续");
        l2State = STEP2;
        clearSerialBuf();
        break;
    case STEP2:  // 等待用户确认
        if (Serial.available())
        {
            mainState = MAIN_MENU;
            l2State = STEP1;
        }
        break;
    default:
        break;
    }
}

/*
 * 查找下一协议：尝试匹配其他协议格式
 */
void findNextJob()
{
    switch (l2State)
    {
    case STEP1:
        if (ac.matchNext())
        {
            Serial.println("匹配下一协议成功！");
        }
        else
        {
            Serial.println("无其它配批协议，空调控制库需重新初始化");
            initialized = false;
            ledOff();
        }
        Serial.println("输入任意内容继续...");
        clearSerialBuf();
        l2State = STEP2;
        break;
    case STEP2:  // 等待用户确认
        if (Serial.available())
        {
            mainState = MAIN_MENU;
            l2State = STEP1;
        }
        break;
    default:
        break;
    }
}

/*
 * 加载预定义：使用内置协议数据
 */
void loadPredefJob()
{
    int choice;
    switch (l2State)
    {
    case STEP1:  // 显示预定义菜单
        showPredefMenu();
        clearSerialBuf();
        l2State = STEP2;
        break;
    case STEP2:  // 处理用户选择
        if (Serial.available())
        {
            choice = Serial.parseInt();
            Serial.println("已选择第 " + String(choice) + " 种");
            printData(bc7215_ac_predefined_data(choice), 13);
            printData(bc7215_ac_predefined_fmt(choice), 33);
            
            if (ac.initPredef(choice))
            {
                Serial.println("初始化成功 !!! 输入任意内容继续");
                initialized = true;
                ledOn();
            }
            else
            {
                Serial.println("初始化失败.... 输入任意内容继续");
                initialized = false;
                ledOff();
            }
            clearSerialBuf();
            l2State = STEP3;
        }
        break;
    case STEP3:  // 等待用户确认
        if (Serial.available())
        {
            mainState = MAIN_MENU;
            l2State = STEP1;
        }
        break;
    default:
        break;
    }
}

/*
 * 显示主菜单
 */
void showMainMenu()
{
    Serial.println("\n\n**************************************************");
    Serial.println("*      欢迎使用 BC7215 离线万能空调控制演示程序         *");
    Serial.println("**************************************************");
    Serial.print("当前空调控制库状态： ");
    if (initialized)
    {
        Serial.println("***已初始化***");
    }
    else
    {
        Serial.println("未初始化(须初始化后才可使用)");
    }
    Serial.println("请选择：");
    Serial.println("   1. 采样并初始化控制库");
    Serial.println("   2. 控制空调");
    Serial.println("   3. 保存初始化数据");
    Serial.println("   4. 读取已存数据并使用该数据初始化");
    Serial.println("   5. 尝试下一匹配(如初始化成功但无法正确控制空调)");
    Serial.println("   6. 加载预定义协议");
    Serial.println("");
}

/*
 * 显示控制菜单
 */
void showCtrlMenu()
{
    Serial.println("\n空调控制，请选择：");
    Serial.println("   1. 设置空调参数");
    Serial.println("   2. 开机");
    Serial.println("   3. 关机");
    Serial.println("   4. 返回上级菜单");
    Serial.println("");
}

/*
 * 显示参数设置菜单
 */
void showParamMenu()
{
    Serial.println("空调参数调整：请输入所设置温度数字，如：24 ; "
                   "或依次输入'温度'，'模式'，'风力等级'完整三个参数，中间用英文逗号','隔开，如: 24, 1, 2");
    Serial.println("   参数取值范围及含义：  温度(°C)        模式           风力");
    Serial.println("                      范围：16~30     0 - 自动       0 - 自动");
    Serial.println("                                     1 - 制冷       1 - 低");
    Serial.println("                                     2 - 制热       2 - 中");
    Serial.println("                                     3 - 除湿       3 - 高");
    Serial.println("                                     4 - 通风");
    Serial.println(" * 超出以上范围数值，表示该项维持现有状态");
    Serial.println("------------------------------------------------------------------------------------");
    Serial.println("(注意：仅限被控空调所支持参数，例如：如对单冷空调设置制热模式，结果将为无法预测)");
    Serial.println("现在请输入所设值空调参数： (输入'exit'退回上级菜单，大小写不敏感)");
    Serial.println("");
}

/*
 * 显示预定义协议菜单
 */
void showPredefMenu()
{
    Serial.println("");
    Serial.println("有少数协议不支持直接由BC7215A芯片解码，以预定义协议方式提供。");
    Serial.println("当直接采样不成功或初始化失败时，可尝试使用预定义数据控制空调。");
    Serial.println("协议名称中所列品牌仅供参考，如不能控制，请逐个尝试所有预定义协议");
    Serial.println("请选择：");
    for (int i = 0; i < ac.cntPredef(); i++)
    {
        Serial.println("  " + String(i) + ". " + ac.getPredefName(i));
    }
    Serial.println("");
}

/*
 * 清除串口缓冲区
 */
void clearSerialBuf()
{
    while (Serial.available())
    {
        Serial.read();
    }
}

/*
 * 以十六进制格式打印数据
 */
void printData(const void* data, uint8_t len)
{
    for (int i = 0; i < len; i++)
    {
        Serial.print(*((uint8_t*)data + i), HEX);
        Serial.print(' ');
    }
    Serial.println("");
}

/*
 * LED控制函数：开启和关闭LED指示灯
 */
void ledOn() { digitalWrite(LED, LOW); }    // 开启LED（低电平点亮）
void ledOff() { digitalWrite(LED, HIGH); }  // 关闭LED（高电平熄灭）
