/*
	BC7215 programmable(learning) remote control example
	
	This example implement a simple 4-button programmable remote control.
	User press both button1 and button2 to enter programming mode, in this
	mode, the onboard LED will blink rapidly. Then user press one of the 4
	buttons to select which button is to be programmed, LED turns to continuously
	on. At this stage press the key to be learnt on the original remote towards
	the IR receiver on the BC7215 board, the information will then be stored
	in Arduino, and board goes back to normal mode.
	In normal mode, press a programmed button, BC7215 will send out the learnt
	IR signal.
	
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

#define MOD     4
#define BUSY    5
#define BUTTON1 6
#define BUTTON2 7
#define BUTTON3 8
#define BUTTON4 9
#define LED     13

BC7215 irModule(IR_SERIAL, MOD, BUSY);        // define BC7215 connection

bc7215DataMaxPkt_t IRData[4];        // we use 4 max sized array to store data
bc7215FormatPkt_t IRFormat[4];		// 4 format packets to store format data which are needed to replicate remote signal

byte i, len;
byte workingMode;
byte keyValue;
byte counter;

void ledOn();		// LED operate functions
void ledOff();
byte readKeypad();	// keypad reading function


void setup()
{
#if defined(USE_SERIAL_MONITOR)   // if "Serial" is used for monitoring, setup Serial
    Serial.begin(115200);
#endif
    IR_SERIAL.begin(19200, SERIAL_8N2);		// setup serial port for BC7215

    pinMode(MOD, OUTPUT);
    pinMode(BUSY, INPUT);
    pinMode(BUTTON1, INPUT_PULLUP);
    pinMode(BUTTON2, INPUT_PULLUP);
    pinMode(BUTTON3, INPUT_PULLUP);
    pinMode(BUTTON4, INPUT_PULLUP);
    pinMode(LED, OUTPUT);

    ledOff();

    irModule.setTx();        // bc7215 set to receiving mode
    delay(2);
}

void loop()
{
    keyValue = readKeypad();		// read keypad
    switch (workingMode)
    {
    case 0:        // workingMode 0 is IR transmitting mode
        switch (keyValue)
        {
        case 0x01:        // button 1 pressed
            ledOn();
#if defined(USE_SERIAL_MONITOR)
            Serial.println("Transmitting IR signal 1.");
#endif
            irModule.loadFormat(IRFormat[0]);	// load format packet 0
            irModule.irTx(IRData[0]);			// send IR data 0
            ledOff();
            break;
        case 0x02:        // button 2 pressed
            ledOn();
#if defined(USE_SERIAL_MONITOR)
            Serial.println("Transmitting IR signal 2.");
#endif
            irModule.loadFormat(IRFormat[1]);	// load format packet 1
            irModule.irTx(IRData[1]);			// send IR data 1
            ledOff();
            break;
        case 0x04:        // button 3 pressed
            ledOn();
#if defined(USE_SERIAL_MONITOR)
            Serial.println("Transmitting IR signal 3.");
#endif
            irModule.loadFormat(IRFormat[2]);	// load format packet 2
            irModule.irTx(IRData[2]);			// send IR data 2
            ledOff();
            break;
        case 0x08:        // button 4 pressed
            ledOn();
#if defined(USE_SERIAL_MONITOR)
            Serial.println("Transmitting IR signal 4.");
#endif
            irModule.loadFormat(IRFormat[3]);	// load format packet 3
            irModule.irTx(IRData[3]);			// send IR data 3
            ledOff();
            break;
        case 0x03:        // keyvalue==3 means button 1 & button 2 pressed
#if defined(USE_SERIAL_MONITOR)
            Serial.println("Start Programming, please select the button to be programmed.");
#endif
            irModule.setRx();        // set BC7215 TO receiving mode
            delay(20);
            irModule.setRxMode(1);        // set BC7215 to complex mode
            workingMode = 5;
            break;
        default:
            if ((counter & 0x0f) == 0)        // counter increase 1 every 1600ms
            {
                ledOn();
            }
            else
            {
                ledOff();
            }
            break;
        }
        break;
    case 1:        // workingMode 1 is BUTTON1 learning mode
        if (irModule.formatReady())		// if format packet received
        {
            irModule.getFormat(IRFormat[0]);	// store it in IRFormat[0]

            // if data packet is available and size is smaller than the max size
            if (irModule.dataReady() && (irModule.dpketSize() <= sizeof(IRData[0])))  
            // if data packet available and size is not larger than container
            {
                irModule.getData(IRData[0]);	// store data packet in IRData[0]
                ledOff();
                irModule.setTx();        // switch BC7215 back to transmitting mode
                delay(2);
                workingMode = 0;        // program change back to normal mode
#if defined(USE_SERIAL_MONITOR)
                Serial.println("IR format and data saved for button 1. Go back to standby mode.");
#endif
            }
        }
        break;
    case 2:        // workingMode 2 is BUTTON2 learning mode
        if (irModule.formatReady())
        {
            irModule.getFormat(IRFormat[1]);

            // if data packet is available and size is smaller than the max size
            if (irModule.dataReady() && (irModule.dpketSize() <= sizeof(IRData[1])))
            {
                irModule.getData(IRData[1]);
                ledOff();
                irModule.setTx();        // switch BC7215 back to transmitting mode
                delay(2);
                workingMode = 0;        // program change back to normal mode
#if defined(USE_SERIAL_MONITOR)
                Serial.println("IR format and data saved for button 2. Go back to standby mode.");
#endif
            }
        }
        break;
    case 3:        // workingMode 3 is BUTTON3 learning mode
        if (irModule.formatReady())
        {
            irModule.getFormat(IRFormat[2]);

            // if data packet is available and size is smaller than the max size
            if (irModule.dataReady() && (irModule.dpketSize() <= sizeof(IRData[2])))
            {
                irModule.getData(IRData[2]);
                ledOff();
                irModule.setTx();        // switch BC7215 back to transmitting mode
                delay(2);
                workingMode = 0;        // program change back to normal mode
#if defined(USE_SERIAL_MONITOR)
                Serial.println("IR format and data saved for button 3. Go back to standby mode.");
#endif
            }
        }
        break;
    case 4:        // workingMode 4 is BUTTON4 learning mode
        if (irModule.formatReady())
        {
            irModule.getFormat(IRFormat[3]);

            // if data packet is available and size is smaller than the max size
            if (irModule.dataReady() && (irModule.dpketSize() <= sizeof(IRData[3])))
            {
                irModule.getData(IRData[3]);
                ledOff();
                irModule.setTx();        // switch BC7215 back to transmitting mode
                delay(2);
                workingMode = 0;        // program change back to normal mode
#if defined(USE_SERIAL_MONITOR)
                Serial.println("IR format and data saved for button 4. Go back to standby mode.");
#endif
            }
        }
        break;
    case 5:        //  workingMode 5 is entering learning mode
        switch (keyValue)	// check keypad input to determin which button is to be setup
        {
        case 0x01:        // button 1 pressed
            ledOn();
            workingMode = 1;
#if defined(USE_SERIAL_MONITOR)
            Serial.println("Waiting for IR signal for button 1.");
#endif
            break;
        case 0x02:        // button 2 pressed
            ledOn();
            workingMode = 2;
#if defined(USE_SERIAL_MONITOR)
            Serial.println("Waiting for IR signal for button 2.");
#endif
            break;
        case 0x04:        // button 3 pressed
            ledOn();
            workingMode = 3;
#if defined(USE_SERIAL_MONITOR)
            Serial.println("Waiting for IR signal for button 3.");
#endif
            break;
        case 0x08:        // button 4 pressed
            ledOn();
            workingMode = 4;
#if defined(USE_SERIAL_MONITOR)
            Serial.println("Waiting for IR signal for button 4.");
#endif
            break;
        default:
            if (counter & 0x02)        // every 200ms
            {
                ledOn();
            }
            else
            {
                ledOff();
            }
            break;
        }
    default:
        break;
    }
    counter++;
    delay(100);
}

void ledOn() { digitalWrite(LED, HIGH); }

void ledOff() { digitalWrite(LED, LOW); }

byte readKeypad()
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
    if (digitalRead(BUTTON3) == LOW)        // if button1 is pressed
    {
        value = 4;
    }
    if (digitalRead(BUTTON4) == LOW)        // if button2 is pressed
    {
        value = 8;
    }
    while ((digitalRead(BUTTON1) == LOW) || (digitalRead(BUTTON2) == LOW) || (digitalRead(BUTTON3) == LOW)
        || (digitalRead(BUTTON4) == LOW))
    {
        if ((digitalRead(BUTTON1) == LOW) && (digitalRead(BUTTON2) == LOW))
        {
            value = 3;
        }
    }
    return value;
}

