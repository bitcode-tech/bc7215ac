/*
 * ESP32_AC_Serial_Monitor.ino
 *
 * Description: ESP32 Air Conditioner Infrared Controller
 * Features: Control air conditioners using BC7215 IR module, supports IR signal sampling,
 *           parameter setting, data backup/restore, multiple control modes, etc.
 * Hardware: ESP32 (TTGO T-Display), BC7215 IR module
 * Dependencies: bc7215.h, bc7215ac.h, SoftwareSerial.h, EEPROM.h
 * Author: Bitcode
 * Date: 2025-08-05
 */

#include <EEPROM.h>
#include <bc7215.h>
#include <bc7215ac.h>

// Pin and constant definitions
const int MOD_PIN = 27;         // User specified: mod=GPIO27
const int BUSY_PIN = 26;        // User specified: busy=GPIO26

const String MODES[] = { "Auto", "Cool", "Heat", "Dry", "Fan", "Keep", "n/a" };
const String FANSPEED[] = { "Auto", "Low", "Med", "High", "Keep", "n/a" };
const String PRESSED_KEY[] = { "Temp +", "Temp -", "Mode", "Fan Speed", "Keep" };
const String EXTRA_KEY[] = { "Temp +/-", "Mode", "Fan Control" };
const String PWR_STATUS[] = { "OFF", "ON", "TOG", "n/a" };

// State machine enumerations
enum L1_STATE
{
    MAIN_MENU,          // Main menu
    CAPTURE,            // Signal sampling
    AC_CONTROL,         // AC control
    BACKUP,             // Backup data
    RESTORE,            // Restore data
    FIND_NEXT,          // Find next protocol
    LOAD_PREDEF,        // Load predefined
    IR_PARSING          // Read Temp. Mode, Fan Speed from IR
};
enum L2_STATE
{
    STEP1,
    STEP2,
    STEP3,
    STEP4,
    STEP5,
    STEP6,
    STEP7        // Secondary state steps
};

// BC7215 communication setup
HardwareSerial bc7215Serial(1);        // BC7215 connects to UART1
BC7215         bc7215Board(bc7215Serial, MOD_PIN, BUSY_PIN);
BC7215AC       ac(bc7215Board);        // AC control object

// Global variables
char                      choice;
int                       extra;
unsigned long             startTime;
int                       interval;
L1_STATE                  mainState;
L2_STATE                  l2State;
L2_STATE                  goBackState;
const bc7215DataVarPkt_t* dataPkt;
const bc7215FormatPkt_t*  formatPkt;
bc7215DataMaxPkt_t        irData;
bc7215FormatPkt_t         irFormat;

/*
 * Setup: Initialize EEPROM, serial communication and LED
 */
void setup()
{
    EEPROM.begin(33 + sizeof(bc7215DataMaxPkt_t));        // Initialize EEPROM
    Serial.begin(115200);                                 // Debug serial
    bc7215Serial.begin(19200, SERIAL_8N2, 25, 33);        // BC7215 serial, RX=GPIO25, TX=GPIO33
    mainState = MAIN_MENU;
    l2State = STEP1;
    interval = 10;
    delay(100);
}

/*
 * Main loop: State machine handling various function modules
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
    case IR_PARSING:
        irParsingJob();
        break;
    default:
        break;
    }
    delay(interval);
    // Additional task code can be added here
}

/*
 * Main menu handler: Display menu and process user selections
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
            case '1':        // Sample and pairing
                mainState = CAPTURE;
                l2State = STEP1;
                break;
            case '2':        // Control AC
                if (ac.initOK)
                {
                    mainState = AC_CONTROL;
                    l2State = STEP1;
                }
                else
                {
                    Serial.println("\nAC control library not initialized yet, please pair first");
                }
                break;
            case '3':        // Save data
                if (ac.initOK)
                {
                    mainState = BACKUP;
                    l2State = STEP1;
                }
                else
                {
                    Serial.println(
                        "\nAC control library not initialized yet, data can only be saved after successful pairing.");
                }
                break;
            case '4':        // Restore data
                mainState = RESTORE;
                l2State = STEP1;
                break;
            case '5':        // Find next protocol
                if (ac.initOK)
                {
                    mainState = FIND_NEXT;
                    l2State = STEP1;
                }
                else
                {
                    Serial.println("\nThis function is only available after pairing");
                }
                break;
            case '6':        // Load predefined
                mainState = LOAD_PREDEF;
                l2State = STEP1;
                break;
            case '7':        // Parsing
                mainState = IR_PARSING;
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
 * Signal capture handler: Capture remote IR signals and initialize AC control library
 */
void captureJob()
{
    switch (l2State)
    {
    case STEP1:        // Prompt user to prepare
        Serial.println("\nNow performing IR AC pairing. "
                       "\nPlease set AC remote to < Cooling mode, 25°C(77°F)>, then press any key to continue...");
        clearSerialBuf();
        l2State = STEP2;
        break;
    case STEP2:        // Wait for user confirmation
        if (Serial.available())
        {
            Serial.println("Now please aim at IR receiver and press <Fan Control> button on remote. \nWill "
                           "automatically proceed to next step after receiving signal...");
            ac.startCapture();
            l2State = STEP3;
        }
        break;
    case STEP3:        // Process first sampling result
        if (ac.signalCaptured())
        {
            ac.stopCapture();
            if (ac.init())        // Try to initialize
            {
                Serial.print("Received data: ");
                dataPkt = ac.getDataPkt();
                printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
                Serial.println("AC control library initialization using received data  **SUCCESS** !!! ");
                extra = ac.extraSample();
                if (extra > 0)        // Need extra sampling
                {
                    if (extra > 3)
                    {
                        break;
                    }
                    Serial.print("This AC format is special, need further sampling of original remote signals. \nNow "
                                 "please press <<");
                    Serial.println(EXTRA_KEY[extra - 1] + ">> button on remote for additional sampling..");
                    Serial.println("Press Enter to start sampling step...");
                    clearSerialBuf();
                    l2State = STEP4;
                    break;
                }
            }
            else        // Initialization failed
            {
                Serial.println("AC control library initialization using received data **FAILED**, "
                               "\npossibly due to incorrect remote state settings or receiving decode errors. Please "
                               "check remote settings and try again");
            }
            l2State = STEP6;
        }
        break;
    case STEP4:        // Prepare for extra sampling
        if (Serial.available())
        {
            Serial.println("Starting sampling...");
            ac.startCapture();
            l2State = STEP5;
        }
        break;
    case STEP5:        // Extra sampling processing
        if (ac.signalCaptured())
        {
            ac.stopCapture();
            ac.saveExtra();
            l2State = STEP6;
        }
        break;
    case STEP6:        // Sampling completion prompt
        Serial.println("Now please enter any content, program will return to main menu and AC control can begin...");
        clearSerialBuf();
        l2State = STEP7;
        break;
    case STEP7:        // Wait for user confirmation to return
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
 * AC control handler: Set parameters, power on/off operations
 */
void acControlJob()
{
    int  comma1, comma2, temp, mode, fan;
    char inputChar;

    switch (l2State)
    {
    case STEP1:        // Show control menu
        showCtrlMenu();
        clearSerialBuf();
        l2State = STEP2;
        break;
    case STEP2:        // Handle control selections
        if (Serial.available())
        {
            switch (Serial.read())
            {
            case '1':        // Set parameters
                showParamMenu();
                clearSerialBuf();
                l2State = STEP3;
                break;
            case '2':        // Power on
                Serial.println("Sending AC power on command");
                startTime = millis();
                dataPkt = ac.on();
                Serial.print("Sending data: ");
                printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
                goBackState = STEP2;
                l2State = STEP4;
                break;
            case '3':        // Power off
                Serial.println("Sending AC power off command");
                startTime = millis();
                dataPkt = ac.off();
                Serial.print("Sending data: ");
                printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
                goBackState = STEP2;
                l2State = STEP4;
                break;
            case '4':        // Return to main menu
                mainState = MAIN_MENU;
                l2State = STEP1;
                break;
            default:
                break;
            }
            clearSerialBuf();
        }
        break;
    case STEP3:        // Parameter setting processing
        if (Serial.available())
        {
            String inputString = Serial.readStringUntil('\n');
            inputString.trim();

            if (inputString.length() > 0)
            {

                if (inputString.equalsIgnoreCase("exit"))
                {
                    Serial.println("Exit");
                    l2State = STEP1;
                }
                else
                {
                    int t = -1;
                    int m = -1;
                    int f = -1;
                    int k = -1;

                    // try to get temp, mode,fan and key from input
                    sscanf(inputString.c_str(), "%d,%d,%d,%d", &t, &m, &f, &k);

                    if (m < 0 || m > 4)
                        m = 5;        // 5 = Keep
                    if (f < 0 || f > 3)
                        f = 4;        // 4 = Keep
                    if (k < 0 || k > 3)
                        k = 4;        // 4 = Keep

                    Serial.print("Sending command to set AC to: ");
                    if (t >= 16 && t <= 30)
                    {
                        Serial.print(t);
                        Serial.print("°C, Mode: ");
                    }
                    else
                    {
                        Serial.print("(keep Temp), Mode: ");
                    }
                    Serial.print(MODES[m]);
                    Serial.print(", Fan Speed: ");
                    Serial.print(FANSPEED[f]);
                    Serial.print(", Key Press: ");
                    Serial.println(PRESSED_KEY[k]);

                    startTime = millis();
                    dataPkt = ac.setTo(t, m, f, k);

                    Serial.print(F("Sending data: "));

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
    case STEP4:        // Wait for transmission completion
        if (!ac.isBusy() || (millis() - startTime > 3000))
        {
            if (millis() - startTime > 3000)
                Serial.println("Transmission timeout");
            Serial.println("Transmission complete!");
            Serial.println("");
            Serial.println("Please continue input");
            clearSerialBuf();
            l2State = goBackState;
        }
        break;
    default:
        break;
    }
}

/*
 * Data backup: Save initialization data to Flash
 */
void backupJob()
{
    switch (l2State)
    {
    case STEP1:
        if (ac.initOK)
        {
            formatPkt = ac.getFormatPkt();
            dataPkt = ac.getDataPkt();
            EEPROM.put(0, *formatPkt);                              // save data to EEPROM
            EEPROM.put(33, *((bc7215DataMaxPkt_t*)dataPkt));        // save format to EEPROM
            EEPROM.commit();
            Serial.println("\nFormat info: ");
            printData(formatPkt, 33);
            Serial.print("Data: ");
            printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
            Serial.println("Information saved to Flash memory");
        }
        else
        {
            Serial.println("\nThis function is only available after pairing");
        }
        l2State = STEP2;
        break;
    case STEP2:        // Wait for user confirmation
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
 * Data restore: Read data from Flash and initialize
 */
void restoreJob()
{
    switch (l2State)
    {
    case STEP1:
        EEPROM.get(0, irFormat);        // Load format from EEPROM
        EEPROM.get(33, irData);         // Load data from EEPROM
        Serial.println("\nUsing saved configuration from Flash");
        Serial.print("Format info: ");
        printData(&irFormat, sizeof(bc7215FormatPkt_t));
        Serial.print("Data: ");
        printData(&irData.data, (irData.bitLen + 7) / 8);

        if (ac.init(irData, irFormat))
        {
            Serial.println("AC control library initialization  ***SUCCESS*** !");
        }
        else
        {
            Serial.println("AC control library initialization failed...");
        }
        Serial.println("Enter any content to continue");
        l2State = STEP2;
        clearSerialBuf();
        break;
    case STEP2:        // Wait for user confirmation
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
 * Find next protocol: Try to match other protocol formats
 */
void findNextJob()
{
    switch (l2State)
    {
    case STEP1:
        if (ac.matchNext())
        {
            Serial.println("Next protocol match successful!");
        }
        else
        {
            Serial.println("No other matching protocols, AC control library needs re-initialization");
        }
        Serial.println("Enter any content to continue...");
        clearSerialBuf();
        l2State = STEP2;
        break;
    case STEP2:        // Wait for user confirmation
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
 * Load predefined: Use built-in protocol data
 */
void loadPredefJob()
{
    int choice;
    switch (l2State)
    {
    case STEP1:        // Show predefined menu
        showPredefMenu();
        clearSerialBuf();
        l2State = STEP2;
        break;
    case STEP2:        // Handle user selection
        if (Serial.available())
        {
            choice = Serial.parseInt();
            Serial.println("Selected option " + String(choice));
            if (ac.initPredef(choice))
            {
                Serial.print("Format: ");
                printData(ac.getFormatPkt(), 33);
                Serial.print("Data: ");
                printData(ac.getDataPkt(), (ac.getDataPkt()->bitLen + 7) / 8 + 2);
                Serial.println("Initialization successful !!! Press any key to continue");
            }
            else
            {
                Serial.print("Format: ");
                printData(&ac.sampleFormat[0], 33);
                Serial.print("Data: ");
                printData(&ac.sampleData[0], BC7215_MAX_RX_DATA_SIZE);
                Serial.println("Initialization failed.... Enter any content to continue");
            }
            clearSerialBuf();
            l2State = STEP3;
        }
        break;
    case STEP3:        // Wait for user confirmation
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
 * Parse IR signal: Read Temperature, Mode, Fan Speed and Power Status from IR
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
                "\n\nReceiving IR signal and parse(decode) Temperature, Mode, Fan Speed, and Power status from it.");
            Serial.println("\nBC7215A is now in RX mode, ready to decode. Enter anything on keyboard to exit");
            ac.startCapture();
            clearSerialBuf();
            l2State = STEP2;
        }
        else
        {
            Serial.println("\n\nThis function is only available when it's paired with AC");
            Serial.println("Please pair first.");
            mainState = MAIN_MENU;
            l2State = STEP1;
        }
        break;
    case STEP2:        // Wait for IR signal
        if (ac.signalCaptured())
        {
            ac.stopCapture();
            T = -1;
            M = -1;
            F = -1;
            P = -1;
            if (ac.parse(T, M, F, P))
            {
                if ((T < 16) || (T > 30))
                    T = -1;
                if ((M < 0) || (M > 4))
                    M = 6;
                if ((F < 0) || (F > 3))
                    F = 5;
                if ((P < 0) || (P > 2))
                    P = 3;
                Serial.println("Parsing Result: Temp: " + String(T) + "°C,  Mode: " + MODES[M]
                    + ",  Fan Speed: " + FANSPEED[F] + ",  Power: " + PWR_STATUS[P]);
            }
            else
            {
                Serial.println("Parsing failed");
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
 * Display main menu
 */
void showMainMenu()
{
    Serial.println("\n\n*****************************************************");
    Serial.println("* Welcome to BC7215A Universal AC Controller Demo  *");
    Serial.println("*****************************************************");
    Serial.print("AC Library Version: ");
    Serial.println(ac.getLibVer());
    Serial.print("Current AC control library status: ");
    if (ac.initOK)
        Serial.println("***INITIALIZED***");
    else
        Serial.println("Not initialized (must be paired with AC before use)");
    Serial.println("Please select:");
    Serial.println("   1. Pairing with AC");
    Serial.println("   2. Control air conditioner");
    Serial.println("   3. Save pairing data");
    Serial.println("   4. Read saved data and pair with it");
    Serial.println("   5. Try next match (if paired successfully but cannot control AC properly)");
    Serial.println("   6. Load predefined protocol");
    Serial.println("   7. Parse IR signal");
    Serial.println("");
}

/*
 * Display control menu
 */
void showCtrlMenu()
{
    Serial.println("\nAC Control, please select:");
    Serial.println("   1. Set AC parameters");
    Serial.println("   2. Power on");
    Serial.println("   3. Power off");
    Serial.println("   4. Return to upper menu");
    Serial.println("");
}

/*
 * Display parameter setting menu
 */
void showParamMenu()
{
    Serial.println(" *** AC parameter adjustment *** "
                   "\nFormat: 'temperature, mode, fan level, pressed-key' separated by commas ',', e.g.: 24, 1, 2, 0");
    Serial.println("Fewer parameter is allowed, for example '18, 2' means set to '18°C Heating");
    Serial.println("Fan Speed and Pressed Key unchanged.");
    Serial.println("Temperature(°C)    Mode           Fan              Key");
    Serial.println(" Range: 16~30       0 - Auto       0 - Auto         0 - Temp +");
    Serial.println("                    1 - Cool       1 - Low          1 - Temp -");
    Serial.println("                    2 - Heat       2 - Med          2 - Mode");
    Serial.println("                    3 - Dry        3 - High         3 - Fan");
    Serial.println("                    4 - Fan\r\n");
    Serial.println(" * Values outside above ranges indicate maintaining current state for that item");
    Serial.println("------------------------------------------------------------------------------------");
    Serial.println("(Note: Limited to settings supported by the controlled AC.)");
    Serial.println("Now please enter AC parameter values: (enter 'exit' to return to upper menu)");
    Serial.println("");
}

/*
 * Display predefined protocol menu
 */
void showPredefMenu()
{
    Serial.println("");
    Serial.println("A few protocols are not supported for direct decoding by BC7215A chip.");
    Serial.println("When direct sampling fails, try using predefined data to control AC.");
    Serial.println("Please select:");
    for (int i = 0; i < ac.cntPredef(); i++)
    {
        Serial.println("  " + String(i) + ". " + ac.getPredefName(i));
    }
    Serial.println("");
}

/*
 * Clear serial buffer
 */
void clearSerialBuf()
{
    while (Serial.available())
    {
        Serial.read();
    }
}

/*
 * Print data in hexadecimal format
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

