#include <SoftwareSerial.h>

  /*
   * This is a small program to send a notification SMS to a user when a mailbox is opened (detected by depressing an endstop switch, such as those found on 3D-printers).
   * 
   * Note that #define _SS_MAX_RX_BUFF was changed to 256 in SoftwareSerial.h, as explained in https://lastminuteengineers.com/sim800l-gsm-module-arduino-tutorial/. 
   * 
   * HARDWARE REQUIREMENTS:
   *  - Arduino Uno
   *  - SIM800L GSM module + valid SIM-card
   *  - ~4V power supply/battery, that can deliver 2A (LM2596 buck converter can be used, if powering with a single source, for example)
   *  - 4 resistors
   *  - capacitor
   *  - jumper wires
   * 
   * HARDWARE SETUP:
   *  - PSU/LIPO ~4V input to VCC (SIM800L), must be able to supply 2A. Recommended to add a capacitor against ground to stabilize input voltage.
   *  - PSU/LIPO GND to GND (SIM800L)
   *  - SIM800L TXD pin to Arduino SoftSerial RX (pin 9)
   *  - SIM800L RXD pin to Arduino SoftSerial TX (pin 8) with resistor divider (needs ~3.3V signal to SIM800L), with 2x larger resistor to GND.
   *  - Endstop switch mounted to Arduino pin 
   *  
   * 
   * SUGGESTIONS FOR IMPROVEMENTS (for a "proper" prototype):
   *  - Use sleep mode on SIM800L (only wake when button depressed, SLEEP: DTR pulled high, AT+CSCLK=1. WAKE: DTR pulled low, send AT within 50ms, AT+CSCLK=0). AT+CFUN=0 to disable some features? Should save roughly 17mA)
   *  - Use sleep mode on Arduino Uno (after setup complete, only wake when button depressed, see: http://www.gammon.com.au/power)
   *  - Build barebones board and move microprocessor there to save even more power
   *  - Use a u.fl antenna for better gain
   *  - Read phone number, PIN code, etc from external storage on startup
   *  - Offer a choice between SMS or internet-based notifications (GET request to server backend, probably cheaper due to small amount of data)
   *  - Add battery protection circuit and potentially monitor battery state to inform user (lipo shouldn't be allowed to go too low)
   * 
   * BELOW ARE SOME GENERAL NOTES AND USEFUL LINKS:
   * 
   * https://www.elecrow.com/wiki/images/2/20/SIM800_Series_AT_Command_Manual_V1.09.pdf AT commands
   * https://www.developershome.com/sms/readSmsByAtCommands.asp AT+CMGR=3 to read from slot 3. AT+CMGD=1 (remove msg 1). AT+CPMS? to see how much memory is used.
   * https://cassiopeia.hk/arduinoradio/ (FM radio)
   * 
   * Page 220 in AT manualfor TCPIP commands. APN: online.telia.se
   *  - AT+CSTT="online.telia.se","",""
   *  - AT+CIICR (starts wireless connection
   *  - AT+CIFSR (get IP)
   *  - AT+CIPPING="google.com"
   *  - AT+CIPSTART="TCP","exploreembedded.com",80
   *  - AT+CIPSEND=63 (on new line, enter HTTP request, e.g. GET exploreembedded.com/wiki/images/1/15/Hello.txt HTTP/1.0
   *  - AT+CIPCLOSE (close connection)
   * 
   * AT+CSCLK=1 to control sleep mode through DTR pin (high = sleep) https://electronics.stackexchange.com/questions/329151/how-to-wake-up-sim800-module-from-sleep-after-entering-sleep-mode-2-atcsclk-2
   *  
   * Location and timestamp: https://marabesi.com/posts/2018/02/16/sim800l-tracking-your-iot-device.html
   * AT+CIPGSMLOC=1,1 after configuring through AT+SAPBR. Gives location (zeroes for me?) and timestamp (GMT) from cell network.
   * 
   * Make a call: ATD+4673xxxxxxx;
   * ATH (hangup), ATA (answer)
   */


//Create software serial object to communicate with SIM800L
SoftwareSerial sim800l(9,8); //SIM800L Tx & Rx is connected to Arduino pins 9 and 8
String feedback_msg = "";
bool lid_opened = false;
const byte interruptPin = 2; // connected to endstop switch

void setup()
{
  
  pinMode(interruptPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(interruptPin), onButtonDepress, RISING);
  
  //Begin serial communication with Arduino and Arduino IDE (Serial Monitor)
  Serial.begin(9600);
  
  //Begin serial communication with Arduino and SIM800L
  sim800l.begin(9600);

  Serial.println("Initializing...");
  delay(1000);

  sim800l.println("AT"); //Once the handshake test is successful, it will back to OK
  updateSerial();
  sim800l.println("AT+CSQ"); //Signal quality test, value range is 0-31 , 31 is the best
  updateSerial();
  sim800l.println("AT+CCID"); //Read SIM information to confirm whether the SIM is plugged
  updateSerial();
  sim800l.println("AT+CREG?"); //Check whether it has registered in the network (0,1 response expected)
  updateSerial();
  sim800l.println("AT+CPIN?"); // Check if expecting PIN
  updateSerial();
  
  Serial.println(">>> Please wait for setup to complete before doing more commands.");
}

void loop()
{
  if (lid_opened)
  {
    Serial.println(">>> YOU HAVE MAIL!");
    sendText("Hi Johan! You have received mail.");
    lid_opened = false;
  }
  updateSerial();
}

void updateSerial()
{
  delay(500);
  while (Serial.available()) 
  {
    sim800l.write(Serial.read());//Forward what Serial received to Software Serial Port
  }
  while(sim800l.available()) 
  {
    char feedback_char = sim800l.read();
    Serial.write(feedback_char);//Forward what Software Serial received to Serial Port

    // Keep track of last feedback message line to look for specific output from GSM modem
    if (feedback_char == '\n')
    {
      if (feedback_msg.indexOf("+CPIN: READY") >= 0)
      {
        Serial.println(">>> NO PIN NEEDED!");
      }
      else if (feedback_msg.indexOf("+CPIN: SIM PIN") >= 0)
      {
        Serial.println(">>> ENTERING PIN PROGRAMMATICALLY.");
        sim800l.println("AT+CPIN=4078"); // Enter PIN code, hardcoded here
      }
      if (feedback_msg.indexOf("SMS Ready") >= 0)
      {
        sendText("Your mailbox notification service is now up and running."); 
      }
      /*
      Huh, that's an easy way to make a bomb...
      
      if (feedback_msg.indexOf("RING") >= 0)
      {
        Serial.println(">>> BOOM!");
      }
      */
      
      feedback_msg = ""; //reset for next line
    }
    else {
      feedback_msg.concat(feedback_char);
    }
  } 
}

void sendText(String msg)
{
  sim800l.println("AT+CMGF=1"); // Configuring TEXT mode
  updateSerial();
  sim800l.println("AT+CMGS=\"+46737600282\""); //change ZZ with country code and xxxxxxxxxxx with phone number to sms
  updateSerial();
  sim800l.print(msg);
  updateSerial();
  sim800l.write(26);
}

void onButtonDepress()
{
  lid_opened = true;
}
