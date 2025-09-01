/*
 * ESP8266_AC_CTRL_nonblocking.ino
 *
 * 描述: 本程序允许使用ESP8266微控制器配合BC7215红外模块来控制空调。
 * 它支持从空调遥控器捕获红外信号、初始化遥控库、从EEPROM保存/加载设置、
 * 以及控制空调参数（温度、模式、风速）。
 * 程序提供串口界面供用户交互，用于初始化、控制和管理空调设置。
 * 本版本为非阻塞式实现，使用状态机模式。
 *
 * 硬件: ESP8266 (NodeMCU), BC7215红外模块
 * 依赖库: bc7215.h, bc7215ac.h, SoftwareSerial.h, EEPROM.h
 * 作者: Bitcode
 * 日期: 2025-08-05
 */

#include <EEPROM.h>
#include <SoftwareSerial.h>
#include <bc7215.h>
#include <bc7215ac.h>

// 引脚和常量定义
const int    LED = 2;                                                             // NodeMCU板载LED引脚
const String MODES[] = { "自动", "制冷", "制热", "除湿", "通风", "保持" };        // 空调模式
const String FANSPEED[] = { "自动", "低", "中", "高", "保持" };                   // 风速档位
const String EXTRA_KEY[] = { "温度加或减", "模式", "风力调节" };                  // 采样用的额外按键

// 一级状态枚举
enum L1_STATE
{
    MAIN_MENU,      // 主菜单
    CAPTURE,        // 信号捕获
    AC_CONTROL,     // 空调控制
    BACKUP,         // 备份数据
    RESTORE,        // 恢复数据
    FIND_NEXT,      // 查找下一协议
    LOAD_PREDEF     // 加载预定义协议
};

// 二级状态枚举
enum L2_STATE
{
    STEP1,
    STEP2,
    STEP3,
    STEP4,
    STEP5,
    STEP6,
    STEP7
};

// BC7215通信的软件串口
SoftwareSerial bc7215Serial(5, 16);                    // 接收、发送引脚
BC7215         bc7215Board(bc7215Serial, 0, 4);        // MOD=gpio0, BUSY=gpio4
BC7215AC       ac(bc7215Board);                        // 空调控制对象

// 全局变量
char                      choice;
int                       counter;
int                       idleCntr;
int                       msgCnt;
int                       extra;
int                       timeMs;
int                       interval;
bool                      initialized = false;
L1_STATE                  mainState;
L2_STATE                  l2State;
L2_STATE                  goBackState;
String                    newParam = "";
const bc7215DataVarPkt_t* dataPkt;
const bc7215FormatPkt_t*  formatPkt;
bc7215DataMaxPkt_t        sampleData[4];
bc7215FormatPkt_t         sampleFormat[4];

/*
 * 设置函数: 初始化EEPROM、串口通信和LED引脚
 */
void setup()
{
    EEPROM.begin(33 + sizeof(bc7215DataMaxPkt_t));        // 按所需大小初始化EEPROM
    Serial.begin(115200);                                 // 启动硬件串口用于调试
    bc7215Serial.begin(19200, SWSERIAL_8N2);              // 启动BC7215软件串口
    pinMode(LED, OUTPUT);                                 // 设置LED引脚为输出
    ledOff();                                             // 初始时关闭LED
    mainState = MAIN_MENU;
    l2State = STEP1;
    interval = 10;
}

/*
 * 主循环: 使用状态机处理菜单和用户选择，包括初始化、控制、保存/加载等功能
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
    //
    // 在这里放置其他任务的代码
    //
}

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
            switch (Serial.read())        // 获取主菜单选择
            {
            case '1':
                mainState = CAPTURE;
                l2State = STEP1;
                break;
            case '2':
                if (initialized)
                {
                    mainState = AC_CONTROL;
                    l2State = STEP1;
                }
                else
                {
                    Serial.println("\n空调遥控库尚未初始化，请先进行初始化");
                }
                break;
            case '3':
                if (initialized)
                {
                    mainState = BACKUP;
                    l2State = STEP1;
                }
                else
                {
                    Serial.println("\n空调遥控库尚未初始化，仅在初始化成功后才可保存数据");
                }
                break;
            case '4':
                mainState = RESTORE;
                l2State = STEP1;
                break;
            case '5':
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
            case '6':
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
 * sampleInit: 从遥控器捕获红外信号并初始化空调遥控库
 */
void captureJob()
{
    switch (l2State)
    {
    case STEP1:
        Serial.println("\n现在进行红外信号采样及空调遥控库初始化，请将空调遥控器调节至< 制冷模式，25°C "
                       ">，准备好后输入任意内容继续...");
        clearSerialBuf();
        l2State = STEP2;
        break;
    case STEP2:
        if (Serial.available())
        {
            Serial.println("现在请对准红外接收头按遥控器<风力调节>按键，接收信号后将自动转至下一步...");
            initialized = false;
            ac.startCapture();        // 开始红外信号捕获
            l2State = STEP3;
        }
        break;
    case STEP3:
        if (ac.signalCaptured())
        {
            ac.stopCapture();
            if (ac.init())
            {
                initialized = true;
                ledOn();
            	Serial.print("收到数据：");
            	dataPkt = ac.getDataPkt();
            	printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
                Serial.println("使用所接收数据初始化空调遥控库  **成功** !!! ");
                extra = ac.extraSample();
                if (extra > 0)
                {
                    if (extra > 3)
                        extra = 3;
                    Serial.print("此空调格式较特殊，需要进一步采样原遥控器信号，现在请再按遥控器上 <<");
                    Serial.println(EXTRA_KEY[extra - 1] + ">> 按键进一步采样..");
                    Serial.println("键盘输入任意内容进入采样步骤...");
                    clearSerialBuf();
                    l2State = STEP4;
                }
            }
            else
            {
                ledOff();
                Serial.println("使用所接收数据初始化空调遥控库**失败**, "
                               "可能是遥控器状态设置错误或接收解码错误，请检查遥控器设置后重新尝试");
            }
            l2State = STEP6;
        }
        break;
    case STEP4:
        if (Serial.available())
        {
            Serial.println("开始采样...");
            ac.startCapture();
            idleCntr = 0;
            msgCnt = 0;
            l2State = STEP5;
        }
        break;
    case STEP5:
        if (ac.signalCaptured((bc7215DataVarPkt_t*)&sampleData[msgCnt], &sampleFormat[msgCnt]))
        {
            if (msgCnt < 3)
            {
                msgCnt++;
            }
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
                if (idleCntr >= 200)
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
        counter++;
        if (counter & 0x10)
        {
            ledOn();        // 等待时LED闪烁
        }
        else
        {
            ledOff();
        }
        break;
    case STEP6:
        ac.stopCapture();
        Serial.println("现在请输入任意内容，程序将返回主菜单，可开始空调控制...");
        clearSerialBuf();
        l2State = STEP7;
        break;
    case STEP7:
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
 * acControl: 处理空调控制子菜单，用于设置参数、开关机
 */
void acControlJob()
{
    int comma1;
    int comma2;
    int temp;
    int mode;
    int fan;

    char inputChar;
    switch (l2State)
    {
    case STEP1:
        showCtrlMenu();
        clearSerialBuf();
        l2State = STEP2;
        break;
    case STEP2:
        if (Serial.available())
        {
            switch (Serial.read())
            {
            case '1':
                showParamMenu();
                clearSerialBuf();
                l2State = STEP3;
                break;
            case '2':
                Serial.println("发送空调开机指令");
                dataPkt = ac.on();        // 发送空调开机命令
                Serial.print("发送数据：");
                printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
                timeMs = 0;
                goBackState = STEP2;
                l2State = STEP4;
                break;
            case '3':
                Serial.println("发送空调关机指令");
                dataPkt = ac.off();        // 发送空调关机命令
                Serial.print("发送数据：");
                printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
                timeMs = 0;
                goBackState = STEP2;
                l2State = STEP4;
                break;
            case '4':
                mainState = MAIN_MENU;
                l2State = STEP1;
                break;
            default:
                break;
            }
            clearSerialBuf();
        }
        break;
    case STEP3:        // 更改参数
        if (Serial.available())
        {
            newParam = "";
            do
            {
                inputChar = Serial.read();
                if ((inputChar == '\n') || (inputChar == '\r'))
                {
                    break;
                }
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
                if (comma1 != -1 && comma2 != -1 && comma2 > comma1)
                {
                    temp = newParam.substring(0, comma1).toInt();
                    mode = newParam.substring(comma1 + 1, comma2).toInt();
                    fan = newParam.substring(comma2 + 1).toInt();
                    if (mode < 0 || mode > 4)
                    {
                        mode = 5;        // 保持模式
                    }
                    if (fan < 0 || fan > 3)
                    {
                        fan = 4;        // 保持风速
                    }
                    if (temp >= 16 && temp <= 30)
                    {
                        Serial.println("发送指令设置空调为： " + String(temp) + "°C, 模式：" + MODES[mode] + "  风力："
                            + FANSPEED[fan]);
                    }
                    else
                    {
                        Serial.println(
                            "发送指令设置空调为： 温度不变， 模式：" + MODES[mode] + "  风力：" + FANSPEED[fan]);
                    }
                    dataPkt = ac.setTo(temp, mode, fan, -1);        // 设置空调参数
                    Serial.print("发送数据：");
                    printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
                    timeMs = 0;
                    goBackState = STEP3;
                    l2State = STEP4;
                }
                else
                {
                    if (comma1 == -1)
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
                        dataPkt = ac.setTo(temp);        // 设置空调参数
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
        }
        break;
    case STEP4:        // 等待发送完成
        timeMs += interval;
        if (!ac.isBusy() || (timeMs > 3000))
        {
            if (timeMs > 3000)
            {
                Serial.println("发送超时");
            }
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

void backupJob()
{
    switch (l2State)
    {
    case STEP1:
        formatPkt = ac.getFormatPkt();
        dataPkt = ac.getDataPkt();
        EEPROM.put(0, *formatPkt);                              // 保存格式包到EEPROM
        EEPROM.put(33, *((bc7215DataMaxPkt_t*)dataPkt));        // 保存数据包到EEPROM
        EEPROM.commit();
        Serial.println("\n格式信息: ");
        printData(formatPkt, 33);
        Serial.print("数据: ");
        printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
        Serial.println("信息已保存");
        l2State = STEP2;
        break;
    case STEP2:
        if (Serial.available())
        {
            mainState = MAIN_MENU;
            l2State = STEP2;
            clearSerialBuf();
        }
        break;
    default:
        break;
    }
}

void restoreJob()
{
    switch (l2State)
    {
    case STEP1:
        EEPROM.get(0, sampleFormat[0]);        // 从EEPROM加载格式包
        EEPROM.get(33, sampleData[0]);        // 从EEPROM加载数据包
        Serial.println("\n使用");
        Serial.print("格式信息: ");
        printData(&sampleFormat[0], sizeof(bc7215FormatPkt_t));
        Serial.print("数据: ");
        printData(sampleData[0].data, (sampleData[0].bitLen + 7) / 8);
        if (ac.init(sampleData[0], sampleFormat[0]))
        {
            Serial.println("初始化空调遥控库  ***成功*** !");
            initialized = true;
        }
        else
        {
            Serial.println("初始化空调遥控库 失败...");
            initialized = false;
        }
        Serial.println("输入任意内容继续");
        l2State = STEP2;
        clearSerialBuf();
        break;
    case STEP2:
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

void findNextJob()
{
    switch (l2State)
    {
    case STEP1:
        if (ac.matchNext())
        {
            Serial.println("匹配下一个协议成功！");
        }
        else
        {
            Serial.println("无其它配批协议，空调遥控库需重新初始化");
            initialized = false;
        }
        Serial.println("输入任意内容继续...");
        clearSerialBuf();
        l2State = STEP2;
        break;
    case STEP2:
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

void loadPredefJob()
{
    int choice;
    switch (l2State)
    {
    case STEP1:
        showPredefMenu();
        clearSerialBuf();
        l2State = STEP2;
        break;
    case STEP2:
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
    case STEP3:
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
 * mainMenu: 显示主菜单
 */
void showMainMenu()
{
    Serial.println("\n\n**************************************************");
    Serial.println("*      欢迎使用 BC7215 离线万能空调控制演示程序         *");
    Serial.println("**************************************************");
    Serial.print("当前空调遥控库状态： ");
    if (initialized)
    {
        Serial.println("***已初始化***");
    }
    else
    {
        Serial.println("未初始化(须初始化后才可使用)");
    }
    Serial.println("请选择：");
    Serial.println("   1. 采样并初始化遥控库");
    Serial.println("   2. 控制空调");
    Serial.println("   3. 保存初始化数据");
    Serial.println("   4. 读取已存数据并使用该数据初始化");
    Serial.println("   5. 尝试下一匹配(如初始化成功但无法正确控制空调)");
    Serial.println("   6. 加载预定义协议");
    Serial.println("");
}

/*
 * ctrlMenu: 显示空调控制菜单
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
 * clearSerialBuf: 清除串口缓冲区中剩余的任何数据
 */
void clearSerialBuf()
{
    while (Serial.available())
    {
        Serial.read();        // 清除缓冲区
    }
}

/*
 * printData: 以十六进制格式打印二进制数据
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
 * LED控制函数: 打开或关闭LED
 */
void ledOn() { digitalWrite(LED, LOW); }
void ledOff() { digitalWrite(LED, HIGH); }
