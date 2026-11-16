/*
	BC7215 IR remote decoder example
	
	This example implement a simple IR remote decoder. It uses the receiving
	mode of BC7215 only. If any signal is received, the decoded raw data
	will be shown in serial monitor.
		
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

const int LED = 13;		// LED is connected to digital I/O 13
//const int BUSY = 2;	// Reserved for future use
//const int MOD = 3;

// In this application, MOD is connected to VCC permanently, and BUSY is not connected
// so the connection parameter uses constant BC7215::MOD_HIGH and BC7215::BUSY_NC
BC7215 irModule(IR_SERIAL, BC7215::MOD_HIGH, BC7215::BUSY_NC);        // define BC7215 connection

bc7215DataMaxPkt_t rcvdData;        // This is the variable for received IR data
char	buffer[4];

word len;
word counter;

void ledOn();		// function to turn ON LED
void ledOff();		// function to turn OFF LED

void setup()
{
#if defined(USE_SERIAL_MONITOR)		// if Serial is used for serial monitor
    Serial.begin(115200);
#endif
    IR_SERIAL.begin(19200, SERIAL_8N2);		// initialized BC7215 serial port

    pinMode(LED, OUTPUT);
    ledOff();
//	pinMode(BUSY, INPUT);		// Reserved for future use
//	pinMode(MOD, OUTPUT);
//	digitalWrite(MOD, HIGH);
}

void loop()
{
    if (irModule.dataReady())        // if IR data received by BC7215
    {
            counter++;
            ledOn();
            irModule.getData(rcvdData);        // get data
			len = (rcvdData.bitLen+7)/8;
#if defined(USE_SERIAL_MONITOR)		// if Serial is used for serial monitor, print information
            Serial.print("Pkt Counter: ");
            Serial.print(counter);
            Serial.print(",  Bit lenght: ");
            Serial.print(rcvdData.bitLen);
            Serial.print(" bits, (");
            Serial.print(len);
            Serial.println(" bytes):");
            for (int i = 0; i < len; i++)        // payload data length is the whole packet size -2
            {
                sprintf(buffer, "%02X ", rcvdData.data[i]);
                Serial.print(buffer);
            }
            Serial.println("");
            Serial.println("");
#endif
    }
    delay(50);
    ledOff();
}

void ledOn() { digitalWrite(LED, HIGH); }

void ledOff() { digitalWrite(LED, LOW); }


