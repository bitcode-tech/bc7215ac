/*
 * ESP8266_AC_CTRL_blocking.ino
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

#include <bc7215.h>
#include <bc7215ac.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>

// Pin and constant definitions
const int LED = 2;    // NodeMCU onboard LED pin
const String MODES[] = {"Auto", "Cool", "Heat", "Dry", "Fan", "Keep"}; // AC modes
const String FANSPEED[] = {"Auto", "Low", "Medium", "High", "Keep"}; // Fan speeds
const String EXTRA_KEY[] = {"Temperature +/-", "Mode", "Fan Speed Adjust"}; // Extra keys for sampling

SoftwareSerial bc7215Serial(5, 16);  // Rx=GPIO5 Tx=GPIO16
BC7215 bc7215Board(bc7215Serial, 0, 4);  // mod=GPIO0, busy=GPIO4
BC7215AC ac(bc7215Board);

// Global variables
char choice;
int counter;
bool initialized = false;
const bc7215DataVarPkt_t* dataPkt;
const bc7215FormatPkt_t* formatPkt;
bc7215DataMaxPkt_t sampleData[4];
bc7215FormatPkt_t sampleFormat[4];

/*
 * Setup function: Initializes EEPROM, serial communications, and LED pin.
 */
void setup() 
{
  EEPROM.begin(33 + sizeof(bc7215DataMaxPkt_t)); // Initialize EEPROM with required size
  Serial.begin(115200); // Start hardware serial for debugging
  bc7215Serial.begin(19200, SWSERIAL_8N2); // Start software serial for BC7215
  pinMode(LED, OUTPUT); // Set LED pin as output
  ledOff(); // Turn off LED initially
}

/*
 * Main loop: Displays menu and handles user choices for initialization, control, save/load, etc.
 */
void loop() 
{
  choice = mainMenu(); // Display main menu and get user choice
  switch (choice)
  {
    case '1':
      sampleInit(); // Sample IR and initialize library
      break;
    case '2':
      if (initialized)
      {
        acControl(); // Enter AC control submenu
      }
      else
      {
        Serial.println("\nAC control library not initialized yet. Please initialize first. Press any key to return to menu...");
        waitForInput();
      }
      break;
    case '3':
      if (initialized)
      {
        formatPkt = ac.getFormatPkt();
        dataPkt = ac.getDataPkt();
        EEPROM.put(0, *formatPkt); // Save format packet to EEPROM
        EEPROM.put(33, *((bc7215DataMaxPkt_t*)dataPkt)); // Save data packet to EEPROM
        EEPROM.commit();
        Serial.println("\nFormat Information: ");
        printData(formatPkt, 33);
        Serial.print("Data: ");
        printData(dataPkt->data, (dataPkt->bitLen+7)/8);
        Serial.println("Information saved successfully");
      }
      else
      {
        Serial.println("\nAC control library not initialized. Can only save data after successful initialization");
      }
      Serial.println("Press any key to continue");
      waitForInput();
      break;
    case '4':
      EEPROM.get(0, sampleFormat[0]); // Load format packet from EEPROM
      EEPROM.get(33, sampleData[0]); // Load data packet from EEPROM
      Serial.println("\nUsing");
      Serial.print("Format Information: ");
      printData(&sampleFormat[0], 33);
      Serial.print("Data: ");
      printData(sampleData[0].data, (sampleData[0].bitLen+7)/8);
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
      waitForInput();
      break;
    case '5':
      if (ac.matchNext())
      {
        Serial.println("Next protocol match successful!");
      }
      else
      {
        Serial.println("No other matching protocols. AC control library needs reinitialization");
        initialized = false;
      }
      Serial.println("Press any key to continue");
      waitForInput();
      break;
    case '6':
      if(ac.initPredef(predefSelMenu()))
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
      waitForInput();
      break;
    default:
      break;
  }
}

/*
 * LED control functions: Turn LED on or off.
 */
void ledOn() { digitalWrite(LED, LOW); }
void ledOff() { digitalWrite(LED, HIGH); }

/*
 * sampleInit: Captures IR signal from remote and initializes AC control library.
 */
void sampleInit()
{
  Serial.println("\nNow performing IR signal sampling and AC control library initialization. "
  "\nPlease set AC remote to < Cooling mode, 25°C(77°F)>, then press any key to continue...");
  waitForInput();
  Serial.println("Now please point at the IR receiver and press the <Fan Speed> button on the remote. \nThe program will automatically proceed to the next step after receiving the signal...");
  initialized = false;
  ac.startCapture(); // Start IR signal capture
  while (!ac.signalCaptured())
  {
    delay(10);
    counter++;
    if (counter & 0x10)
    {
      ledOn(); // Blink LED while waiting
    }
    else
    {
      ledOff();
    }
  }
  ac.stopCapture();
  if (ac.init())
  {
    initialized = true;
    ledOn();
  	dataPkt = ac.getDataPkt();
  	Serial.print("Data received: ");
  	printData(dataPkt->data, (dataPkt->bitLen+7)/8);
    Serial.println("AC control library initialized using received data  **SUCCESS** !!");
    int extra = ac.extraSample();
    if (extra > 0)
    {
      if (extra > 3) extra = 3;
      Serial.print("This AC format is special and requires additional sampling of original remote signals. \nNow please press the <<");
      Serial.println(EXTRA_KEY[extra-1] + ">> button on the remote for further sampling..");
      Serial.println("Press any key to enter sampling step...");
      waitForInput();
      Serial.println("Starting sampling...");
      ac.startCapture();
      int idleCntr = 0;
      int msgCnt = 0;
      while (1)
      {
        if (extra == 3)
        {
          if (ac.signalCaptured((bc7215DataVarPkt_t*)&sampleData[msgCnt], &sampleFormat[msgCnt]))
          {
            msgCnt++;
            idleCntr = 0;
          }
          else
          {
            if ((msgCnt != 0) && (!ac.isBusy()))
            {
              idleCntr += 10;
              if (idleCntr >= 200)
              {
                ac.stopCapture();
                Serial.println("Signal collection complete! Total signals collected: "+String(msgCnt));
                for (int i = 0; i < msgCnt; i++)
                {
                  printData(sampleData[i].data, (sampleData[i].bitLen+7)/8);
                }
                if (ac.init(msgCnt, sampleData, sampleFormat))
                {
                  Serial.println("Reinitialization with new collected signals successful!");
                  ledOn();
                  break;
                }
                else
                {
                  Serial.println("Reinitialization with new collected signals failed...");
                  ledOff();
                  ac.stopCapture();
                  Serial.println("Please press any key to return to main menu...");
                  waitForInput();
                  return;
                }
              }
            }
          }
        }
        else
        {
          if (ac.signalCaptured((bc7215DataVarPkt_t*)&sampleData[0], &sampleFormat[0]))
          {
            ac.stopCapture();
            ac.saveExtra(sampleData[0], sampleFormat[0]);
            Serial.println("Signal collection complete!");
            break;
          }
        }
        delay(10);
        counter++;
        if (counter & 0x10)
        {
          ledOn();
        }
        else
        {
          ledOff();
        }
      }
    }
    Serial.println("Please press any key. Program will return to main menu and AC control can begin...");
  }
  else
  {
    ledOff();
    Serial.println("AC control library initialization using received data **FAILED**, "
         "\npossible remote control status setting error or reception/decoding error. Please check remote settings and try again");
  }

  waitForInput();
}

/*
 * acControl: Handles AC control submenu for setting parameters, on/off.
 */
void acControl()
{
  char inputChar;
  String newParam = "";
  int timeMs;
  int interval=10;
  
  while (1)
  {
    char choice = ctrlMenu(); // Display control menu and get choice
    switch (choice)
    {
      case '1':
        Serial.println("");
        Serial.println("");
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
        Serial.println("(Note: Limited to parameters supported by controlled AC. For example: setting heat mode on cooling-only AC will have unpredictable results)");
        Serial.println("Please enter AC parameter values: (Enter 'exit' to return to upper menu, case insensitive)");
        Serial.println("");
        do
        {
          newParam = "";
          waitForInput();
          do
          {
            inputChar = Serial.read();
            if ((inputChar == '\n') || (inputChar == '\r'))
            {
              break;
            }
            newParam += inputChar;
            while (!Serial.available());
          } while (1);
          newParam.trim();
          Serial.println(newParam);
          if (newParam.equalsIgnoreCase("exit"))
          {
            Serial.println("Exit");
            break;
          }
          else
          {
            int comma1 = newParam.indexOf(',');
            int comma2 = newParam.indexOf(',', comma1+1);
            if (comma1 != -1 && comma2 != -1 && comma2 > comma1)
            {
              int temp = newParam.substring(0, comma1).toInt();
              int mode = newParam.substring(comma1+1, comma2).toInt();
              int fan = newParam.substring(comma2+1).toInt();
              if (mode < 0 || mode > 4)
              {
                mode = 5; // Keep mode
              }
              if (fan < 0 || fan > 3)
              {
                fan = 4; // Keep fan
              }
              if (temp >= 16 && temp <= 30)
              {
                Serial.println("Sending command to set AC to: "+String(temp)+"°C, Mode: "+MODES[mode]+"  Fan Speed: "+FANSPEED[fan]);
              }
              else
              {
                Serial.println("Sending command to set AC to: Temperature unchanged, Mode: "+MODES[mode]+"  Fan Speed: "+FANSPEED[fan]);
              }
              dataPkt = ac.setTo(temp, mode, fan);
    			    Serial.print("Sending data: ");
    			    printData(dataPkt->data, (dataPkt->bitLen+7)/8);
              timeMs = 0;
              while (ac.isBusy() && (timeMs<3000))
              {
                delay(interval);
                timeMs += interval;
              }
              if (timeMs >= 3000)
              {
                Serial.print("Send timeout");
              }
              Serial.println(" Complete!");
            }
            else
            {
              if (comma1 == -1)
              {
                int temp = newParam.toInt();
                if ((temp >= 16) && (temp <=30))
                {
                  Serial.println("Sending command to set AC to "+String(temp)+"°C");
                  dataPkt = ac.setTo(temp);
                  Serial.print("Sending data: ");
                  printData(dataPkt->data, (dataPkt->bitLen+7)/8);
                  timeMs = 0;
                  while (ac.isBusy() && (timeMs<3000))
                  {
                    delay(interval);
                    timeMs += interval;
                  }
                  if (timeMs >= 3000)
                  {
                    Serial.print("Send timeout");
                  }
                  Serial.println(" Complete!");
                }
                else
                { 
                  Serial.println("Input format error, please re-enter");
                }
              }
              else
              { 
                Serial.println("Input format error, please re-enter");
              }
            }
          }
          Serial.println("");
          Serial.println("Please continue entering settings, enter 'exit' to quit");
        } while (1);
        break;
      case '2':
          Serial.println("Sending AC power ON command");
          dataPkt = ac.on(); // Send AC on command
          Serial.print("Sending data: ");
          printData(dataPkt->data, (dataPkt->bitLen+7)/8);
          timeMs = 0;
          while (ac.isBusy () && (timeMs<3000))
          {
            delay(interval);
            timeMs += interval;
          }
          if (timeMs >= 3000)
          {
            Serial.print("Send timeout");
          }
          Serial.println(" Complete!");
        break;
      case '3':
          Serial.println("Sending AC power OFF command");
          dataPkt = ac.off(); // Send AC off command
          Serial.print("Sending data: ");
          printData(dataPkt->data, (dataPkt->bitLen+7)/8);
          timeMs = 0;
          while (ac.isBusy() && (timeMs<3000))
          {
            delay(interval);
            timeMs += interval;
          }
          if (timeMs >= 3000)
          {
            Serial.print("Send timeout");
          }
          Serial.println(" Complete!");
        break;
      case '4':
        return; // Return to main menu
        break;
      default:
        break;
    }
  }
}

/*
 * mainMenu: Displays main menu and returns user choice.
 */
char mainMenu()
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
  Serial.println("   3. Save initialization data to FLASH (must be after successful initialization)");
  Serial.println("   4. Load saved data and initialize");
  Serial.println("   5. Try next match (if initialization successful but cannot control AC properly)");
  Serial.println("   6. Load predefined protocol (when sampling cannot successfully initialize)");
  Serial.println("");
  waitForInput();
  return Serial.read();
}

/*
 * ctrlMenu: Displays AC control menu and returns user choice.
 */
char ctrlMenu()
{
  Serial.println("\nAC Control, please select:");
  Serial.println("   1. Set AC parameters");
  Serial.println("   2. Power ON");
  Serial.println("   3. Power OFF");
  Serial.println("   4. Return to upper menu");
  Serial.println("");
  waitForInput();
  return Serial.read();
}

uint8_t predefSelMenu()
{
  int choice;
  Serial.println("");
  Serial.println("A few protocols are not supported for direct decoding by BC7215A chip, provided as predefined protocols.");
  Serial.println("When direct sampling fails or initialization fails, you can try using predefined data to control the AC.");
  Serial.println("Brand names listed in protocol names are for reference only. If unable to control, please try all predefined protocols individually.");
  Serial.println("Please select:");
  for (int i=0; i<ac.cntPredef(); i++)
  {
    Serial.println("  "+String(i)+". "+ac.getPredefName(i));
  }
  Serial.println("");
  waitForInput();
  choice = Serial.parseInt();
  Serial.println("Selected type "+String(choice));
  return choice;
}

void waitForInput()
{
  while(Serial.available())
  {
    Serial.read(); // Clear buffer
  }
  while(!Serial.available());
}

/*
 * printData: Prints binary data in hex format.
 */
void printData(const void* data, uint8_t len)
{
  for (int i = 0; i < len; i++)
  {
      Serial.print(*((uint8_t*)data+i), HEX);
      Serial.print(' ');
  }
  Serial.println("");
}
