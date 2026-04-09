/*
 * ESP32_AC_Serial_Monitor.ino
 *
 * 描述: ESP32 空调红外控制器
 * 特性: 使用 BC7215 红外模块控制空调，支持红外信号采样、
 * 参数设置、数据备份/恢复、多种控制模式等。
 * 硬件: ESP32 (TTGO T-Display), BC7215 红外模块
 * 依赖: bc7215.h, bc7215ac.h, SoftwareSerial.h, EEPROM.h
 * 作者: Bitcode
 * 日期: 2025-08-05
 */

#include <EEPROM.h>
#include <bc7215.h>
#include <bc7215ac.h>

// 引脚和常量定义
const int MOD_PIN = 27;         // 用户指定: mod=GPIO27
const int BUSY_PIN = 26;        // 用户指定: busy=GPIO26

// 界面显示字符串（已汉化）
const String MODES[] = { "自动", "制冷", "制热", "除湿", "送风", "保持", "不适用" };
const String FANSPEED[] = { "自动", "低", "中", "高", "保持", "不适用" };
const String PRESSED_KEY[] = { "温度 +", "温度 -", "模式", "风速", "保持" };
const String PWR_STATUS[] = { "关", "开", "切换", "不适用" };

// 状态机枚举
enum L1_STATE
{
	CHOOSE_UNIT,		// 选择温度制式
    MAIN_MENU,          // 主菜单
    CAPTURE,            // 信号采样
    AC_CONTROL,         // 空调控制
    BACKUP,             // 备份数据
    RESTORE,            // 恢复数据
    FIND_NEXT,          // 查找下一个协议
    LOAD_PREDEF,        // 加载预定义协议
    IR_PARSING          // 从红外信号解析温度、模式、风速
};

enum L2_STATE
{
    STEP1,
    STEP2,
    STEP3,
    STEP4,
    STEP5,
    STEP6,
    STEP7        // 二级状态步骤
};

// BC7215 通信设置
HardwareSerial bc7215Serial(1);        // BC7215 连接到 UART1
BC7215         bc7215Board(bc7215Serial, MOD_PIN, BUSY_PIN);
BC7215AC       ac(bc7215Board);        // 空调控制对象

// 全局变量
char                      choice;
unsigned long             startTime;
int                       interval;
L1_STATE                  mainState;
L2_STATE                  l2State;
L2_STATE                  goBackState;
const bc7215DataVarPkt_t* dataPkt;
const bc7215FormatPkt_t* formatPkt;
bc7215DataMaxPkt_t        irData;
bc7215FormatPkt_t         irFormat;

/*
 * Setup: 初始化 EEPROM，串口通信和 LED
 */
void setup()
{
    EEPROM.begin(sizeof(bc7215FormatPkt_t) + sizeof(bc7215DataMaxPkt_t) + sizeof(bool));  // 初始化 EEPROM
    Serial.begin(115200);                           // 调试串口
    bc7215Serial.begin(19200, SERIAL_8N2, 25, 33);  // BC7215 串口, RX=GPIO25, TX=GPIO33
    mainState = CHOOSE_UNIT;
    l2State = STEP1;
    interval = 10;
    delay(100);
	bc7215Board.setRx();		// 如果BC7215A处于关机状态，切换为接收模式会将其唤醒
	delay(50);
    bc7215Board.setTx();        // 转换为发射模式
    delay(50);
	Serial.println("");
	Serial.print("系统初始化完成，");
}

/*
 * 主循环: 处理各个功能模块的状态机
 */
void loop()
{
    switch (mainState)
    {
    case CHOOSE_UNIT:
    	chooseUnitJob();
    	break;
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
    case IR_PARSING:
        irParsingJob();
        break;
    default:
        break;
    }
    delay(interval);
    // 可以在此处添加额外的任务代码
}

/*
 * 制式菜单处理程序: 显示菜单并处理用户选择
 */
void chooseUnitJob(void)
{
    switch (l2State)
    {
    case STEP1:
    	Show_Unit_Menu();
        clearSerialBuf();
        l2State = STEP2;
        break;
    case STEP2:
        if (Serial.available())
        {        // 如果有输入
            switch (Serial.read())
            {
            case '1':        // 摄氏
				ac.setCelsius();
            	Serial.println("空调设置为摄氏");
            	l2State = STEP3;
            	break;
            case '2':		// 华氏
				ac.setFahrenheit();
            	Serial.println("空调设置为华氏");
            	l2State = STEP3;
            	break;
            default:
				l2State = STEP1;
            	break;
            }
        }
        break;
    case STEP3:
    	mainState = MAIN_MENU;
    	l2State = STEP1;
    	break;
    default:
    	break;
    }
}

/*
 * 主菜单处理程序: 显示菜单并处理用户选择
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
            case '1':        // 采样和配对
                mainState = CAPTURE;
                l2State = STEP1;
                break;
            case '2':        // 控制空调
                if (ac.initOK)
                {
                    mainState = AC_CONTROL;
                    l2State = STEP1;
                }
                else
                {
                    Serial.println("\n空调控制库尚未初始化，请先进行配对");
                }
                break;
            case '3':        // 保存数据
                if (ac.initOK)
                {
                    mainState = BACKUP;
                    l2State = STEP1;
                }
                else
                {
                    Serial.println(
                        "\n空调控制库尚未初始化，只有在配对成功后才能保存数据。");
                }
                break;
            case '4':        // 恢复数据
                mainState = RESTORE;
                l2State = STEP1;
                break;
            case '5':        // 查找下一个协议
                if (ac.initOK)
                {
                    mainState = FIND_NEXT;
                    l2State = STEP1;
                }
                else
                {
                    Serial.println("\n此功能仅在配对后可用");
                }
                break;
            case '6':        // 加载预定义
                mainState = LOAD_PREDEF;
                l2State = STEP1;
                break;
            case '7':        // 解析
                mainState = IR_PARSING;
                l2State = STEP1;
                break;
			case '8':		// 转换制式
				mainState = CHOOSE_UNIT,		// 选择温度制式
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
 * 信号捕获处理程序: 捕获遥控器红外信号并初始化空调控制库
 */
void captureJob()
{
    switch (l2State)
    {
    case STEP1:        // 提示用户准备
		if (ac.isCelsius())
		{
        	Serial.println("\n现在进行红外空调配对。 "
        	               "\n请将空调遥控器设置为 <制冷模式, 25°C>，然后按任意键继续...");
		}
		else
		{
        	Serial.println("\n现在进行红外空调配对。 "
        	               "\n请将空调遥控器设置为 <制冷模式, 78°F>，然后按任意键继续...");
		}
        clearSerialBuf();
        l2State = STEP2;
        break;
    case STEP2:        // 等待用户确认
        if (Serial.available())
        {
            Serial.println("现在请对准红外接收头按下遥控器上的 <风速> 按钮。\n"
                           "收到信号后将自动进行下一步...");
            ac.startCapture();
            l2State = STEP3;
        }
        break;
    case STEP3:        // 处理采样结果
        if (ac.signalCaptured())
        {
            ac.stopCapture();
            if (ac.init())        // 尝试初始化
            {
                Serial.print("接收到的数据: ");
                dataPkt = ac.getDataPkt();
                printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
                Serial.println("使用接收到的数据初始化空调控制库 **成功** !!! ");
            }
            else        // 初始化失败
            {
                Serial.println("使用接收到的数据初始化空调控制库 **失败**，"
                               "\n可能是由于遥控器状态设置不正确或接收解码错误。请"
                               "检查遥控器设置并重试");
            }
            l2State = STEP4;
        }
        break;
    case STEP4:        // 采样完成提示
        Serial.println("现在请输入任意内容，程序将返回主菜单，即可开始控制空调...");
        clearSerialBuf();
        l2State = STEP5;
        break;
    case STEP5:        // 等待用户确认返回
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
 * 空调控制处理程序: 设置参数，开关机操作
 */
void acControlJob()
{
    int  comma1, comma2, temp, mode, fan;
    char inputChar;

    switch (l2State)
    {
    case STEP1:        // 显示控制菜单
        showCtrlMenu();
        clearSerialBuf();
        l2State = STEP2;
        break;
    case STEP2:        // 处理控制选择
        if (Serial.available())
        {
            switch (Serial.read())
            {
            case '1':        // 设置参数
                showParamMenu();
                clearSerialBuf();
                l2State = STEP3;
                break;
            case '2':        // 开机
                Serial.println("正在发送开启空调指令");
                startTime = millis();
                dataPkt = ac.on();
                Serial.print("发送数据: ");
                printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
                goBackState = STEP2;
                l2State = STEP4;
                break;
            case '3':        // 关机
                Serial.println("正在发送关闭空调指令");
                startTime = millis();
                dataPkt = ac.off();
                Serial.print("发送数据: ");
                printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
                goBackState = STEP2;
                l2State = STEP4;
                break;
            case '4':        // 返回主菜单
                mainState = MAIN_MENU;
                l2State = STEP1;
                break;
            default:
                break;
            }
            clearSerialBuf();
        }
        break;
    case STEP3:        // 参数设置处理
        if (Serial.available())
        {
            String inputString = Serial.readStringUntil('\n');
            inputString.trim();

            if (inputString.length() > 0)
            {

                if (inputString.equalsIgnoreCase("exit"))
                {
                    Serial.println("退出");
                    l2State = STEP1;
                }
                else
                {
                    int t = -1;
                    int m = -1;
                    int f = -1;
                    int k = -1;
                    // 尝试从输入中获取温度、模式、风速和按键
                    sscanf(inputString.c_str(), "%d,%d,%d,%d", &t, &m, &f, &k);
                    if (m < 0 || m > 4)
                        m = 5;
                    // 5 = 保持
                    if (f < 0 || f > 3)
                        f = 4;
                    // 4 = 保持
                    if (k < 0 || k > 3)
                        k = 4;
                    // 4 = 保持

                    Serial.print("发送指令设置空调为: ");
                    if (ac.isCelsius() && (t >= 16 && t <= 30))
                    {
                        Serial.print(t);
                        Serial.print("°C, 模式: ");
                    }
					else if (!ac.isCelsius() && (t >= 60 && t <= 88))
					{
                        Serial.print(t);
                        Serial.print("°F, 模式: ");
					}
                    else
                    {
                        Serial.print("(保持温度), 模式: ");
                    }
                    Serial.print(MODES[m]);
                    Serial.print(", 风速: ");
                    Serial.print(FANSPEED[f]);
                    Serial.print(", 按键: ");
                    Serial.println(PRESSED_KEY[k]);

                    startTime = millis();
                    dataPkt = ac.setTo(t, m, f, k);
                    Serial.print(F("发送数据: "));

                    if (dataPkt->bitLen == 0)
                    {
                        dataPkt = ((bc7215CombinedMsg_t*)dataPkt)->body.msg.datPkt;
                    }
                    printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
                    goBackState = STEP3;
                    l2State = STEP4;
                }
            }
        }
        break;
    case STEP4:        // 等待传输完成
        if (!ac.isBusy() || (millis() - startTime > 3000))
        {
            if (millis() - startTime > 3000)
                Serial.println("传输超时");
            Serial.println("传输完成!");
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
 * 数据备份: 将初始化数据保存到 Flash
 */
void backupJob()
{
	bool	isCelsius;

	isCelsius = ac.isCelsius();
    switch (l2State)
    {
    case STEP1:
        if (ac.initOK)
        {
            formatPkt = ac.getFormatPkt();
            dataPkt = ac.getDataPkt();
            EEPROM.put(0, *formatPkt);                              // 保存数据到 EEPROM
            EEPROM.put(sizeof(bc7215FormatPkt_t), *((bc7215DataMaxPkt_t*)dataPkt));        // 保存格式到 EEPROM
			EEPROM.put(sizeof(bc7215FormatPkt_t)+sizeof(bc7215DataMaxPkt_t), isCelsius);	// 保存制式到 EEPROM
            EEPROM.commit();
            Serial.println("\n格式信息: ");
            printData(formatPkt, sizeof(bc7215FormatPkt_t));
            Serial.print("数据: ");
            printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
            Serial.println("信息已保存到 Flash 存储器");
        }
        else
        {
            Serial.println("\n此功能仅在配对后可用");
        }
        l2State = STEP2;
        break;
    case STEP2:        // 等待用户确认
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
 * 数据恢复: 从 Flash 读取数据并初始化
 */
void restoreJob()
{
	bool	isCelsius;

    switch (l2State)
    {
    case STEP1:
        EEPROM.get(0, irFormat);         // 从 EEPROM 加载格式
        EEPROM.get(sizeof(bc7215FormatPkt_t), irData);          // 从 EEPROM 加载数据
		EEPROM.get(sizeof(bc7215FormatPkt_t)+sizeof(bc7215DataMaxPkt_t), isCelsius);	// 从 EEPROM 加载制式
        Serial.println("\n使用 Flash 中保存的配置");
        Serial.print("格式信息: ");
        printData(&irFormat, sizeof(bc7215FormatPkt_t));
        Serial.print("数据: ");
        printData(&irData.data, (irData.bitLen + 7) / 8);
		if (isCelsius)
		{
			ac.setCelsius();
		}
		else
		{
			ac.setFahrenheit();
		}
        if (ac.init(irData, irFormat))
        {
            Serial.println("空调控制库初始化  ***成功*** !");
        }
        else
        {
            Serial.println("空调控制库初始化失败...");
        }
        Serial.println("输入任意内容继续");
        l2State = STEP2;
        clearSerialBuf();
        break;
    case STEP2:        // 等待用户确认
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
 * 查找下一个协议: 尝试匹配其他协议格式
 */
void findNextJob()
{
    switch (l2State)
    {
    case STEP1:
        if (ac.matchNext())
        {
            Serial.println("下一个协议匹配成功！");
        }
        else
        {
            Serial.println("没有其他匹配的协议，空调控制库需要重新初始化");
        }
        Serial.println("输入任意内容继续...");
        clearSerialBuf();
        l2State = STEP2;
        break;
    case STEP2:        // 等待用户确认
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
 * 加载预定义: 使用内置的协议数据
 */
void loadPredefJob()
{
    int choice;
    switch (l2State)
    {
    case STEP1:        // 显示预定义菜单
        showPredefMenu();
        clearSerialBuf();
        l2State = STEP2;
        break;
    case STEP2:        // 处理用户选择
        if (Serial.available())
        {
            choice = Serial.parseInt();
            Serial.println("已选择选项 " + String(choice));
            if (ac.initPredef(choice))
            {
                Serial.print("格式: ");
                printData(ac.getFormatPkt(), sizeof(bc7215FormatPkt_t));
                Serial.print("数据: ");
                printData(ac.getDataPkt(), (ac.getDataPkt()->bitLen + 7) / 8 + 2);
                Serial.println("初始化成功 !!! 按任意键继续");
            }
            else
            {
                Serial.print("格式: ");
                printData(&ac.sampleFormat[0], sizeof(bc7215FormatPkt_t));
                Serial.print("数据: ");
                printData(&ac.sampleData[0], BC7215_MAX_RX_DATA_SIZE);
                Serial.println("初始化失败.... 输入任意内容继续");
            }
            clearSerialBuf();
            l2State = STEP3;
        }
        break;
    case STEP3:        // 等待用户确认
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
 * 解析红外信号: 从红外读取温度、模式、风速和电源状态
 */
void irParsingJob()
{
    int T, M, F, P;
    switch (l2State)
    {
    case STEP1:
        if (ac.initOK)
        {
            Serial.println(
                "\n\n正在接收红外信号并解析（解码）其中的温度、模式、风速和电源状态。");
            Serial.println("\nBC7215A 现处于接收模式，准备解码。键盘输入任意内容退出");
            ac.startCapture();
            clearSerialBuf();
            l2State = STEP2;
        }
        else
        {
            Serial.println("\n\n此功能仅在与空调配对后可用");
            Serial.println("请先进行配对。");
            mainState = MAIN_MENU;
            l2State = STEP1;
        }
        break;
    case STEP2:        // 等待红外信号
        if (ac.signalCaptured())
        {
            ac.stopCapture();
            T = -1;
            M = -1;
            F = -1;
            P = -1;
            if (ac.parse(T, M, F, P))
            {
				if (ac.isCelsius())
				{
                	if ((T < 16) || (T > 30))
                	    T = -1;
				}
				else
				{
                	if ((T < 60) || (T > 88))
                	    T = -1;
				}
                if ((M < 0) || (M > 4))
                    M = 6;
                if ((F < 0) || (F > 3))
                    F = 5;
                if ((P < 0) || (P > 2))
                    P = 3;
                Serial.println("解析结果: 温度: " + String(T) + "°C,  模式: " + MODES[M]
                    + ",  风速: " + FANSPEED[F] + ",  电源: " + PWR_STATUS[P]);
            }
            else
            {
                Serial.println("解析失败");
            }
            ac.startCapture();
        }
        if (Serial.available())
        {
            ac.stopCapture();
            mainState = MAIN_MENU;
            l2State = STEP1;
        }
        break;
    default:
        break;
    }
}

/*
 * 显示温度制式菜单 
 */
void Show_Unit_Menu(void)
{
    Serial.println("请选择空调的温度制式：");
    Serial.println("   1. °C 摄氏");
    Serial.println("   2. °F 华氏");
    Serial.println("");
}

/*
 * 显示主菜单
 */
void showMainMenu()
{
    Serial.println("\n\n*****************************************************");
    Serial.println("* 欢迎使用 BC7215A 通用空调控制器演示程序        *");
    Serial.println("*****************************************************");
    Serial.print("AC 库版本: ");
    Serial.println(ac.getLibVer());
    Serial.print("当前空调控制库状态: ");
    if (ac.initOK)
        Serial.println("***已初始化***");
    else
        Serial.println("未初始化 (使用前必须先与空调配对)");
	if (ac.isCelsius())
		Serial.println("温度制式：摄氏");
	else
		Serial.println("温度制式：华氏");
    Serial.println("请选择:");
    Serial.println("   1. 与空调配对");
    Serial.println("   2. 控制空调");
    Serial.println("   3. 保存配对数据");
    Serial.println("   4. 读取已保存数据并进行配对");
    Serial.println("   5. 尝试下一个匹配 (如果配对成功但无法正常控制空调)");
    Serial.println("   6. 加载预定义协议");
    Serial.println("   7. 解析红外信号");
	Serial.println("   8. 设置系统温度制式");
    Serial.println("");
}

/*
 * 显示控制菜单
 */
void showCtrlMenu()
{
    Serial.println("\n空调控制，请选择:");
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
	if (ac.isCelsius())
	{
    	Serial.println(" *** 空调参数调整 *** "
    	               "\n格式: '温度, 模式, 风速, 按键' 用逗号 ',' 分隔, 例如: 24, 1, 2, 0");
    	Serial.println("允许减少参数，例如 '18, 2' 意味着设置为 '18°C 制热模式'");
    	Serial.println("风速和按键保持不变。");
    	Serial.println("温度(°C)         模式            风速             按键");
    	Serial.println(" 范围: 16~30       0 - 自动         0 - 自动         0 - 温度 +");
	}
	else
	{
    	Serial.println(" *** 空调参数调整 *** "
    	               "\n格式: '温度, 模式, 风速, 按键' 用逗号 ',' 分隔, 例如: 75, 1, 2, 0");
    	Serial.println("允许减少参数，例如 '72, 2' 意味着设置为 '72°F 制热模式'");
    	Serial.println("风速和按键保持不变。");
    	Serial.println("温度(°F)         模式            风速             按键");
    	Serial.println(" 范围: 60-88       0 - 自动         0 - 自动         0 - 温度 +");
	}
    Serial.println("                   1 - 制冷         1 - 低           1 - 温度 -");
    Serial.println("                   2 - 制热         2 - 中           2 - 模式");
    Serial.println("                   3 - 除湿         3 - 高           3 - 风速");
    Serial.println("                   4 - 送风\r\n");
    Serial.println(" * 超出上述范围的值表示保持该项当前状态");
    Serial.println("------------------------------------------------------------------------------------");
    Serial.println("(注意: 受限于被控空调支持的设置。)");
    Serial.println("现在请输入空调参数值: (输入 'exit' 返回上级菜单)");
    Serial.println("");
}

/*
 * 显示预定义协议菜单
 */
void showPredefMenu()
{
    Serial.println("");
    Serial.println("有少数协议 BC7215A 芯片不支持直接解码。");
    Serial.println("当直接采样失败时，尝试使用预定义数据来控制空调。");
    Serial.println("请选择:");
    for (int i = 0; i < ac.cntPredef(); i++)
    {
        Serial.println("  " + String(i) + ". " + ac.getPredefName(i));
    }
    Serial.println("");
}

/*
 * 清空串口缓冲区
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

