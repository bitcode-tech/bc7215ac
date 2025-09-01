/*
	BC7215 IR controlled switch example
	
	This is a demo project using BC7215 as IR receiver, it will let you
	to use any IR remote control, and turn any 2 spare buttons on any remote
	into the remote control keys of the 2-channel switches.
	The circuit have 2 buttons used to pair the circuit with 2 keys on
	any remote control. When any button is pressed, Arduino will go into
	'pairing' mode, user let the Arduino 'learn' the remote code, and
	after the channel has been 'paired', receiving the same IR signal
	from the remote will then toggle the corresponding relay.
		
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
const int BUTTON1 = 6;
const int BUTTON2 = 7;
const int SW1 = 8;
const int SW2 = 9;
const int LED = 13;

// In this application, MOD is connected to VCC permanently, and BUSY is not connected
// so the connection parameter uses constant BC7215::MOD_HIGH and BC7215::BUSY_NC
BC7215 irModule(IR_SERIAL, BC7215::MOD_HIGH, BC7215::BUSY_NC);        // define BC7215 connection

bc7215DataMaxPkt_t irData[2];        // 2 saved IR data for each controlled switch
bc7215DataMaxPkt_t rcvdData;         // This is for the received IR data
byte               sig;				 // received data's signature byte
byte               workingMode;
byte               keyValue;
byte               counter;
word               len;

void ledOn();		// LED operate functions
void ledOff();
void toggleSw1();	// output relay oeperate functions
void toggleSw2();
byte readKeypad();	// keypad read function


void setup()
{
#if defined(USE_SERIAL_MONITOR)   // if "Serial" is used for monitoring, setup Serial
    Serial.begin(115200);
#endif
    IR_SERIAL.begin(19200, SERIAL_8N2);		// Setup port for BC7215

    pinMode(BUTTON1, INPUT_PULLUP);
    pinMode(BUTTON2, INPUT_PULLUP);
    pinMode(SW1, OUTPUT);
    digitalWrite(SW1, LOW);
    pinMode(SW2, OUTPUT);
    digitalWrite(SW2, LOW);
    pinMode(LED, OUTPUT);
    digitalWrite(LED, HIGH);
}

void loop()
{
    keyValue = readKeypad();        // read if there is key press
    switch (workingMode)
    {
    case 0:                               // workingMode 0 is normal mode
        // counter increase 1 every 50ms
        if ((counter & 0x1f) == 0)        // flash LED every 1600ms to show it's in working mode
        {
            ledOn();
        }
        else
        {
            ledOff();
        }

        switch (keyValue)		// actions if keypad is pressed
        {
        case 0x01:                  // button 1 pressed
            ledOn();                // turn on LED to indicate it's in setting mode
            workingMode = 1;        // Mode1 is to setup Sw1
#if defined(USE_SERIAL_MONITOR)
            Serial.println("Start pairing for SW1.");
#endif
            break;
        case 0x02:                  // button 2 pressed
            ledOn();                // turn on LED to indicate it's in setting mode
            workingMode = 2;        // Mode2 is to setup Sw2
#if defined(USE_SERIAL_MONITOR)
            Serial.println("Start pairing for SW2.");
#endif
            break;
        default:
            break;
        }

        if (irModule.dataReady())        // if remote data is received
        {
            len = irModule.dpketSize();
            if (len <= sizeof(rcvdData))        // if data will not overflow the target RAM
            {
                sig = irModule.getData(rcvdData);        // read data and clear status
                // some PPM remotes such as RC5 and RC6 have a toggle bit in data, which will toggle each time
                // the button is pressed. To make comparasion easier, we mask these bits if the signal is PPM
                if ((sig & 0x30)
                    != 0x30)        // if TP1:TP0 != 11 (PPM signal), mask possible toggle bits in RC5 and RC6
                {
                    rcvdData.data[0] |= 0x28;        // mask bit5(RC5) and bit3(RC6)
                }
                if ((rcvdData.bitLen == irData[0].bitLen)
                    && (BC7215::compareDpkt(sig, rcvdData, irData[0]) == 1))
                // if received IR is same as 1st stored data packet
                {
                    toggleSw1();
#if defined(USE_SERIAL_MONITOR)
                    Serial.println("SW1 toggled.");
#endif
                    // some remote such as RC5 repeat sending while the button is pressed,
                    // we only leave when there is no IR data received in 0.2s
                    // to prevent the switch being toggled by repeating signals.
                    do
                    {
                        irModule.clrData();        // clear any received data packet
                        delay(200);
                    } while (irModule.dataReady());
                }
                else if ((rcvdData.bitLen == irData[1].bitLen)
                    && (BC7215::compareDpkt(sig, rcvdData, irData[1]) == 1))
                // if received IR is same as 2nd stored data packet
                {
                    toggleSw2();
#if defined(USE_SERIAL_MONITOR)
                    Serial.println("SW2 toggled.");
#endif

                    // some remote such as RC5 repeat sending while the button is pressed,
                    // we only leave when there is no IR data received in 0.2s
                    // to prevent the switch being toggled by repeating signals.
                    do
                    {
                        irModule.clrData();        // clear any received data packet
                        delay(200);
                    } while (irModule.dataReady());
                }
            }
            else
            {
                irModule.clrData();
            }
        }

        break;
    case 1:        // workingMode 1 is Sw1 setting mode
        if (irModule.dataReady())
        {
            len = irModule.dpketSize();
            if (len <= sizeof(irData[0]))
            {
                sig = irModule.getData(irData[0]);
                // save data in irData[0], in real product the data should be saved in EEPROM

                // some PPM remotes such as RC5 and RC6 have a toggle bit in data, which will toggle each time
                // the button is pressed. To make comparasion easier, we mask these bits if the signal is PPM
                if ((sig & 0x30)
                    != 0x30)        // if TP1:TP0 != 11 (PPM signal), mask possible toggle bits in RC5 and RC6
                {
                    irData[0].data[0] |= 0x28;        // mask bit5(toggle bit for RC5) and bit3(toggle bit for RC6)
                }

                ledOff();

                // some remote such as RC5 repeat sending while the button is pressed,
                // we only leave when there is no IR data received in 0.2s
                // to prevent the switch being toggled by repeating signals.
                do
                {
                    irModule.clrData();        // clear data
                    delay(200);
                } while (irModule.dataReady());        // if there is no new data in 200ms
                workingMode = 0;                       // switch back to normal mode
#if defined(USE_SERIAL_MONITOR)
                Serial.println("IR data for SW1 saved.");
#endif
            }
            else
            {
                irModule.clrData();        // if data will overflow, discard received data
            }
        }
        break;
    case 2:        // workingMode 2 is Sw2 setting mode
        if (irModule.dataReady())
        {
            len = irModule.dpketSize();
            if (len <= sizeof(irData[1]))        // if data will not overflow the IR RAM
            {
                sig = irModule.getData(irData[1]);
                // save data in irData[1], in real product the data should be saved in EEPROM

                // some PPM remotes such as RC5 and RC6 have a toggle bit in data, which will toggle each time
                // the button is pressed. To make comparasion easier, we mask these bits if the signal is PPM
                if ((sig & 0x30)
                    != 0x30)        // if TP1:TP0 != 11 (PPM signal), mask possible toggle bits in RC5 and RC6
                {
                    irData[1].data[0] |= 0x28;        // mask bit5(toggle bit for RC5) and bit3(toggle bit for RC6)
                }

                ledOff();

                // some remote such as RC5 repeat sending while the button is pressed,
                // we only leave when there is no IR data received in 0.2s
                // to prevent the switch being toggled by repeating signals.
                do
                {
                    irModule.clrData();        // clear data
                    delay(200);
                } while (irModule.dataReady());        // if there is no new data in 200ms
                workingMode = 0;                       // switch back to normal mode
#if defined(USE_SERIAL_MONITOR)
                Serial.println("IR data for SW2 saved.");
#endif
            }
            else
            {
                irModule.clrData();        // if data will overflow, discard received data
            }
        }
        break;
    default:
        break;
    }
    counter++;
    delay(50);
}

void ledOn() { digitalWrite(LED, HIGH); }

void ledOff() { digitalWrite(LED, LOW); }

void toggleSw1()
{
    if (digitalRead(SW1) == HIGH)
    {
        digitalWrite(SW1, LOW);
    }
    else
    {
        digitalWrite(SW1, HIGH);
    }
    Serial.println("SW1 toggled");
}

void toggleSw2()
{
    if (digitalRead(SW2) == HIGH)
    {
        digitalWrite(SW2, LOW);
    }
    else
    {
        digitalWrite(SW2, HIGH);
    }
    Serial.println("SW2 toggled");
}

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
    while ((digitalRead(BUTTON1) == LOW) || (digitalRead(BUTTON2) == LOW))
        ;        // wait for all buttons to be released
    return value;
}

