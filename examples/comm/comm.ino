/*
	BC7215 IR data communication example
	
	This example demostrate how to use BC7215 for data communication over IR.
	It's best to use 2 sets of the Arduino-BC7215 circuit to show how data 
	is exchanged.
	This demo code uses 2 buttons to transmit CRC checksum appended and raw
	data without CRC. When no button is pressed, the circuit is in receiving
	mode, if any data is received, the received data with be shown on serial
	monitor, and 2 LEDs will show whether the data has a correct CRC.
	
	The circuit:
	  Please refer to the user manual of the examples:
	  Arduino/libraries/bc7215/extras/doc/bc7215_arduino_examples_en.pdf   or
	  Arduino/libraries/bc7215/extras/doc/bc7215_examples.md
	  
	Created: March 2024
	by Bitcode
	
	https://www.github.com
	
*/

#include <bc7215.h>

#define IR_SERIAL 		Serial1        // Define the serial port used for BC7215

// Comment out the following #define line if you use 'Serial' as IR_SERIAL
// to connect BC7215. 
// If you use 'Serial' to connect BC7215, You will need to disconnect 
// BC7215 while uploading the sketch to avoid conflict as 'Serial' is also shared by uploading.
#define USE_SERIAL_MONITOR        // Code will print running status to serial monitor

// Pin connections
const int MOD_PIN = 4;
const int BUSY_PIN = 5;
const int BUTTON1 = 6;
const int BUTTON2 = 7;
const int LED2 = 8;
const int LED3 = 9;
const int LED1 = 13;

// IR format packet used for communication
const bc7215FormatPkt_t NECFormat = { { { 0x34 } },
    { 0x14, 0x5D, 0x0D, 0x5D, 0x14, 0x3D, 0x3D, 0x1D, 0x1C, 0x9C, 0x62, 0xA0, 0x29, 0xB2, 0x99, 0x44, 0x00, 0x00, 0xC2,
        0x36, 0x9F, 0xF7, 0xFA, 0xB8, 0xE2, 0x9A, 0xA3, 0x26, 0xEA, 0x90, 0x87, 0x30 } };

// Binary data used to demostrate binary data communication
const uint8_t BinData[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 };
// ASCII string used for communication
const char    TxtData[] = { "Hello World." };

BC7215 irModule(IR_SERIAL, MOD_PIN, BUSY_PIN);        // define BC7215 connection

byte BufData[32];		// Buffer used to send and receive data
byte crc;				// variable for calculated CRC value

byte i, len;
byte keyValue;
byte counter;

void led1On();		// function to operate LED1
void led1Off();
void led2On();		// function to operate LED2
void led2Off();
void led3On();		// function to operate LED3
void led3Off();
byte readKeypad();	// function to read keypad

void setup()
{
#if defined(USE_SERIAL_MONITOR)
    Serial.begin(115200);
#endif
    IR_SERIAL.begin(19200, SERIAL_8N2);

    pinMode(BUTTON1, INPUT_PULLUP);
    pinMode(BUTTON2, INPUT_PULLUP);
    pinMode(LED1, OUTPUT);
    pinMode(LED2, OUTPUT);
    pinMode(LED3, OUTPUT);

    led1Off();
    led2Off();
    led3Off();

    irModule.setRx();        // bc7215 set to receiving mode
    delay(20);
}

void loop()
{
    keyValue = readKeypad();
    switch (keyValue)	// do actions according to key press
    {
    case 0x01:        // button 1 pressed, send data with CRC
        led1On();
        irModule.setTx();        // switch BC7215 to TX mode
        delay(2);

        // load format packet before sending
        irModule.loadFormat(NECFormat);

        // sending binary data
#if defined(USE_SERIAL_MONITOR)		// if 'Serial' is connected with computer, print information to serial monitor
        Serial.println("Sending binary data with CRC...");
#endif
        // User must make sure sizeof(BinData) is not larger than sizeof(BufData)-1
        for (i = 0; i < sizeof(BinData); i++)		// copy every byte in BinData to BufData
        {
            BufData[i] = BinData[i];        // user should make sure BufData is at least 1 byte larger than BinData
        }
        crc = BC7215::crc8(BinData, sizeof(BinData));
        BufData[sizeof(BinData)] = crc;        // add CRC at end
        irModule.sendRaw(BufData, sizeof(BinData) + 1);		// send buffer data via BC7215
        while (!irModule.cmdCompleted())		// wait for sending to complete
            ;

        delay(100);        // should wait at least 40ms before another transmitting

#if defined(USE_SERIAL_MONITOR)		// if 'Serial' is connected with computer, print information to serial monitor
        Serial.println("Sending ASCII text with CRC...");
#endif
        // sending text data, no need to load format information again
        // User must make sure sizeof(TxtData) is not larger than sizeof(BufData)-1
        for (i = 0; i < sizeof(TxtData) - 1; i++)		// copy every byte in TxtData to BufData except ending 0
        {
            BufData[i] = TxtData[i];        // user should make sure BufData is at least of same size as TxtData
        }
        crc = BC7215::crc8(
            TxtData, sizeof(TxtData) - 1);        // only calculate the text part, not including the '0' at the end.
        BufData[sizeof(TxtData) - 1] = crc;
        irModule.sendRaw(BufData, sizeof(TxtData));		// send buffer data via BC7215
        while (!irModule.cmdCompleted())		// wait for sending to complete
            ;

        led1Off();
        irModule.setRx();        // turn back to receiving mode, waiting for incoming data
        delay(20);
        break;
    case 0x02:        // button 2 pressed, send data without CRC
        led1On();
        irModule.setTx();        // switch BC7215 to TX mode
        delay(2);

        // load format packet before sending
        irModule.loadFormat(NECFormat);

        // sending binary data
#if defined(USE_SERIAL_MONITOR)
        Serial.println("Sending binary data without CRC...");
#endif
        irModule.sendRaw(BinData, sizeof(BinData));
        while (!irModule.cmdCompleted())
            ;

        delay(100);        // should wait at least 40ms before another transmitting

#if defined(USE_SERIAL_MONITOR)
        Serial.println("Sending ASCII text without CRC...");
#endif
        // sending text data, no need to load format information again
        irModule.sendRaw(TxtData, sizeof(TxtData) - 1);
        while (!irModule.cmdCompleted())
            ;

        led1Off();
        irModule.setRx();        // turn back to receiving mode, waiting for incoming data
        delay(20);
        break;
    default:		// nokey was pressed, blink LED1 at every 1.6s
        if ((counter & 0x0f) == 0)        // every 1600ms (counter increased by 1 each 100ms)
        {
            led1On();
        }
        else
        {
            led1Off();
        }
        break;
    }
    if (irModule.dataReady())		// if IR data has been received by BC7215
    {
        len = irModule.getRaw(BufData, 0x200);   // get all received data (0x200 is the max possible value for length)
        // actual fetched data length is return to 'len'

        // *** data received, process the data any way you want here ***
#if defined(USE_SERIAL_MONITOR)
        for (i = 0; i < len; i++)
        {
            Serial.print(BufData[i], HEX);
            Serial.print(' ');
        }
        Serial.println("");
#endif

        if (BC7215::crc8(BufData, len) == 0)        // check if CRC is correct
        {
#if defined(USE_SERIAL_MONITOR)
            Serial.println("Received data CRC check correct.");
#endif
            led2On();
            delay(200);
            led2Off();
        }
        else        // CRC check failed, flash LED twice
        {
#if defined(USE_SERIAL_MONITOR)
            Serial.println("Received data CRC check error.");
#endif
            led3On();
            delay(200);
            led3Off();
        }
    }
    counter++;
    delay(100);
}

void led1On() { digitalWrite(LED1, HIGH); }

void led1Off() { digitalWrite(LED1, LOW); }

void led2On() { digitalWrite(LED2, HIGH); }

void led2Off() { digitalWrite(LED2, LOW); }

void led3On() { digitalWrite(LED3, HIGH); }

void led3Off() { digitalWrite(LED3, LOW); }

byte readKeypad(void)
{
    byte value = 0;
    if (digitalRead(BUTTON1) == LOW)        // if button1 is pressed
    {
        value = 1;
    }
    if (digitalRead(BUTTON2) == LOW)        // if button2 is pressed
    {
        value = 2;
    }
    while ((digitalRead(BUTTON1) == LOW) || (digitalRead(BUTTON2) == LOW))
        ;        // wait for all buttons to be released
    return value;
}


