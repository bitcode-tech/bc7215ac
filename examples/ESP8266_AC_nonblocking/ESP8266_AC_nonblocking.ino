/*
 * ESP8266_AC_CTRL_nonblocking.ino
 *
 * Description: This program enables control of an air conditioner using an ESP8266 microcontroller
 * with a BC7215 IR module. It supports capturing IR signals from an AC remote, initializing the control
 * library, saving/loading settings to/from EEPROM, and controlling AC parameters (temperature, mode, fan speed).
 * The program provides a serial interface for user interaction to initialize, control, and manage AC settings.
 *
 * Hardware: ESP8266 (NodeMCU), BC7215 IR module
 * Dependencies: bc7215.h, bc7215ac.h, SoftwareSerial.h, EEPROM.h
 * Author: Bitcode
 * Date: 2025-08-05
 */

#include <EEPROM.h>
#include <SoftwareSerial.h>
#include <bc7215.h>
#include <bc7215ac.h>

// Pin and constant definitions
const int    LED = 2;                                                             // NodeMCU onboard LED pin
const String MODES[] = { "Auto", "Cool", "Heat", "Dry", "Fan", "Keep" };        // AC modes
const String FANSPEED[] = { "Auto", "Low", "Medium", "High", "Keep" };                   // Fan speeds
const String EXTRA_KEY[] = { "Temperature +/-", "Mode", "Fan Speed" };                  // Extra keys for sampling
enum L1_STATE
{
    MAIN_MENU,      // Main menu
    CAPTURE,        // Signal sampling
    AC_CONTROL,     // AC control
    BACKUP,         // Backup data
    RESTORE,        // Restore data
    FIND_NEXT,      // Find next protocol
    LOAD_PREDEF     // Load predefined
};
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

// Software serial for BC7215 communication
SoftwareSerial bc7215Serial(5, 16);                    // Rx, Tx pins
BC7215         bc7215Board(bc7215Serial, 0, 4);        // mod=gpio0, busy=gpio4
BC7215AC       ac(bc7215Board);                        // AC control object

// Global variables
char                      choice;           // User choice
int                       counter;          // Counter
int                       idleCntr;         // Idle counter
int                       msgCnt;           // Message count
int                       extra;            // Extra sampling flag
int                       timeMs;           // Time counter
int                       interval;         // Loop interval
bool                      initialized = false;  // Initialization status
L1_STATE                  mainState;        // Main state
L2_STATE                  l2State;          // Secondary state
L2_STATE                  goBackState;      // Return state
String                    newParam = "";    // New parameter string
const bc7215DataVarPkt_t* dataPkt;         // Data packet pointer
const bc7215FormatPkt_t*  formatPkt;       // Format packet pointer
bc7215DataMaxPkt_t        sampleData[4];   // Sample data buffer
bc7215FormatPkt_t         sampleFormat[4]; // Sample format buffer

/*
 * Setup function: Initializes EEPROM, serial communications, and LED pin.
 */
void setup()
{
    EEPROM.begin(33 + sizeof(bc7215DataMaxPkt_t));        // Initialize EEPROM with required size
    Serial.begin(115200);                                 // Start hardware serial for debugging
    bc7215Serial.begin(19200, SWSERIAL_8N2);              // Start software serial for BC7215
    pinMode(LED, OUTPUT);                                 // Set LED pin as output
    ledOff();                                             // Turn off LED initially
    mainState = MAIN_MENU;
    l2State = STEP1;
    interval = 10;
}

/*
 * Main loop: Displays menu and handles user choices for initialization, control, save/load, etc.
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
    // put your code for other tasks here
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
            switch (Serial.read())        // get main menu selection
            {
            case '1':  // Sample and initialize
                mainState = CAPTURE;
                l2State = STEP1;
                break;
            case '2':  // Control AC
                if (initialized)
                {
                    mainState = AC_CONTROL;
                    l2State = STEP1;
                }
                else
                {
                    Serial.println("\nAC control library not initialized yet, please initialize first");
                }
                break;
            case '3':  // Save data
                if (initialized)
                {
                    mainState = BACKUP;
                    l2State = STEP1;
                }
                else
                {
                    Serial.println("\nAC control library not initialized, can only save data after successful initialization");
                }
                break;
            case '4':  // Restore data
                l2State = STEP1;
                break;
            case '5':  // Find next protocol
                if (initialized)
                {
                    mainState = FIND_NEXT;
                    l2State = STEP1;
                }
                else
                {
                    Serial.println("\nThis function can only be used after initialization");
                }
                break;
            case '6':  // Load predefined
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
 * Signal capture handler: Capture remote IR signals and initialize AC control library
 */
void captureJob()
{
    switch (l2State)
    {
    case STEP1:  // Prompt user to prepare
        Serial.println("\nNow performing IR signal sampling and AC control library initialization. Please set AC remote to < Cool mode, 25°C "
                       ">. Enter any content to continue after ready...");
        clearSerialBuf();
        l2State = STEP2;
        break;
    case STEP2:  // Wait for user confirmation
        if (Serial.available())
        {
            Serial.println("Now please point at the IR receiver and press the <Fan Speed> button on the remote. \nThe program will automatically proceed to the next step after receiving the signal...");
            initialized = false;
            ac.startCapture();        // Start IR signal capture
            l2State = STEP3;
        }
        break;
    case STEP3:  // Process first sampling result
        if (ac.signalCaptured())
        {
            ac.stopCapture();
            if (ac.init())
            {
                initialized = true;
                ledOn();
            	Serial.print("Data received: ");
            	dataPkt = ac.getDataPkt();
            	printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
                Serial.println("AC control library initialized using received data  **SUCCESS** !!! ");
                extra = ac.extraSample();
                if (extra > 0)
                {
                    if (extra > 3)
                        extra = 3;
                    Serial.print("This AC format is special and requires additional sampling of original remote signals. \nNow please press the <<");
                    Serial.println(EXTRA_KEY[extra - 1] + ">> button on the remote for further sampling..");
                    Serial.println("Press any key to enter sampling step...");
                    clearSerialBuf();
                    l2State = STEP4;
                }
            }
            else  // Initialization failed
            {
                ledOff();
                Serial.println("AC control library initialization using received data **FAILED**, "
                               "\npossible remote control status setting error or reception/decoding error. Please check remote settings and try again");
            }
            l2State = STEP6;
        }
        break;
    case STEP4:  // Prepare for extra sampling
        if (Serial.available())
        {
            Serial.println("Starting sampling...");
            ac.startCapture();
            idleCntr = 0;
            msgCnt = 0;
            l2State = STEP5;
        }
        break;
    case STEP5:  // Extra sampling processing
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
                Serial.println("Signal collection complete!");
                l2State = STEP6;
            }
        }
        else
        {
            if ((msgCnt != 0) && (!ac.isBusy()))
            {
                idleCntr += 10;
                if (idleCntr >= 200)  // Sampling timeout
                {
                    ac.stopCapture();
                    Serial.println("Signal collection complete! Total signals collected: " + String(msgCnt));
                    for (int i = 0; i < msgCnt; i++)
                    {
                        printData(sampleData[i].data, (sampleData[i].bitLen + 7) / 8);
                    }
                    if (ac.init(msgCnt, sampleData, sampleFormat))
                    {
                        Serial.println("Reinitialization with new collected signals successful!");
                        ledOn();
                    }
                    else
                    {
                        Serial.println("Reinitialization with new collected signals failed...");
                        ledOff();
                    }
                    l2State = STEP6;
                }
            }
        }
        counter++;
        if (counter & 0x10)
        {
            ledOn();        // Blink LED while waiting
        }
        else
        {
            ledOff();
        }
        break;
    case STEP6:  // Sampling completion prompt
        ac.stopCapture();
        Serial.println("Please press any key. Program will return to main menu and AC control can begin...");
        clearSerialBuf();
        l2State = STEP7;
        break;
    case STEP7:  // Wait for user confirmation to return
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
    int comma1;
    int comma2;
    int temp;
    int mode;
    int fan;

    char inputChar;
    switch (l2State)
    {
    case STEP1:  // Show control menu
        showCtrlMenu();
        clearSerialBuf();
        l2State = STEP2;
        break;
    case STEP2:  // Handle control selections
        if (Serial.available())
        {
            switch (Serial.read())
            {
            case '1':  // Set parameters
                showParamMenu();
                clearSerialBuf();
                l2State = STEP3;
                break;
            case '2':  // Power on
                Serial.println("Sending AC power ON command");
                dataPkt = ac.on();        // Send AC on command
                Serial.print("Sending data: ");
                printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
                timeMs = 0;
                goBackState = STEP2;
                l2State = STEP4;
                break;
            case '3':  // Power off
                Serial.println("Sending AC power OFF command");
                dataPkt = ac.off();        // Send AC off command
                Serial.print("Sending data: ");
                printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
                timeMs = 0;
                goBackState = STEP2;
                l2State = STEP4;
                break;
            case '4':  // Return to main menu
                mainState = MAIN_MENU;
                l2State = STEP1;
                break;
            default:
                break;
            }
            clearSerialBuf();
        }
        break;
    case STEP3:        // change parameters
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
                Serial.println("Exit");
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
                        mode = 5;        // Keep mode
                    }
                    if (fan < 0 || fan > 3)
                    {
                        fan = 4;        // Keep fan
                    }
                    if (temp >= 16 && temp <= 30)
                    {
                        Serial.println("Sending command to set AC to: " + String(temp) + "°C, Mode: " + MODES[mode] + "  Fan Speed: "
                            + FANSPEED[fan]);
                    }
                    else
                    {
                        Serial.println(
                            "Sending command to set AC to: Temperature unchanged, Mode: " + MODES[mode] + "  Fan Speed: " + FANSPEED[fan]);
                    }
                    dataPkt = ac.setTo(temp, mode, fan, -1);        // Set AC parameters
                    Serial.print("Sending data: ");
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
                            Serial.println("Sending command to set AC to: " + String(temp) + "°C");
                        }
                        else
                        {
                            Serial.println("Sending command to set AC to: Temperature unchanged");
                        }
                        dataPkt = ac.setTo(temp);        // Set AC parameters
                        Serial.print("Sending data: ");
                        printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
                        timeMs = 0;
                        goBackState = STEP3;
                        l2State = STEP4;
                    }
                    else
                    {
                        Serial.println("Input format error, please re-enter");
                    }
                }
            }
        }
        break;
    case STEP4:  // Wait for transmission completion
        timeMs += interval;
        if (!ac.isBusy() || (timeMs > 3000))
        {
            if (timeMs > 3000)
            {
                Serial.println("Send timeout");
            }
            Serial.println("Send complete!");
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

void backupJob()
{
    switch (l2State)
    {
    case STEP1:
        formatPkt = ac.getFormatPkt();
        dataPkt = ac.getDataPkt();
        EEPROM.put(0, *formatPkt);                              // Save format packet to EEPROM
        EEPROM.put(33, *((bc7215DataMaxPkt_t*)dataPkt));        // Save data packet to EEPROM
        EEPROM.commit();
        Serial.println("\nFormat Information: ");
        printData(formatPkt, 33);
        Serial.print("Data: ");
        printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
        Serial.println("Information saved successfully");
        l2State = STEP2;
        break;
    case STEP2:  // Wait for user confirmation
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

void restoreJob()
{
    switch (l2State)
    {
    case STEP1:
        EEPROM.get(0, sampleFormat[0]);                              // Load format packet from EEPROM
        EEPROM.get(33, sampleData[0]);        // Load data packet from EEPROM
        Serial.println("\nUsing");
        Serial.print("Format Information: ");
        printData(&sampleFormat[0], sizeof(bc7215FormatPkt_t));
        Serial.print("Data: ");
        printData(sampleData[0].data, (sampleData[0].bitLen + 7) / 8);
        if (ac.init(sampleData[0], sampleFormat[0]))
        {
            Serial.println("AC control library initialization  ***SUCCESS*** !");
            initialized = true;
        }
        else
        {
            Serial.println("AC control library initialization failed...");
            initialized = false;
        }
        Serial.println("Press any key to continue");
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
            Serial.println("Next protocol match successful!");
        }
        else
        {
            Serial.println("No other matching protocols. AC control library needs reinitialization");
            initialized = false;
        }
        Serial.println("Press any key to continue...");
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
            Serial.println("Selected type " + String(choice));
            printData(bc7215_ac_predefined_data(choice), 13);
            printData(bc7215_ac_predefined_fmt(choice), 33);
            if (ac.initPredef(choice))
            {
                Serial.println("Initialization successful !!! Press any key to continue");
                initialized = true;
                ledOn();
            }
            else
            {
                Serial.println("Initialization failed.... Press any key to continue");
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
 * mainMenu: Displays main menu and returns user choice.
 */
void showMainMenu()
{
    Serial.println("\n\n*************************************************************");
    Serial.println("*    Welcome to BC7215 Universal AC Control Demo Program    *");
    Serial.println("*************************************************************");
    Serial.print("Current AC control library status: ");
    if (initialized)
    {
        Serial.println("***INITIALIZED***");
    }
    else
    {
        Serial.println("Not initialized (must be initialized before use)");
    }
    Serial.println("Please select:");
    Serial.println("   1. Sample and initialize control library");
    Serial.println("   2. Control AC");
    Serial.println("   3. Save initialization data");
    Serial.println("   4. Load saved data and initialize using that data");
    Serial.println("   5. Try next match (if initialization successful but cannot control AC properly)");
    Serial.println("   6. Load predefined protocol");
    Serial.println("");
}

/*
 * ctrlMenu: Displays AC control menu and returns user choice.
 */
void showCtrlMenu()
{
    Serial.println("\nAC Control, please select:");
    Serial.println("   1. Set AC parameters");
    Serial.println("   2. Power ON");
    Serial.println("   3. Power OFF");
    Serial.println("   4. Return to upper menu");
    Serial.println("");
}

void showParamMenu()
{
    Serial.println("AC Parameter Adjustment: Please enter the temperature, e.g.: 24 ; "
                   "\nor enter 'temperature', 'mode', 'fan level' separated by commas ',', e.g.: 24, 1, 2");
    Serial.println(" Ranges and meanings:  Temperature(°C)    Mode           Fan Speed");
    Serial.println("                      Range: 16~30        0 - Auto       0 - Auto");
    Serial.println("                                          1 - Cool       1 - Low");
    Serial.println("                                          2 - Heat       2 - Medium");
    Serial.println("                                          3 - Dry        3 - High");
    Serial.println("                                          4 - Fan");
    Serial.println(" * Values outside the above ranges indicate maintaining current status for that item");
    Serial.println("------------------------------------------------------------------------------------");
    Serial.println("(Note: Limited to settings supported by controlled AC. \nFor example: setting heat mode on cooling-only AC will have unpredictable results)");
    Serial.println("Please enter AC parameter values: (Enter 'exit' to return to upper menu, case insensitive)");
    Serial.println("");
}

void showPredefMenu()
{
    Serial.println("");
    Serial.println("A few protocols are not supported for direct decoding by BC7215A chip, provided as predefined protocols.");
    Serial.println("When direct sampling fails or initialization fails, you can try using predefined data to control the AC.");
    Serial.println("Brand names listed in protocol names are for reference only. If unable to control, please try all predefined protocols individually.");
    Serial.println("Please select:");
    for (int i = 0; i < ac.cntPredef(); i++)
    {
        Serial.println("  " + String(i) + ". " + ac.getPredefName(i));
    }
    Serial.println("");
}

/*
 * clearSerialBuf: clear any data remaining in Serial buffer.
 */
void clearSerialBuf()
{
    while (Serial.available())
    {
        Serial.read();        // Clear buffer
    }
}

/*
 * printData: Prints binary data in hex format.
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
 * LED control functions: Turn LED on or off.
 */
void ledOn() { digitalWrite(LED, LOW); }
void ledOff() { digitalWrite(LED, HIGH); }
