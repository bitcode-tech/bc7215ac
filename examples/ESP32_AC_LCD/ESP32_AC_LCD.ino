/*
 * ESP32 Air Conditioner Control
 * 
 * This program controls an air conditioner using an ESP32 microcontroller with:
 * - BC7215A IR transceiver module for sending AC control signals
 * - TFT display for user interface (TTGO T-Display)
 * - Onboard physical buttons for control
 * 
 * Features:
 * - Universal A/C control, offline and controls all brands/models.
 * - One click pairing
 * - Controls temperature, mode, and fan speed
 * - GUI with status indicators
 */

#include <Preferences.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <bc7215.h>
#include <bc7215ac.h>


// ======= Hardware Configuration (TTGO T-Display) =======
#define BUTTON_LEFT    0         // Left button (-)
#define BUTTON_RIGHT   35        // Right button (+)
#define BC7215_MOD     27        // BC7215 module control pin
#define BC7215_BUSY    26        // BC7215 busy status pin
#define BC7215_PORT_RX 25        // ESP32 RX, connect to BC7215 TX
#define BC7215_PORT_TX 33        // ESP32 TX, connect to BC7215 RX


// ======= Display Constants (Portrait 135x240) =======
#define SCREEN_W 135
#define SCREEN_H 240

// ======= Theme Colors =======
static const uint16_t COLOR_BG = TFT_WHITE;                // Background color
static const uint16_t COLOR_TEXT = TFT_BLACK;              // Text color
static const uint16_t COLOR_ONLINE_BADGE = TFT_DARKCYAN;   // Top badge background when online
static const uint16_t COLOR_LABEL_BG = TFT_DARKCYAN;   	   // Middle label background
static const uint16_t COLOR_BADGE_FG = TFT_WHITE;          // Badge text color
static const uint16_t COLOR_ACTION_BG = TFT_LIGHTGREY;     // Bottom button background
static const uint16_t COLOR_ACTION_FG = TFT_BLACK;         // Bottom button text color
static const uint16_t COLOR_ACTIVE_BADGE = TFT_RED;        // Badge color when active/sending

// ====== Display Content Arrays =======
const String MODES[] = { " AUTO ", " COOL ", " HEAT ", " DRY ", " FAN " };        // AC operation modes
const String FANSPEED[] = { " AUTO ", " LOW ", " MED ", " HIGH " };               // Fan speed levels
const String EXTRA_KEY[] = { "TEMP +/-", "MODE", "FAN SPEED" };                   // Extra keys for additional IR signal capture
const char*  MAIN_MENU[] = { "Capture And Init.", "Find Next Match", "Load Pre-def", "Exit" };

// ======= State Machine Enumerations =======

// Main application states (Level 1)
enum L1_STATE
{
    START,          // Initial startup state
    INIT,           // AC initialization and IR learning
    TEMP_CTL,       // Temperature control mode (main operation)
    ON_OFF_CTL,     // Power on/off control
    MENU_MAIN,      // Main menu display
    MENU_PREDEF,    // Predefined AC models menu
    FIND_NEXT,      // Find next matching AC protocol
    IR_SENDING,     // Currently sending IR signal
    NOT_CONNECTED   // BC7215 module not connected
};

// Sub-states for complex operations (Level 2)
enum L2_STATE
{
    STEP1, STEP2, STEP3, STEP4, STEP5, STEP6, STEP7
};

// Button press detection states
enum KEYBOARD_STATE
{
    BOTH_RELEASED,      // No buttons pressed
    ONE_PRESSED,        // Single button pressed
    BOTH_PRESSED,       // Both buttons pressed
    ONE_RELEASED,       // One button released while other still pressed
    ONE_LONG_PRESSED,   // Single button held for long time
    BOTH_LONG_PRESSED   // Both buttons held for long time
};

// Button events generated from state machine
enum KEY_EVENT
{
    NO_KEY,             // No button event
    LEFT_KEY_SHORT,     // Left button short press
    RIGHT_KEY_SHORT,    // Right button short press
    LEFT_KEY_LONG,      // Left button long press
    RIGHT_KEY_LONG,     // Right button long press
    BOTH_KEY_SHORT,     // Both buttons short press
    BOTH_KEY_LONG       // Both buttons long press
};

// ======= Global Objects and Variables =======
Preferences    savedData;                           // Non-volatile storage for AC settings
TFT_eSPI       tft = TFT_eSPI();                   // TFT display object
HardwareSerial BC7215_SERIAL(1);                  // Serial port for BC7215 communication
BC7215         bc7215Board(BC7215_SERIAL, BC7215_MOD, BC7215_BUSY);  // BC7215 control object
BC7215AC       ac(bc7215Board);                    // AC control object

// State variables
L1_STATE                  mainState;               // Current main state
L1_STATE                  retState;                // Return state after IR sending
L2_STATE                  l2State;                 // Current sub-state
KEYBOARD_STATE            keyboardState;           // Current button state
KEY_EVENT                 keyEvent;                // Last button event

// Control variables
int                       interval;                // Main loop delay interval
int                       temp = 25;               // Current temperature setting (25°C)
int                       mode = 1;                // Current AC mode (1 = COOL)
int                       fan = 1;                 // Current fan speed (1 = LOW)
int                       remoteBtn = 0;           // Last remote button pressed (0 = TEMP-)
const char**              menuItems;               // Current menu items array
const char**              preDefs;                 // Predefined AC models array
int                       currentMenuSelection;    // Currently selected menu item
unsigned int              timerMs;                // General purpose timer
bool                      usingCelsius = true;    // Temperature unit flag
int                       msgCnt;                 // Message counter
int                       idleCntr;               // Idle counter
int                       extra;                  // Extra IR signals needed
int                       savedTemp;              // Temporary temperature storage
int8_t                    matchCnt;               // Number of AC protocol matches found
bool					  irSending = false;	  // Flag indicating IR is transmitting
bool					  initOK = false;		  // Flag indicating Init has been done successfully

// BC7215 data structures
const bc7215DataVarPkt_t* dataPkt;                // IR data packet pointer
const bc7215FormatPkt_t*  formatPkt;              // IR format packet pointer
bc7215DataMaxPkt_t        sampleData[4];          // Storage for captured IR data
bc7215FormatPkt_t         sampleFormat[4];        // Storage for captured IR format

// ================== Arduino Standard Functions ==================

void setup()
{
    // Initialize serial communication for debugging
    Serial.begin(115200);
    
    // Initialize TFT display
    tft.init();
    tft.setRotation(0);                // Portrait orientation (135x240)
    tft.setTextDatum(TC_DATUM);        // Top-center text alignment
    
    // Configure button pins
    pinMode(BUTTON_LEFT, INPUT_PULLUP);
    pinMode(BUTTON_RIGHT, INPUT);

    // Initialize BC7215 serial communication
    BC7215_SERIAL.begin(19200, SERIAL_8N2, BC7215_PORT_RX, BC7215_PORT_TX);
    Serial.println("BC7215 SERIAL INITIALIZED");
    
    // Initialize predefined AC models array
    preDefs = new const char*[ac.cntPredef()];
    for (int i = 0; i < ac.cntPredef(); i++)
    {
        preDefs[i] = ac.getPredefName(i);
    }
    
    // Initialize state variables
    mainState = START;
    l2State = STEP1;
    keyboardState = BOTH_RELEASED;
    keyEvent = NO_KEY;
    interval = 10;
}

void loop()
{
    // Main state machine
    switch (mainState)
    {
    case START:
        powerup();              // Handle startup sequence
        break;
    case INIT:
        initAC();              // Handle AC initialization and IR learning
        break;
    case TEMP_CTL:
        tempCtrl();            // Handle temperature control interface
        break;
    case ON_OFF_CTL:
        onOffCtrl();           // Handle power on/off interface
        break;
    case MENU_MAIN:
        mainMenu();            // Handle main menu navigation
        break;
    case MENU_PREDEF:
        predefMenu();          // Handle predefined AC models menu
        break;
    case FIND_NEXT:
        findNext();            // Handle finding next AC protocol match
        break;
    case IR_SENDING:
        // Wait for IR transmission to complete
        if (!ac.isBusy())
        {
			irSending = false;
            drawIrBadge(COLOR_BG);        // Clear IR activity indicator
            mainState = retState;         // Return to previous state
        }
		else
		{
			timerMs += interval;
			if (timerMs > 3000)				// Transmitting overtime
			{
				irSending = false;
	            drawIrBadge(COLOR_BG);        // clear IR badge
	            mainState = retState;         // return to previous state
			}
		}
        break;
    case NOT_CONNECTED:
        flashNoConn();         // Display connection error
        break;
    default:
        break;
    }
    
    updateKeyStatus();         // Process button inputs
    delay(interval);
}

/*
 * Startup sequence handler
 * Initializes hardware and loads saved AC configuration
 */
void powerup()
{
    switch (l2State)
    {
    case STEP1:
        // Clear screen and initialize BC7215
        tft.fillScreen(COLOR_BG);
        bc7215Board.setTx();           // Set BC7215 to transmit mode
        delay(50);
        bc7215Board.setShutDown();     // Put BC7215 in shutdown mode
        timerMs = 0;

        l2State = STEP2;
        break;
        
    case STEP2:
        // Wait for BC7215 to be ready
        if (!bc7215Board.isBusy())
        {
            bc7215Board.setRx();       // Wake up BC7215 and set to receive mode
            delay(50);
            l2State = STEP3;
        }
        else
        {
            // Timeout handling
            timerMs += interval;
            if (timerMs > 1000)
            {
                mainState = NOT_CONNECTED;
                timerMs = 0;
            }
        }
        break;
        
    case STEP3:
        // Load saved AC configuration from flash memory
        savedData.begin("bc7215 init", true);
        savedData.getBytes("format", &sampleFormat[0], sizeof(bc7215FormatPkt_t));
        savedData.getBytes("data", &sampleData[0], sizeof(bc7215DataMaxPkt_t));
        savedData.getBytes("matchIndex", &matchCnt, sizeof(matchCnt));
        
        // Try to initialize AC with saved data
        if (ac.init(sampleData[0], sampleFormat[0]))
        {
			initOK = true;
            // If we have multiple matches, navigate to the correct one
            if (matchCnt > 0)
            {
                bool result;
                for (int i = 0; i < matchCnt; i++)
                {
                    result = ac.matchNext();
                }
                if (!result)        // If can't find that match, use the original
                {
                    ac.init(sampleData[0], sampleFormat[0]);
                    matchCnt = 0;
                }
            }
            
            // Load extra IR signals if available
            if (ac.extraSample())
            {
                savedData.getBytes("extraFormat", &sampleFormat[1], sizeof(bc7215FormatPkt_t));
                savedData.getBytes("extraData", &sampleData[1], sizeof(bc7215DataMaxPkt_t));
                ac.saveExtra((bc7215DataVarPkt_t*)&sampleData[1], &sampleFormat[1]);
            }
            
            // Initialize default AC settings
            temp = 25;
            mode = 1;
            fan = 1;
            mainState = TEMP_CTL;
            
            // Display initial interface
            drawBigNumber(String(temp));
            drawModeLabel(mode);
            drawFanLabel(fan);
            drawTempButtons();
        }
        else
        {
            // No saved configuration found, show main menu
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
 * AC initialization and IR signal learning process
 * Guides user through capturing IR signals from their AC remote
 */
void initAC()
{
    switch (l2State)
    {
    case STEP1:
        // Show initial instruction screen
        drawOKButton();
        drawEscButton();
        showInitScrn1();
        l2State = STEP2;
        break;
        
    case STEP2:
        // Wait for user to start capture or cancel
        if (keyEvent == RIGHT_KEY_SHORT)
        {
            clearRightBtn();
            showInitScrn2("<FAN SPEED>");  // Show capture instruction
            ac.startCapture();              // Begin IR signal capture
            l2State = STEP3;
        }
        if (keyEvent == LEFT_KEY_SHORT)
        {
            mainState = START;              // Cancel and return to start
            l2State = STEP1;
        }
        keyEvent = NO_KEY;
        break;
        
    case STEP3:
        // Wait for IR signal capture and process result
        if (ac.signalCaptured())
        {
            ac.stopCapture();
            if (ac.init())                  // Try to initialize with captured signal
            {
				initOK = true;
                matchCnt = 0;
                extra = ac.extraSample();   // Check if extra signals needed
                if (extra > 0)              // Need additional captures
                {
                    if (extra > 3) extra = 3;  // Limit to maximum 3 extra captures
                    showInitScrn3(EXTRA_KEY[extra - 1]);  // Show what button to press next
                    drawOKButton();
                    l2State = STEP7;
                }
                else                        // Initialization successful
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
                showInitFailMsg();          // Initialization failed
                l2State = STEP5;
            }
        }
        if (keyEvent == LEFT_KEY_SHORT)
        {
            mainState = START;              // Cancel and return to start
            l2State = STEP1;
        }
        keyEvent = NO_KEY;
        break;
        
    case STEP4:
        // Capture additional IR signals for complete AC control
        if (ac.signalCaptured((bc7215DataVarPkt_t*)&sampleData[msgCnt], &sampleFormat[msgCnt]))
        {
            if (msgCnt < 3) msgCnt++;
            idleCntr = 0;
            if (extra < 3)
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
            // Handle timeout if no more signals detected
            if ((msgCnt != 0) && (!ac.isBusy()))
            {
                idleCntr += interval;
                if (idleCntr >= 200)        // 200ms timeout
                {
                    ac.stopCapture();
                    if (ac.init(msgCnt, sampleData, sampleFormat))
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
        // Handle initialization failure
        if (keyEvent == RIGHT_KEY_SHORT)
        {
            showInitScrn1();               // Retry initialization
            l2State = STEP2;
        }
        else if (keyEvent == LEFT_KEY_SHORT)
        {
            mainState = START;             // Return to start
            l2State = STEP1;
        }
        keyEvent = NO_KEY;
        break;
        
    case STEP6:
        // Wait for OK button to continue
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
        // Wait for OK button then start extra capture
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
 * Main temperature control interface
 * Handles temperature adjustment, mode changes, and fan speed control
 */
void tempCtrl()
{
    switch (keyEvent)
    {
    case LEFT_KEY_SHORT:
        // Decrease temperature (only for AUTO, COOL, HEAT modes)
        if (mode <= 2)
        {
            if (temp > 16)
            {
                temp--;
            }
            remoteBtn = 0;                 // TEMP- button
			irSending = true;
            acDispUpdate();                // Update display
            acStatUpdate();                // Send IR command and MQTT update
        }
        break;
        
    case RIGHT_KEY_SHORT:
        // Increase temperature (only for AUTO, COOL, HEAT modes)
        if (mode <= 2)
        {
            if (temp < 30)
            {
                temp++;
            }
            remoteBtn = 1;                 // TEMP+ button
			irSending = true;
            acDispUpdate();
            acStatUpdate();
        }
        break;
        
    case LEFT_KEY_LONG:
        // Change AC mode
        mode++;
        if (mode >= 5)
        {
            mode = 0;
            temp = savedTemp;              // Restore temperature when leaving DRY mode
        }
        if (mode == 3)                     // Entering DRY mode
        {
            savedTemp = temp;              // Save current temperature
            temp = 16;                     // DRY mode typically uses fixed low temp
        }
        remoteBtn = 2;                     // MODE button
		irSending = true;
        acDispUpdate();
        acStatUpdate();
        break;
        
    case RIGHT_KEY_LONG:
        // Change fan speed
        fan++;
        if (fan >= 4)
        {
            fan = 0;
        }
        remoteBtn = 3;                     // FAN SPEED button
		irSending = true;
        acDispUpdate();
        acStatUpdate();
        break;
        
    case BOTH_KEY_SHORT:
        // Switch to power on/off control
        mainState = ON_OFF_CTL;
        clearCentralArea();
        drawOnOffButtons();
        break;
        
    case BOTH_KEY_LONG:
        // Enter main menu
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
 * Update display to show current AC status and indicate IR transmission
 */
void acDispUpdate()
{
	if (irSending)
	{
    	drawIrBadge(COLOR_ACTIVE_BADGE);       // Show IR activity
	}
    
    // Update temperature display (hide for DRY & FAN mode)
    if (mode < 3)
    {
        drawBigNumber(String(temp));
    }
    else
    {
        drawBigNumber("    ");             // Clear temperature display for DRY & FAN mode
    }
    drawModeLabel(mode);
    drawFanLabel(fan);
	timerMs = 0;
    mainState = IR_SENDING;
    retState = TEMP_CTL;
}

/*
 * Send IR command and update MQTT status
 */
void acStatUpdate()
{
    ac.setTo(temp, mode, fan, remoteBtn);  // Send IR command to AC
}

/*
 * Power on/off control interface
 */
void onOffCtrl()
{
    switch (keyEvent)
    {
    case LEFT_KEY_SHORT:
        // Power ON
		irSending = true;
        acDispUpdate();
        ac.on();
		timerMs = 0;
        mainState = IR_SENDING;
        retState = TEMP_CTL;               // Return to temperature control after sending
        break;
        
    case RIGHT_KEY_SHORT:
        // Power OFF
		irSending = true;
        drawIrBadge(COLOR_ACTIVE_BADGE);
        ac.off();
		timerMs = 0;
        mainState = IR_SENDING;
        retState = ON_OFF_CTL;             // Stay in power control after sending
        break;
        
    case BOTH_KEY_SHORT:
        // Return to temperature control
        acDispUpdate();
        drawTempButtons();
        break;
        
    case BOTH_KEY_LONG:
        // Enter main menu
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
 * Main menu navigation
 */
void mainMenu()
{
    int newIndex;
    switch (keyEvent)
    {
    case LEFT_KEY_SHORT:
        // Navigate menu items
        newIndex = currentMenuSelection + 1;
        if (newIndex >= sizeof(MAIN_MENU) / sizeof(char*))
        {
            newIndex = 0;
        }
        updateMenu(newIndex);
        break;
        
    case RIGHT_KEY_SHORT:
        // Select menu item
        switch (currentMenuSelection)
        {
        case 0:        // Capture And Init.
            mainState = INIT;
            l2State = STEP1;
            break;
        case 1:        // Find Next Match
            l2State = STEP1;
            mainState = FIND_NEXT;
            break;
        case 2:        // Load Pre-def
            mainState = MENU_PREDEF;
            showMenu(preDefs, ac.cntPredef());
            currentMenuSelection = 0;
            updateMenu(0);
            break;
        case 3:        // Exit
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
 * Predefined AC models menu navigation
 */
void predefMenu()
{
    int newIndex;
    switch (keyEvent)
    {
    case LEFT_KEY_SHORT:
        // Navigate predefined models
        newIndex = currentMenuSelection + 1;
        if (newIndex >= ac.cntPredef())
        {
            newIndex = 0;
        }
        updateMenu(newIndex);
        break;
        
    case RIGHT_KEY_SHORT:
        // Select predefined model
        if (ac.initPredef(currentMenuSelection))
        {
            saveInitInfo();
            clearCentralArea();
            mainState = TEMP_CTL;
            // Reset to default values
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
            mainState = MENU_MAIN;         // Return to main menu if failed
        }
        break;
    default:
        break;
    }
    keyEvent = NO_KEY;
}

/*
 * Find next matching AC protocol
 */
void findNext()
{
    switch (l2State)
    {
    case STEP1:
        if (initOK)
        {
            if (ac.matchNext())            // Try to find next matching protocol
            {
                matchCnt++;
                // Save the new match count
                savedData.begin("bc7215 init", false);
                savedData.putBytes("matchIndex", &matchCnt, sizeof(matchCnt));
                savedData.end();
                showNextMatchScrn(matchCnt);
                retState = TEMP_CTL;
            }
            else
            {
				initOK = false;
                matchCnt = 0;
                showNextFailScrn();        // No more matches found
                retState = START;
            }
        }
        else
        {
            showNoInitScrn();              // AC not initialized
            retState = START;
        }
        clearLeftBtn();
        drawOKButton();
        l2State = STEP2;
        break;
        
    case STEP2:
        // Wait for OK button
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
 * Save AC initialization data to non-volatile storage
 */
void saveInitInfo()
{
    formatPkt = ac.getFormatPkt();
    dataPkt = ac.getDataPkt();
    savedData.begin("bc7215 init", false);
    savedData.putBytes("format", formatPkt, sizeof(bc7215FormatPkt_t));
    savedData.putBytes("data", dataPkt, sizeof(bc7215DataMaxPkt_t));
    savedData.putBytes("matchIndex", &matchCnt, sizeof(matchCnt));
    
    // Save extra IR signals if available
    if (ac.extraSample())
    {
        savedData.putBytes("extraFormat", ac.getExtra().body.msg.fmt, sizeof(bc7215FormatPkt_t));
        savedData.putBytes("extraData", (bc7215DataMaxPkt_t*)ac.getExtra().body.msg.datPkt, sizeof(bc7215DataMaxPkt_t));
    }
    savedData.end();
}

/*
 * Display flashing "NOT CONNECTED" message when BC7215 is not responding
 */
void flashNoConn()
{
    static bool showMessage = true;
    
    if (timerMs >= 1000)
    {
        timerMs = 0;
        showMessage = !showMessage;  // Toggle display state
    }
    
    if (showMessage)
    {
        // Show error message
        tft.setTextColor(COLOR_TEXT, COLOR_BG);
        String str = "BC7215A";
        tft.drawString(str, SCREEN_W / 2, SCREEN_H / 2 - tft.fontHeight());
        str = "NOT CONNECTED.";
        tft.drawString(str, SCREEN_W / 2, SCREEN_H / 2 + 4);
    }
    else
    {
        clearCentralArea();
    }
    
    timerMs += interval;
}


/*
 * Button state machine and event detection
 * Handles debouncing, long press detection, and multi-button combinations
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
        // No buttons pressed - wait for button press
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
        // Single button pressed - check for long press or second button
        if ((leftPressed && previousLeft) || (rightPressed && previousRight))
        {
            keyTimer += interval;
            if (keyTimer > 2000)           // 2 second threshold for long press
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
            keyTimer = 0;                  // Reset timer on button change
        }
        
        if (leftPressed && rightPressed)
        {
            keyTimer = 0;
            keyboardState = BOTH_PRESSED;
        }
        else if (!leftPressed && !rightPressed)
        {
            // Button released - generate short press event
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
        // Both buttons pressed - check for long press
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
        // One button released while other still pressed
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
        // Long press detected - wait for release
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
 * DISPLAY AND UI FUNCTIONS
 * 
 * These functions handle all display operations including:
 * - Top status badges (WiFi, MQTT, IR)
 * - Main number display
 * - Mode and fan labels
 * - Button layouts
 * - Menu systems
 * - Initialization screens
 ***********************************************************************************/

// ======= Top Badge Layout (Equal distribution, uniform size) =======
#define BADGE_FONT        1              // Font size for badges
#define BADGE_SIZE        1              // Text scale for badges
#define BADGE_RADIUS      2              // Corner radius for badges
#define BADGE_Y           6              // Y position of badges
#define BADGE_HEIGHT      12             // Badge height (text + ~2px top/bottom)
#define BADGE_SIDE_MARGIN 3              // Left/right margins
#define BADGE_GAP         6              // Gap between badges
#define BADGE_WIDTH       ((SCREEN_W - 2 * BADGE_SIDE_MARGIN - 2 * BADGE_GAP) / 3)

// ======= Layout Parameters (Essential positioning only) =======
struct Layout
{
    // Top three badge X positions
    int badgeX0, badgeX1, badgeX2;

    // Central large number display
    int numberY = 50;
    int numberAreaY = 40;
    int numberAreaH = 60;

    // Middle left/right labels
    int middleLabelY = 145;
    int middleLabelPad = 8;
} L;

/*
 * Calculate equal distribution positions for top badges
 */
void computeTopBadgesLayout()
{
    L.badgeX0 = BADGE_SIDE_MARGIN;
    L.badgeX1 = L.badgeX0 + BADGE_WIDTH + BADGE_GAP;
    L.badgeX2 = L.badgeX1 + BADGE_WIDTH + BADGE_GAP;
}

/*
 * Draw a top status badge with centered text
 */
void drawBadge(int xStart, uint16_t bg, const char* text)
{
    tft.fillRoundRect(xStart, BADGE_Y, BADGE_WIDTH, BADGE_HEIGHT, BADGE_RADIUS, bg);
    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(BADGE_FONT);
    tft.setTextSize(BADGE_SIZE);
    tft.setTextColor(COLOR_BADGE_FG);
    
    // Center text in badge
    int tw = tft.textWidth(text);
    int fh = tft.fontHeight();
    int tx = xStart + (BADGE_WIDTH - tw) / 2;
    int ty = BADGE_Y + (BADGE_HEIGHT - fh) / 2;
    tft.drawString(text, tx, ty);
    tft.setTextDatum(TC_DATUM);
}

/*
 * Clear the main number display area
 */
void clearNumberArea() 
{ 
    tft.fillRect(0, L.numberAreaY, SCREEN_W, L.numberAreaH, COLOR_BG); 
}

/*
 * Display large temperature number in center of screen
 */
void drawBigNumber(const String& value)
{
    clearNumberArea();
    tft.setTextFont(8);                    // Large font for temperature
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.drawString(value, SCREEN_W / 2, L.numberY);
}

/*
 * Display AC mode label on left side
 */
void drawModeLabel(int mode)
{
    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_BG, COLOR_LABEL_BG);
    int y = L.middleLabelY;
    tft.fillRect(0, y - 2, SCREEN_W / 2, tft.fontHeight() + 8, COLOR_BG);
    tft.drawString(MODES[mode], L.middleLabelPad, y);
    tft.setTextDatum(TC_DATUM);
}

/*
 * Display fan speed label on right side
 */
void drawFanLabel(int fan)
{
    tft.setTextDatum(TR_DATUM);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_BG, COLOR_LABEL_BG);
    int y = L.middleLabelY;
    tft.fillRect(SCREEN_W / 2, y - 2, SCREEN_W / 2, tft.fontHeight() + 8, COLOR_BG);
    tft.drawString(FANSPEED[fan], SCREEN_W - L.middleLabelPad, y);
    tft.setTextDatum(TC_DATUM);
}

/*
 * Clear the central display area (between badges and buttons)
 */
void clearCentralArea() 
{ 
    tft.fillRect(0, L.numberAreaY, SCREEN_W, 177 - L.numberAreaY, COLOR_BG); 
}

/*
 * Draw temperature control buttons (- and + with MODE/FAN labels)
 */
void drawTempButtons()
{
    // Left button (TEMP -)
    tft.fillRoundRect(4, 178, 61, 58, 8, COLOR_ACTION_BG);
    // Right button (TEMP +)
    tft.fillRoundRect(70, 178, 61, 58, 8, COLOR_ACTION_BG);
    
    // Button labels
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_ACTION_FG);
    tft.drawString("MODE", 34, 216);       // Long press left = mode
    tft.drawString("FAN", 100, 216);       // Long press right = fan
    
    // Button symbols
    tft.setTextFont(4);
    tft.setTextSize(3);
    tft.setTextColor(COLOR_ACTION_FG);
    tft.drawString("-", 34, 160);          // Decrease temperature
    tft.drawString("+", 100, 160);         // Increase temperature
}

/*
 * Draw menu navigation buttons
 */
void drawMenuButtons()
{
    drawSELButton();
    drawOKButton();
}

/*
 * Draw OK button (right side)
 */
void drawOKButton()
{
    tft.fillRoundRect(70, 178, 61, 58, 8, COLOR_ACTION_BG);
    tft.setTextFont(4);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_ACTION_FG);
    tft.drawString("OK", 100, 180 + (58 - tft.fontHeight()) / 2);
}

/*
 * Draw ESC (escape) button (left side)
 */
void drawEscButton()
{
    tft.fillRoundRect(4, 178, 61, 58, 8, COLOR_ACTION_BG);
    tft.setTextFont(4);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_ACTION_FG);
    tft.drawString("ESC", 34, 180 + (58 - tft.fontHeight()) / 2);
}

/*
 * Draw SEL (select) button (left side)
 */
void drawSELButton()
{
    tft.fillRoundRect(4, 178, 61, 58, 8, COLOR_ACTION_BG);
    tft.setTextFont(4);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_ACTION_FG);
    tft.drawString("SEL", 34, 180 + (58 - tft.fontHeight()) / 2);
}

/*
 * Draw power control buttons (ON/OFF)
 */
void drawOnOffButtons()
{
    // Left button (ON)
    tft.fillRoundRect(4, 178, 61, 58, 8, COLOR_ACTION_BG);
    // Right button (OFF)
    tft.fillRoundRect(70, 178, 61, 58, 8, COLOR_ACTION_BG);
    
    tft.setTextFont(4);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_ACTION_FG);
    tft.drawString("ON", 34, 180 + (58 - tft.fontHeight()) / 2);
    tft.drawString("OFF", 100, 180 + (58 - tft.fontHeight()) / 2);
}

/*
 * Clear left button area
 */
void clearLeftBtn()
{
    tft.fillRoundRect(4, 178, 61, 58, 8, COLOR_BG);
}

/*
 * Clear right button area
 */
void clearRightBtn()
{
    tft.fillRoundRect(70, 178, 61, 58, 8, COLOR_BG);
}

/*
 * Display a menu with list of items
 */
void showMenu(const char* items[], int cnt)
{
    menuItems = items;
    clearCentralArea();
    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.drawString("Menu :", 4, L.numberAreaY);
    
    // Display all menu items
    for (int i = 0; i < cnt; i++)
    {
        tft.drawString(" " + String(menuItems[i]) + " ", 8, L.numberAreaY + (i + 1) * tft.fontHeight());
    }
    currentMenuSelection = 0;
    
    // Display library version at bottom
    tft.setTextFont(1);
    tft.drawString(String(ac.getLibVer()), 2, 177 - tft.fontHeight());
    tft.setTextDatum(TC_DATUM);
}

/*
 * Update menu selection highlighting
 */
void updateMenu(int selected)
{
    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(2);
    tft.setTextSize(1);
    
    // Clear previous selection
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.drawString(" " + String(menuItems[currentMenuSelection]) + " ", 8,
        L.numberAreaY + (currentMenuSelection + 1) * tft.fontHeight());
    
    // Highlight new selection
    tft.setTextColor(COLOR_TEXT, COLOR_ACTION_BG);
    tft.drawString(" " + String(menuItems[selected]) + " ", 8, L.numberAreaY + (selected + 1) * tft.fontHeight());
    
    currentMenuSelection = selected;
    tft.setTextDatum(TC_DATUM);
}

/*
 * Status badge drawing functions
 */
void drawIrBadge(uint16_t color) 
{ 
    drawBadge(BADGE_WIDTH * 2 + BADGE_GAP * 2 + BADGE_SIDE_MARGIN, color, " IR "); 
}


/*
 * Initialization screen functions
 */

/*
 * Show initial setup instruction screen
 */
void showInitScrn1()
{
    clearCentralArea();
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.drawString("  Capture & Init.", SCREEN_W / 2, L.numberAreaY);
    tft.drawString("Set your remote to", SCREEN_W / 2, L.numberAreaY + tft.fontHeight());
    tft.drawString("Cooling mode, 25'C", SCREEN_W / 2, L.numberAreaY + tft.fontHeight() * 3);
    tft.drawString("Press OK when ready", SCREEN_W / 2, L.numberAreaY + tft.fontHeight() * 5);
}

/*
 * Show IR signal capture instruction screen
 */
void showInitScrn2(const String& keyText)
{
    clearCentralArea();
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.drawString("Now point IR receiver", SCREEN_W / 2, L.numberAreaY);
    tft.drawString("and press ", SCREEN_W / 2, L.numberAreaY + tft.fontHeight());
    tft.drawString(keyText, SCREEN_W / 2, L.numberAreaY + tft.fontHeight() * 3);
    tft.drawString("to capture signal", SCREEN_W / 2, L.numberAreaY + tft.fontHeight() * 5);
}

/*
 * Show additional capture required screen
 */
void showInitScrn3(const String& keyText)
{
    clearCentralArea();
    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.drawString("Need 1 more capture", 4, L.numberAreaY);
    tft.drawString("for button:", 4, L.numberAreaY + tft.fontHeight());
    tft.drawString(keyText, SCREEN_W / 2 - tft.textWidth(keyText) / 2, L.numberAreaY + tft.fontHeight() * 3);
    tft.drawString("OK to start capture", 4, L.numberAreaY + tft.fontHeight() * 5);
    tft.setTextDatum(TC_DATUM);
}

/*
 * Show initialization failure message
 */
void showInitScrn4()
{
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_BG, COLOR_TEXT);
    tft.drawString("  Init FAIL...  ", SCREEN_W / 2, L.numberAreaY);
    tft.drawString("Check remote setting", SCREEN_W / 2, L.numberAreaY + tft.fontHeight());
    tft.drawString("Try again...", SCREEN_W / 2, L.numberAreaY + tft.fontHeight() * 2);
}

/*
 * Show initialization success message
 */
void showInitScrn5()
{
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_BG, COLOR_TEXT);
    tft.drawString("  Init OK!  ", SCREEN_W / 2, L.numberAreaY);
    tft.drawString("Can Control A/C Now", SCREEN_W / 2, L.numberAreaY + tft.fontHeight());
}

/*
 * Placeholder function (unused)
 */
void showInitScrn6() { }

/*
 * Display initialization success screen with OK button
 */
void showInitOKMsg()
{
    clearCentralArea();
    drawOKButton();
    showInitScrn5();        // Show success message
    clearLeftBtn();
}

/*
 * Display initialization failure screen with retry option
 */
void showInitFailMsg()
{
    clearCentralArea();
    drawOKButton();
    showInitScrn4();        // Show failure message
}

/*
 * Display successful match found message
 */
void showNextMatchScrn(int8_t matchCnt)
{
    clearCentralArea();
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.drawString("Match Found !", SCREEN_W / 2, L.numberAreaY);
    tft.drawString("Count : " + String(matchCnt), SCREEN_W / 2, L.numberAreaY + tft.fontHeight());
    tft.drawString("OK to continue", SCREEN_W / 2, L.numberAreaY + tft.fontHeight() * 2);
}

/*
 * Display no more matches found message
 */
void showNextFailScrn()
{
    clearCentralArea();
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.drawString("No match Found !", SCREEN_W / 2, L.numberAreaY);
    tft.drawString("Program will restart ", SCREEN_W / 2, L.numberAreaY + tft.fontHeight());
    tft.drawString("OK to continue", SCREEN_W / 2, L.numberAreaY + tft.fontHeight() * 2);
}

/*
 * Display not initialized error message
 */
void showNoInitScrn()
{
    clearCentralArea();
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.drawString("Not Initialized !", SCREEN_W / 2, L.numberAreaY);
    tft.drawString("Init. the library first ", SCREEN_W / 2, L.numberAreaY + tft.fontHeight());
    tft.drawString("OK to continue", SCREEN_W / 2, L.numberAreaY + tft.fontHeight() * 2);
}
