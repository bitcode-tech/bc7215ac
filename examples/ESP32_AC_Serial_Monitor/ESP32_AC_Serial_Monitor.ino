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
const int    LED = 4;                                                             // Onboard LED pin
const String MODES[] = { "Auto", "Cool", "Heat", "Dry", "Fan", "Keep" };         // AC modes
const String FANSPEED[] = { "Auto", "Low", "Med", "High", "Keep" };              // Fan speed levels
const String EXTRA_KEY[] = { "Temp +/-", "Mode", "Fan Control" };               // Extra sampling keys

// State machine enumerations
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
    STEP1, STEP2, STEP3, STEP4, STEP5, STEP6, STEP7    // Secondary state steps
};

// BC7215 communication setup
HardwareSerial bc7215Serial(1);                    // BC7215 connects to UART1
BC7215         bc7215Board(bc7215Serial, 27, 26);   // mod=GPIO27, busy=GPIO26
BC7215AC       ac(bc7215Board);                    // AC control object

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
 * Setup: Initialize EEPROM, serial communication and LED
 */
void setup()
{
    EEPROM.begin(33 + sizeof(bc7215DataMaxPkt_t));        // Initialize EEPROM
    Serial.begin(115200);                                 // Debug serial
    bc7215Serial.begin(19200, SERIAL_8N2, 25, 33);       // BC7215 serial, RX=GPIO25, TX=GPIO33
    pinMode(LED, OUTPUT);                                 // LED output mode
    ledOff();                                             // Turn off LED
    mainState = MAIN_MENU;
    l2State = STEP1;
    interval = 10;
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
                    Serial.println("\nAC control library not initialized yet, data can only be saved after successful initialization");
                }
                break;
            case '4':  // Restore data
                mainState = RESTORE;
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
                    Serial.println("\nThis function is only available after initialization");
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
  Serial.println("\nNow performing IR signal sampling and AC control library initialization. "
  "\nPlease set AC remote to < Cooling mode, 25°C(77°F)>, then press any key to continue...");
        clearSerialBuf();
        l2State = STEP2;
        break;
    case STEP2:  // Wait for user confirmation
        if (Serial.available())
        {
            Serial.println("Now please aim at IR receiver and press <Fan Control> button on remote. \nWill automatically proceed to next step after receiving signal...");
            initialized = false;
            ac.startCapture();        // Start IR signal capture
            l2State = STEP3;
        }
        break;
    case STEP3:  // Process first sampling result
        if (ac.signalCaptured())
        {
            ac.stopCapture();
            
            if (ac.init())  // Try to initialize
            {
                initialized = true;
                ledOn();
            	Serial.print("Received data: ");
            	dataPkt = ac.getDataPkt();
            	printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
                Serial.println("AC control library initialization using received data  **SUCCESS** !!! ");
                extra = ac.extraSample();
                if (extra > 0)  // Need extra sampling
                {
                    if (extra > 3) extra = 3;
                    Serial.print("This AC format is special, need further sampling of original remote signals. \nNow please press <<");
                    Serial.println(EXTRA_KEY[extra - 1] + ">> button on remote for additional sampling..");
                    Serial.println("Enter any content to start sampling step...");
                    clearSerialBuf();
                    l2State = STEP4;
                }
            }
            else  // Initialization failed
            {
                ledOff();
                Serial.println("AC control library initialization using received data **FAILED**, "
                               "\npossibly due to incorrect remote state settings or receiving decode errors. Please check remote settings and try again");
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
            if (msgCnt < 3) msgCnt++;
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
                    Serial.println("Signal collection complete! Total collected " + String(msgCnt) + " signals");
                    for (int i = 0; i < msgCnt; i++)
                    {
                        printData(sampleData[i].data, (sampleData[i].bitLen + 7) / 8);
                    }
                    if (ac.init(msgCnt, sampleData, sampleFormat))
                    {
                        Serial.println("Re-initialization using new collected signals succeeded!");
                        ledOn();
                    }
                    else
                    {
                        Serial.println("Re-initialization using new collected signals failed...");
                        ledOff();
                    }
                    l2State = STEP6;
                }
            }
        }
        // LED blinks while waiting
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
    case STEP6:  // Sampling completion prompt
        ac.stopCapture();
        Serial.println("Now please enter any content, program will return to main menu and AC control can begin...");
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
    int comma1, comma2, temp, mode, fan;
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
                Serial.println("Sending AC power on command");
                dataPkt = ac.on();
                Serial.print("Sending data: ");
                printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
                timeMs = 0;
                goBackState = STEP2;
                l2State = STEP4;
                break;
            case '3':  // Power off
                Serial.println("Sending AC power off command");
                dataPkt = ac.off();
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
    case STEP3:  // Parameter setting processing
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
                Serial.println("Exit");
                l2State = STEP1;
            }
            else
            {
                comma1 = newParam.indexOf(',');
                comma2 = newParam.indexOf(',', comma1 + 1);
                
                if (comma1 != -1 && comma2 != -1 && comma2 > comma1)  // Three parameter format
                {
                    temp = newParam.substring(0, comma1).toInt();
                    mode = newParam.substring(comma1 + 1, comma2).toInt();
                    fan = newParam.substring(comma2 + 1).toInt();
                    
                    if (mode < 0 || mode > 4) mode = 5;  // Keep mode
                    if (fan < 0 || fan > 3) fan = 4;     // Keep fan speed
                    
                    if (temp >= 16 && temp <= 30)
                    {
                        Serial.println("Sending command to set AC to: " + String(temp) + "°C, Mode: " + MODES[mode] + "  Fan: " + FANSPEED[fan]);
                    }
                    else
                    {
                        Serial.println("Sending command to set AC to: Temperature unchanged, Mode: " + MODES[mode] + "  Fan: " + FANSPEED[fan]);
                    }
                    dataPkt = ac.setTo(temp, mode, fan, -1);
                    Serial.print("Sending data: ");
                    printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
                    timeMs = 0;
                    goBackState = STEP3;
                    l2State = STEP4;
                }
                else if (comma1 == -1)  // Temperature parameter only
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
                    dataPkt = ac.setTo(temp);
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
        break;
    case STEP4:  // Wait for transmission completion
        timeMs += interval;
        if (!ac.isBusy() || (timeMs > 3000))
        {
            if (timeMs > 3000) Serial.println("Transmission timeout");
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
 * Data backup: Save initialization data to EEPROM
 */
void backupJob()
{
    switch (l2State)
    {
    case STEP1:
        formatPkt = ac.getFormatPkt();
        dataPkt = ac.getDataPkt();
        EEPROM.put(0, *formatPkt);                              // Save format packet
        EEPROM.put(33, *((bc7215DataMaxPkt_t*)dataPkt));        // Save data packet
        EEPROM.commit();
        Serial.println("\nFormat info: ");
        printData(formatPkt, 33);
        Serial.print("Data: ");
        printData(dataPkt->data, (dataPkt->bitLen + 7) / 8);
        Serial.println("Information saved");
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

/*
 * Data restore: Read data from EEPROM and initialize
 */
void restoreJob()
{
    switch (l2State)
    {
    case STEP1:
        EEPROM.get(0, sampleFormat[0]);                              // Read format packet
        EEPROM.get(33, sampleData[0]);        // Read data packet
        Serial.println("\nUsing");
        Serial.print("Format info: ");
        printData(&sampleFormat[0], sizeof(bc7215FormatPkt_t));
        Serial.print("Data: ");
        printData(&sampleData[0].data, (sampleData[0].bitLen + 7) / 8);
        
        if (ac.init(sampleData[0], sampleFormat[0]))
        {
            Serial.println("AC control library initialization  ***SUCCESS*** !");
            initialized = true;
            ledOn();
        }
        else
        {
            Serial.println("AC control library initialization failed...");
            initialized = false;
            ledOff();
        }
        Serial.println("Enter any content to continue");
        l2State = STEP2;
        clearSerialBuf();
        break;
    case STEP2:  // Wait for user confirmation
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
            initialized = false;
            ledOff();
        }
        Serial.println("Enter any content to continue...");
        clearSerialBuf();
        l2State = STEP2;
        break;
    case STEP2:  // Wait for user confirmation
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
    case STEP1:  // Show predefined menu
        showPredefMenu();
        clearSerialBuf();
        l2State = STEP2;
        break;
    case STEP2:  // Handle user selection
        if (Serial.available())
        {
            choice = Serial.parseInt();
            Serial.println("Selected option " + String(choice));
            printData(bc7215_ac_predefined_data(choice), 13);
            printData(bc7215_ac_predefined_fmt(choice), 33);
            
            if (ac.initPredef(choice))
            {
                Serial.println("Initialization successful !!! Enter any content to continue");
                initialized = true;
                ledOn();
            }
            else
            {
                Serial.println("Initialization failed.... Enter any content to continue");
                initialized = false;
                ledOff();
            }
            clearSerialBuf();
            l2State = STEP3;
        }
        break;
    case STEP3:  // Wait for user confirmation
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
 * Display main menu
 */
void showMainMenu()
{
    Serial.println("\n\n****************************************************");
    Serial.println("*  Welcome to BC7215 Universal AC Controller Demo  *");
    Serial.println("****************************************************");
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
    Serial.println("   2. Control air conditioner");
    Serial.println("   3. Save initialization data");
    Serial.println("   4. Read saved data and initialize with it");
    Serial.println("   5. Try next match (if initialized successfully but cannot control AC properly)");
    Serial.println("   6. Load predefined protocol");
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
    Serial.println("AC parameter adjustment: Enter temperature number, e.g.: 24 ; "
                   "\nor enter 'temperature', 'mode', 'fan level' separated by commas ',', e.g.: 24, 1, 2");
    Serial.println("   Ranges and meanings:  Temperature(°C)    Mode           Fan");
    Serial.println("                      Range: 16~30           0 - Auto       0 - Auto");
    Serial.println("                                             1 - Cool       1 - Low");
    Serial.println("                                             2 - Heat       2 - Med");
    Serial.println("                                             3 - Dry        3 - High");
    Serial.println("                                             4 - Fan");
    Serial.println(" * Values outside above ranges indicate maintaining current state for that item");
    Serial.println("------------------------------------------------------------------------------------");
    Serial.println("(Note: Limited to settings supported by the controlled AC. For example: setting heat mode on cooling-only AC will have unpredictable results)");
    Serial.println("Now please enter AC parameter values: (enter 'exit' to return to upper menu, case insensitive)");
    Serial.println("");
}

/*
 * Display predefined protocol menu
 */
void showPredefMenu()
{
    Serial.println("");
    Serial.println("A few protocols are not supported for direct decoding by BC7215A chip, provided as predefined protocols.");
    Serial.println("When direct sampling fails or initialization is unsuccessful, try using predefined data to control AC.");
    Serial.println("Brand names in protocol names are for reference only. If control fails, please try all predefined protocols one by one");
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

/*
 * LED control functions: Turn LED on and off
 */
void ledOn() { digitalWrite(LED, LOW); }    // Turn on LED (active low)
void ledOff() { digitalWrite(LED, HIGH); }  // Turn off LED (active high)
