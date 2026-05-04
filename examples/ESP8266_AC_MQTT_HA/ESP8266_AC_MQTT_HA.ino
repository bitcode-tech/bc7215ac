/*
 * ESP8266 NodeMCU Air Conditioner Control with MQTT (Headless)
 *
 * Ported from the ESP32 + TTGO T-Display version.
 * - No display: status is shown by the on-board LED (GPIO2 / D4, active LOW).
 * - Local control is done via the FLASH button (GPIO0 / D3).
 *
 * LED behaviour:
 *   - Pairing   : continuous blink (~250 ms period)
 *   - Working   : short flash every 3 s (parsing heartbeat)
 *   - IR TX/RX  : solid ON while sending or while a received signal is parsed
 *   - Otherwise : OFF
 *
 * Button behaviour:
 *   - Long press FLASH button (>= 2 s): enter pairing mode.
 *     Device stays in pairing mode until a valid AC remote signal is learned.
 *
 * Work flow:
 *   power-up ─► load config from EEPROM
 *            ├─► found  ─► WORKING (parsing + MQTT control)
 *            └─► none   ─► PAIRING (wait for IR)
 *   WORKING + MQTT cmd arrives ─► stop parsing ─► send IR ─► back to WORKING
 *   WORKING + long-press FLASH ─► stop parsing ─► PAIRING
 *
 * --------------------------------------------------------------------------
 *  NOTE on BC7215 serial:
 *  ESP8266 has only one full hardware UART, which is normally reserved for
 *  debug output. We therefore use SoftwareSerial (EspSoftwareSerial library)
 *  to talk to the BC7215. If your BC7215 library's constructor only accepts
 *  `HardwareSerial&`, do one of the following instead:
 *
 *    (A) Comment out the SoftwareSerial block and use Serial.swap():
 *          Serial.begin(19200, SERIAL_8N2);  // UART0 on D9/D10
 *          Serial.swap();                    // move UART0 to D7 (RX) / D8 (TX)
 *          BC7215 bc7215Board(Serial, BC7215_MOD, BC7215_BUSY);
 *        Caveat: GPIO15 (D8) must be held LOW at boot; a TX line idling HIGH
 *        can interfere with startup. Add an external pull-down if needed.
 *
 *    (B) Update the BC7215 library to take a `Stream&` reference and the
 *        SoftwareSerial path below will work unchanged.
 * --------------------------------------------------------------------------
 */

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <bc7215.h>
#include <bc7215ac.h>

// ======= WiFi / MQTT / Home Assistant configuration =======
//#define MY_WIFI_SSID     "******"                   // Replace with your WiFi SSID
//#define MY_WIFI_PASSWORD "*************"            // Replace with your WiFi password
//#define MY_MQTT_IP_ADDR  "***.***.***.***"		  // Replace with your MQTT	broker IP address
#define MY_UUID          "bc7215_ac_01"             // Unique device id
#define HA_BASE          "home/ac/bc7215"

#if !defined(MY_WIFI_SSID) || !defined(MY_WIFI_PASSWORD) || !defined(MY_UUID)
#   error "Please configure MY_WIFI_SSID / MY_WIFI_PASSWORD / MY_UUID"
#endif

// ======= Pin assignment (NodeMCU) =======
// NOTE: D0 (GPIO16) is a special pin on ESP8266 — no internal pull-up, no
// interrupts, separate register. It is OK to use it as SoftwareSerial TX
// (output only) with recent versions of EspSoftwareSerial, but it cannot
// be used as RX. Here we use D0 only as TX, so it is safe.
#define PIN_BUTTON       0          // D3, FLASH button, active LOW (has pull-up)
#define PIN_LED          2          // D4, on-board LED, active LOW (LED_BUILTIN)
#define BC7215_MOD       14         // D5
#define BC7215_BUSY      4          // D2
#define BC7215_PORT_RX   5          // D1, ESP RX  <- BC7215 TX
#define BC7215_PORT_TX   16         // D0, ESP TX  -> BC7215 RX

// LED helpers (on-board LED is active LOW)
#define LED_ON()     digitalWrite(PIN_LED, LOW)
#define LED_OFF()    digitalWrite(PIN_LED, HIGH)
#define LED_TOGGLE() digitalWrite(PIN_LED, !digitalRead(PIN_LED))

// ======= MQTT topics =======
const char*    WIFI_SSID         = MY_WIFI_SSID;
const char*    WIFI_PASSWORD     = MY_WIFI_PASSWORD;
const char*    MQTT_HOST         = MY_MQTT_IP_ADDR;	
const uint16_t MQTT_PORT         = 1883;
const char*    MQTT_CLIENT_ID    = MY_UUID;
const char*    MQTT_LWT          = HA_BASE "/available";
const char*    TEMP_SET_TOPIC    = HA_BASE "/temperature/set";
const char*    TEMP_STATE_TOPIC  = HA_BASE "/temperature/state";
const char*    MODE_SET_TOPIC    = HA_BASE "/mode/set";
const char*    MODE_STATE_TOPIC  = HA_BASE "/mode/state";
const char*    FAN_SET_TOPIC     = HA_BASE "/fan/set";
const char*    FAN_STATE_TOPIC   = HA_BASE "/fan/state";
const char*    HA_MODES[]        = { "auto", "cool", "heat", "dry", "fan_only" };
const char*    HA_FANSPEED[]     = { "auto", "low", "medium", "high" };
const char*    HA_DISCOVERY_TOPIC = "homeassistant/climate/bc7215_ac/config";

const char* HA_DISCOVERY_INFO_C = R"(
{
    "name": "Bedroom AC",
    "unique_id": "bc7215_ac_01",
    "~": "home/ac/bc7215",
    "mode_command_topic": "~/mode/set",
    "mode_state_topic": "~/mode/state",
    "modes": ["off", "auto", "cool", "heat", "dry", "fan_only"],
    "temperature_command_topic": "~/temperature/set",
    "temperature_state_topic": "~/temperature/state",
    "min_temp": 16,
    "max_temp": 30,
    "temp_step": 1,
    "fan_mode_command_topic": "~/fan/set",
    "fan_mode_state_topic": "~/fan/state",
    "fan_modes": ["auto", "low", "medium", "high"],
    "availability_topic": "~/available",
    "payload_available": "online",
    "payload_not_available": "offline",
    "device":
    {
        "identifiers": ["bc7215_ac_01"],
        "name": "BC7215_AC_CONTROLLER",
        "manufacturer": "DIY",
        "model": "ESP8266_BC7215_AC"
    }
})";
const char*	HA_DISCOVERY_INFO_F = R"(
{
	"name": "Bedrood AC",
	"unique_id": "bc7215_ac_01",
	"~": "home/ac/bc7215",
	"mode_command_topic": "~/mode/set",
	"mode_state_topic": "~/mode/state",
	"modes": ["off", "auto", "cool", "heat", "dry", "fan_only"],
	"temperature_command_topic": "~/temperature/set",
	"temperature_state_topic": "~/temperature/state",
	"temperature_unit": "F",
	"min_temp": 60,
	"max_temp": 88,
	"temp_step": 1,
	"fan_mode_command_topic": "~/fan/set",
	"fan_mode_state_topic": "~/fan/state",
	"fan_modes": ["auto", "low", "medium", "high"],
	"availability_topic": "~/available",
	"payload_available": "online",
	"payload_not_available": "offline",
	"device": 
	{
		"identifiers": ["bc7215_ac_01"],
		"name": "BC7215_AC_CONTROLLER",
		"manufacturer": "DIY",
		"model": "ESP32_BC7215_AC"
	}
})";

// ======= State machine =======
enum L1_STATE
{
    START,
    PAIRING,
    WORKING,
    IR_SENDING,
    NOT_CONNECTED
};

enum L2_STATE
{
    STEP1,
    STEP2,
    STEP3
};

enum NET_STATUS
{
    WAITING,
    CONNECTING,
    CONNECTED,
    UNAVAILABLE
};

// ======= EEPROM layout =======
#define EEPROM_MAGIC  0xBC72A001UL
#define EEPROM_SIZE   1024               // plenty for FormatPkt + DataMaxPkt

struct SavedConfig
{
    uint32_t            magic;
    bool                isCelsius;
    int8_t              matchCnt;
    bc7215FormatPkt_t   irFormat;
    bc7215DataMaxPkt_t  irData;
};

// ======= Global objects =======
SoftwareSerial bc7215Serial;
BC7215         bc7215Board(bc7215Serial, BC7215_MOD, BC7215_BUSY);
BC7215AC       ac(bc7215Board);

WiFiClient     espWiFi;
PubSubClient   mqtt(espWiFi);

// ======= Global state =======
L1_STATE   mainState;
L1_STATE   retState;          // state to return to after IR_SENDING
L2_STATE   l2State;
NET_STATUS wifiState;
NET_STATUS mqttState;

// AC control variables
int    temp      = 25;        // current temperature setpoint
int    mode      = 1;         // 0=auto 1=cool 2=heat 3=dry 4=fan
int    fan       = 1;         // 0=auto 1=low 2=med 3=high
int    remoteBtn = 0;         // last button: 0=Temp- 1=Temp+ 2=Mode 3=Fan
int8_t matchCnt  = 0;
bool   acPowerOn = true;
bool   mqttCmd   = false;
bool   irSending = false;

// Timing
unsigned long startTime        = 0;
unsigned long wifiStartTime    = 0;
unsigned long mqttStartTime    = 0;
unsigned long ledBlinkTime     = 0;
unsigned long heartbeatBaseTime = 0;  // "phase-0" of the 3-second heartbeat cycle
unsigned long noConnBlinkTime  = 0;

// Button
bool          btnLast         = false;
unsigned long btnPressedTime  = 0;
bool          longPressHandled = false;

// Scratch buffers for loading AC data from EEPROM
bc7215DataMaxPkt_t  irData;
bc7215FormatPkt_t   irFormat;

const int interval = 10;      // main loop tick in ms

// Forward declarations
void powerup();
void doPairing();
void doWorking();
void doIrSending();
void doNotConnected();
void wifiConnect();
void mqttConnect();
void mqttOnlineAction();
void processMqtt(char* topic, byte* payload, unsigned int length);
void updateButton();
void enterPairing();
void enterWorking();
bool loadInitInfo();
void saveInitInfo();
void acStatPublish();
void handleHeartbeat(unsigned long now);

// =====================================================================
// Arduino entry points
// =====================================================================
void setup()
{
    Serial.begin(115200);
    Serial.println();
    Serial.println(F("[BOOT] ESP8266 BC7215 AC Controller"));

    // GPIO setup
    pinMode(PIN_LED, OUTPUT);
    LED_OFF();
    pinMode(PIN_BUTTON, INPUT_PULLUP);

    // Serial to BC7215 (SoftwareSerial, 19200 8N2)
    bc7215Serial.begin(19200, SWSERIAL_8N2, BC7215_PORT_RX, BC7215_PORT_TX);
    Serial.println(F("[BOOT] BC7215 serial initialised"));

    // Persistent storage
    EEPROM.begin(EEPROM_SIZE);

    // Initial state
    mainState = START;
    l2State   = STEP1;

	ac.setCelsius();
	// ac.setFahrenheit();		// use this line instead if your AC is Fahrenheit.
}

void loop()
{
    switch (mainState)
    {
    case START:          powerup();          break;
    case PAIRING:        doPairing();        break;
    case WORKING:        doWorking();        break;
    case IR_SENDING:     doIrSending();      break;
    case NOT_CONNECTED:  doNotConnected();   break;
    default: break;
    }

    wifiConnect();
    mqttConnect();
    updateButton();

    delay(interval);
}

// =====================================================================
// START: hardware/network bring-up, then jump to WORKING or PAIRING
// =====================================================================
void powerup()
{
    switch (l2State)
    {
    case STEP1:
    {
        // Wake / probe BC7215
        bc7215Board.setRx();        // wake up BC7215A from shutdown
        delay(50);
        bc7215Board.setTx();
        delay(50);
        bc7215Board.setShutDown();  // used as a presence probe

        startTime = millis();

        // Kick off WiFi + MQTT
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        mqtt.setServer(MQTT_HOST, MQTT_PORT);
        mqtt.setBufferSize(1024);
        mqtt.setCallback(processMqtt);
        wifiStartTime = millis();
        wifiState = CONNECTING;
        mqttState = WAITING;

        l2State = STEP2;
        break;
    }

    case STEP2:
        // Wait for BC7215 to respond to the shutdown command
        if (!bc7215Board.isBusy())
        {
            bc7215Board.setRx();      // wake it back up
            delay(50);
            bc7215Board.setTx();
            l2State = STEP3;
        }
        else if (millis() - startTime > 1000)
        {
            Serial.println(F("[BOOT] BC7215A NOT DETECTED"));
            mainState = NOT_CONNECTED;
            startTime = millis();
        }
        break;

    case STEP3:
        // Try to restore a previously learned remote
        if (loadInitInfo() && ac.init(irData, irFormat))
        {
            // Navigate to the stored match index (if > 0)
            if (matchCnt > 0)
            {
                bool result = false;
                for (int i = 0; i < matchCnt; i++)
                {
                    result = ac.matchNext();
                }
                if (!result)
                {
                    ac.init(irData, irFormat);
                    matchCnt = 0;
                }
            }

            temp = ac.isCelsius() ? 25 : 78;
            mode = 1;
            fan  = 1;
            Serial.println(F("[BOOT] Saved remote loaded, entering WORKING"));
            enterWorking();
        }
        else
        {
            Serial.println(F("[BOOT] No saved remote, entering PAIRING"));
            enterPairing();
        }
        break;

    default:
        break;
    }
}

// =====================================================================
// PAIRING: LED blinks continuously, wait for IR signal from remote
// =====================================================================
void doPairing()
{
    unsigned long now = millis();

    // 250 ms LED toggle
    if (now - ledBlinkTime >= 250)
    {
        ledBlinkTime = now;
        LED_TOGGLE();
    }

    // Wait for the user to press a button on their AC remote
    if (ac.signalCaptured())
    {
        ac.stopCapture();

        if (ac.init())
        {
            Serial.println(F("[PAIR] Pairing OK"));
            matchCnt = 0;
            saveInitInfo();

            // Reset default setpoints after pairing
            temp = ac.isCelsius() ? 25 : 78;
            mode = 1;
            fan  = 1;

            // Flash LED "OFF" clearly so user sees the transition
            LED_OFF();

            // Re-announce to Home Assistant (in case it was down at boot)
            if (mqttState == CONNECTED)
            {
				mqttOnlineAction();
                acStatPublish();
            }

            enterWorking();
        }
        else
        {
            // Pairing failed -> stay in PAIRING and listen again
            Serial.println(F("[PAIR] Pairing failed, restarting capture"));
            ac.startCapture();
        }
    }
}

// =====================================================================
// WORKING: parse received IR, publish to MQTT, heartbeat blink
// =====================================================================
void doWorking()
{
    unsigned long now = millis();

    handleHeartbeat(now);

    // IR signal received?
    if (ac.signalCaptured())
    {
        LED_ON();                    // indicate RX activity
        ac.stopCapture();

        int T = -1, M = -1, F = -1, P = -1;
        if (ac.parse(T, M, F, P))
        {
            Serial.printf("[RX] T=%d M=%d F=%d P=%d\n", T, M, F, P);

            // Temperature (accept only in-range values)
            bool tempOK = (ac.isCelsius()  && (T >= 16) && (T <= 30)) ||
                          (!ac.isCelsius() && (T >= 60) && (T <= 88));
            if (tempOK && mqttState == CONNECTED)
            {
                mqtt.publish(TEMP_STATE_TOPIC, String(T).c_str());
                temp = T;
            }

            // Mode
            if (M >= 0 && M <= 4)
            {
                mode = M;
                if (mqttState == CONNECTED)
                {
                    mqtt.publish(MODE_STATE_TOPIC, HA_MODES[M]);
                }
            }

            // Fan speed
            if (F >= 0 && F <= 3)
            {
                fan = F;
                if (mqttState == CONNECTED)
                {
                    mqtt.publish(FAN_STATE_TOPIC, HA_FANSPEED[F]);
                }
            }

            // Power (0=off, 1=on)
            if (P == 0)
            {
                acPowerOn = false;
                if (mqttState == CONNECTED)
                {
                    mqtt.publish(MODE_STATE_TOPIC, "off");
                }
            }
            else if (P == 1)
            {
                acPowerOn = true;
            }
        }
        else
        {
            Serial.println(F("[RX] Parse failed"));
        }

        LED_OFF();
        ac.startCapture();                   // resume listening
        heartbeatBaseTime = millis();        // resync heartbeat so it doesn't flash immediately
    }
}

// =====================================================================
// Heartbeat: 3-second cycle, short flash at the start of each cycle
// =====================================================================
void handleHeartbeat(unsigned long now)
{
    const unsigned long PERIOD    = 3000;     // 3 s
    const unsigned long FLASH_LEN = 80;       // LED ON duration, ms

    unsigned long phase = (now - heartbeatBaseTime) % PERIOD;
    if (phase < FLASH_LEN)
    {
        LED_ON();
    }
    else
    {
        LED_OFF();
    }
}

// =====================================================================
// IR_SENDING: wait until BC7215 is done transmitting, then return
// =====================================================================
void doIrSending()
{
    // While sending, LED stays on (set when we entered this state)
    if (!ac.isBusy() || (millis() - startTime > 3000))
    {
        LED_OFF();
        irSending = false;
        mqttCmd   = false;

        mainState = retState;

        if (mainState == WORKING)
        {
            // Re-enter parsing mode
            ac.startCapture();
            heartbeatBaseTime = millis();
        }
    }
}

// =====================================================================
// NOT_CONNECTED: BC7215 not detected. Slow LED blink as an error code.
// =====================================================================
void doNotConnected()
{
    unsigned long now = millis();
    if (now - noConnBlinkTime >= 500)
    {
        noConnBlinkTime = now;
        LED_TOGGLE();
    }
}

// =====================================================================
// Entering PAIRING / WORKING - common setup for each state
// =====================================================================
void enterPairing()
{
    mainState    = PAIRING;
    ledBlinkTime = millis();
    LED_OFF();
    ac.startCapture();
}

void enterWorking()
{
    mainState         = WORKING;
    heartbeatBaseTime = millis();
    LED_OFF();
    ac.startCapture();
}

// =====================================================================
// WiFi connection state machine
// =====================================================================
void wifiConnect()
{
    static int connTimes = 0;

    switch (wifiState)
    {
    case CONNECTING:
        if (millis() - wifiStartTime >= 300)
        {
            wifiStartTime = millis();
            connTimes++;
            if (connTimes >= 30)        // ~9 s
            {
                wifiState = UNAVAILABLE;
                Serial.println(F("[WIFI] Unavailable"));
            }
        }
        if (WiFi.status() == WL_CONNECTED)
        {
            wifiState = CONNECTED;
            Serial.print(F("[WIFI] Connected, IP="));
            Serial.println(WiFi.localIP());
            connTimes = 0;
        }
        break;

    case CONNECTED:
        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println(F("[WIFI] Lost"));
            wifiState     = CONNECTING;
            mqttState     = WAITING;
            wifiStartTime = millis();
            connTimes     = 0;
        }
        break;

    case UNAVAILABLE:
        // One-shot retry every ~60 s
        if (millis() - wifiStartTime >= 60000)
        {
            Serial.println(F("[WIFI] Retry"));
            WiFi.disconnect();
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
            wifiState     = CONNECTING;
            wifiStartTime = millis();
            connTimes     = 0;
        }
        break;

    default:
        break;
    }
}

// =====================================================================
// MQTT connection state machine
// =====================================================================
void mqttConnect()
{
    switch (mqttState)
    {
    case WAITING:
        if ((wifiState == CONNECTED) && ac.initOK)
        {
            mqttState      = CONNECTING;
            mqttStartTime  = millis();
        }
        else if (wifiState == UNAVAILABLE)
        {
            mqttState = UNAVAILABLE;
        }
        break;

    case CONNECTING:
        Serial.println(F("[MQTT] Connecting"));
        if (mqtt.connect(MQTT_CLIENT_ID, NULL, NULL, MQTT_LWT, 0, true, "offline"))
        {
            mqttOnlineAction();
            mqttState = CONNECTED;
        }
        else
        {
            mqttStartTime = millis();
            while (millis() - mqttStartTime < 2000)
            {
                yield();        // keep WiFi stack happy while waiting
            }
            Serial.println(F("[MQTT] 2nd attempt"));
            if (mqtt.connect(MQTT_CLIENT_ID, NULL, NULL, MQTT_LWT, 0, true, "offline"))
            {
                mqttOnlineAction();
                mqttState = CONNECTED;
            }
            else
            {
                mqttState     = UNAVAILABLE;
                mqttStartTime = millis();
            }
        }
        break;

    case CONNECTED:
        if (!mqtt.connected())
        {
            Serial.println(F("[MQTT] Disconnected"));
            mqttState = CONNECTING;
        }
        else
        {
            mqtt.loop();
        }
        break;

    case UNAVAILABLE:
        // Retry every ~30 s, as long as WiFi is up
        if (wifiState == CONNECTED && millis() - mqttStartTime >= 30000)
        {
            mqttState     = CONNECTING;
            mqttStartTime = millis();
        }
        break;

    default:
        break;
    }
}

// =====================================================================
// Actions performed once MQTT comes online
// =====================================================================
void mqttOnlineAction()
{
	bool result;
    Serial.println(F("[MQTT] Online"));
	if (ac.isCelsius())
	{
		result = mqtt.publish(HA_DISCOVERY_TOPIC, HA_DISCOVERY_INFO_C, true);
	}
	else
	{
		result = mqtt.publish(HA_DISCOVERY_TOPIC, HA_DISCOVERY_INFO_F, true);
	}
	if (result)
    {
        Serial.println(F("[MQTT] HA discovery published"));
    }
    mqtt.publish(MQTT_LWT, "online", true);

    if (ac.initOK)
    {
        acStatPublish();
    }

    mqtt.subscribe(TEMP_SET_TOPIC);
    mqtt.subscribe(MODE_SET_TOPIC);
    mqtt.subscribe(FAN_SET_TOPIC);
}

// =====================================================================
// Publish current temp/mode/fan to MQTT
// =====================================================================
void acStatPublish()
{
    if (mqttState != CONNECTED) return;
    mqtt.publish(TEMP_STATE_TOPIC, String(temp).c_str());
    mqtt.publish(MODE_STATE_TOPIC, HA_MODES[mode]);
    mqtt.publish(FAN_STATE_TOPIC,  HA_FANSPEED[fan]);
	Serial.println("[MQTT] AC state published");
}

// =====================================================================
// MQTT incoming message callback
// Any command while in WORKING state:
//   stop parsing -> send IR -> IR_SENDING (LED on) -> back to WORKING
// Commands are ignored while pairing / sending / not-connected.
// =====================================================================
void processMqtt(char* topic, byte* payload, unsigned int length)
{
    Serial.print(F("[MQTT] Topic: "));
    Serial.println(topic);

    // Only accept commands while we're actively working
    if (mainState != WORKING) return;
    if (!ac.initOK)           return;

    char val[length + 1];
    memcpy(val, payload, length);
    val[length] = '\0';
    int value = atoi(val);

    bool doSend = false;
    bool specialPower = false;        // separate "on()" / "off()" vs. "setTo()"
    bool powerOn = false;

    if (strcmp(topic, TEMP_SET_TOPIC) == 0)
    {
        bool tempOK = (ac.isCelsius()  && (value >= 16) && (value <= 30)) ||
                      (!ac.isCelsius() && (value >= 60) && (value <= 88));
        if (tempOK)
        {
            temp      = value;
            remoteBtn = (value > temp) ? 1 : 0;     // cosmetic; safe default
            doSend    = true;
            Serial.printf("[MQTT] Set temp=%d\n", temp);
        }
        else
        {
            Serial.println(F("[MQTT] Invalid temperature"));
        }
    }
    else if (strcmp(topic, MODE_SET_TOPIC) == 0)
    {
        if (strcmp(val, "off") == 0)
        {
            specialPower = true;
            powerOn      = false;
            acPowerOn    = false;
            if (mqttState == CONNECTED)
            {
                mqtt.publish(MODE_STATE_TOPIC, "off");
            }
            Serial.println(F("[MQTT] Power OFF"));
        }
        else
        {
            int i;
            for (i = 0; i < 5; i++)
            {
                if (strcmp(val, HA_MODES[i]) == 0)
                {
                    mode = i;
                    break;
                }
            }
            if (i < 5)
            {
                if (!acPowerOn)
                {
                    specialPower = true;
                    powerOn      = true;
                    acPowerOn    = true;
                    Serial.println(F("[MQTT] Power ON"));
                }
                else
                {
                    remoteBtn = 2;                  // MODE button
                    doSend    = true;
                    Serial.printf("[MQTT] Set mode=%s\n", HA_MODES[mode]);
                }
            }
        }
    }
    else if (strcmp(topic, FAN_SET_TOPIC) == 0)
    {
        int i;
        for (i = 0; i < 4; i++)
        {
            if (strcmp(val, HA_FANSPEED[i]) == 0)
            {
                fan = i;
                break;
            }
        }
        if (i < 4)
        {
            remoteBtn = 3;                          // FAN button
            doSend    = true;
            Serial.printf("[MQTT] Set fan=%s\n", HA_FANSPEED[fan]);
        }
        else
        {
            Serial.println(F("[MQTT] Invalid fan value"));
        }
    }

    if (!doSend && !specialPower) return;

    // Leave parsing mode, start transmit, switch to IR_SENDING
    ac.stopCapture();
    LED_ON();                           // TX indicator (steady while sending)

    if (specialPower)
    {
        if (powerOn) ac.on();
        else         ac.off();
    }
    else
    {
        ac.setTo(temp, mode, fan, remoteBtn);
        acStatPublish();                // echo state back to HA
    }

    irSending = true;
    mqttCmd   = true;
    startTime = millis();
    retState  = WORKING;
    mainState = IR_SENDING;
}

// =====================================================================
// FLASH button: long-press (>= 2 s) enters pairing mode
// =====================================================================
void updateButton()
{
    bool pressed      = (digitalRead(PIN_BUTTON) == LOW);
    unsigned long now = millis();

    if (pressed && !btnLast)
    {
        // Just pressed
        btnPressedTime   = now;
        longPressHandled = false;
    }
    else if (pressed && btnLast && !longPressHandled)
    {
        // Still held down
        if (now - btnPressedTime >= 2000)
        {
            longPressHandled = true;
            Serial.println(F("[BTN] Long press -> PAIRING"));

            // Cancel whatever we were doing and go pair
            if (mainState == WORKING || mainState == PAIRING)
            {
                ac.stopCapture();
            }
            enterPairing();
        }
    }
    else if (!pressed && btnLast)
    {
        // Released
        btnPressedTime = 0;
    }

    btnLast = pressed;
}

// =====================================================================
// EEPROM save / load
// =====================================================================
void saveInitInfo()
{
    SavedConfig cfg;
    cfg.magic     = EEPROM_MAGIC;
    cfg.isCelsius = ac.isCelsius();
    cfg.matchCnt  = matchCnt;

    const bc7215FormatPkt_t*  fp = ac.getFormatPkt();
    const bc7215DataVarPkt_t* dp = ac.getDataPkt();

    memcpy(&cfg.irFormat, fp, sizeof(bc7215FormatPkt_t));
    memcpy(&cfg.irData,   dp, sizeof(bc7215DataMaxPkt_t));

    EEPROM.put(0, cfg);
    if (EEPROM.commit())
    {
        Serial.println(F("[EE] Config saved"));
    }
    else
    {
        Serial.println(F("[EE] Save failed"));
    }
}

bool loadInitInfo()
{
    SavedConfig cfg;
    EEPROM.get(0, cfg);
    if (cfg.magic != EEPROM_MAGIC)
    {
        Serial.println(F("[EE] No saved config"));
        return false;
    }

    memcpy(&irFormat, &cfg.irFormat, sizeof(bc7215FormatPkt_t));
    memcpy(&irData,   &cfg.irData,   sizeof(bc7215DataMaxPkt_t));
    matchCnt = cfg.matchCnt;

    if (cfg.isCelsius) ac.setCelsius();
    else               ac.setFahrenheit();

    Serial.println(F("[EE] Config loaded"));
    return true;
}
