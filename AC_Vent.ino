/*
 *
 * PRODUCTION VERSION - DO NOT COMMIT!!!
 *
 #ESP32 controller for AC Vents
 Peter Wells - March 2020

 Version 1.2.0
  Support microstepping

 Version 1.1.0
  Added debouncing
  Swap buttons with Javascript for <a href... for IOS support
  
 Version 1.0.0
  Initial version
*/
#include <WiFi.h>
#include <WiFiMulti.h>
#include <EEPROM.h>


// ---------------------------------------------------------------------------------------------------------------------
// -- START USER CONFIGURATION --
// ---------------------------------------------------------------------------------------------------------------------
#define NUM_MOTORS 3

const int dirPins[] = { //Pins controlling direction of stepper
  15, //Peter
  5, //Burton
  25 //guest
};
const int stepper[] = { //Pins controlling stepper steps
  2,
  18,
  33
};
const int stepperPower[] = { //Pins controlling stepper power
  4,
  19,
  32
};
const int endStop[] = { //Pins controlling stepper steps
  14,
  27,
  13
};
const char* ventNames[] = { //Pins controlling stepper steps
  "Peter's Room",
  "Burton's Room",
  "Guest Room"
};

// Current microstepping setting (no need to vary stepDelay or fullyOpen)
const int microStepping = 8; //1=no microStepping, 32=max (on DRV8825)

// Amount in steps (200 per rotation) to open the vent (ignoring microStepping)
const int fullyOpen = 600;

//Speed of the motor (in Microseconds)
const int stepDelay = 2000; //delay between steps (1000 = fastest, 5000 = pretty slow)

// Wifi / Network setings
const char* ssid1    = "SSID"; // Primary WiFi network
const char* password1 = "wifipassword";
const char* ssid2    = ""; // Optional backup WiFi network (blank to disable)
const char* password2 = "";

IPAddress local_IP(192, 168, 2, 110);
IPAddress gateway(192, 168, 2, 1);
IPAddress subnet(255, 255, 255, 0);
// ---------------------------------------------------------------------------------------------------------------------
// -- END USER CONFIGURATION --
// ---------------------------------------------------------------------------------------------------------------------

// Switch trigger values (invert if switch triggers on vent open)
const int openVent = LOW;
const int closeVent = HIGH;

int stepperPos[NUM_MOTORS]; // Current position of stepper motors
int fullyOpenMicro = fullyOpen * microStepping; // MicroStepping compensated fully open position (in steps)
int stpDelay = stepDelay / microStepping; //set delay compensating for MicroStepping

WiFiMulti wifiMulti;
WiFiServer server(80);

//function defaults
void spinMotor( int motor=0, int dir=LOW, int dist=1 );
void findZero( int motor=0, int dir=LOW, int posMoved=0 );
void ensureOpen( int closeMotor=0, int openStatus=0 );
//end function defaults

void setup()
{
  Serial.begin(115200);
  // initialize EEPROM with predefined size (for power outage recovery of position)
  EEPROM.begin(NUM_MOTORS);
  
  for (int i=0; i < NUM_MOTORS; i++){
    //recover saved state from EEPROM
    int savedPercent = EEPROM.read(i);
    if( savedPercent <= 100 ){
      float savedPos = (savedPercent / 100.0) * float(fullyOpenMicro);
      stepperPos[i] = round(savedPos); //coverting float to int
      Serial.print("Recovered motor ");
      Serial.print(i);
      Serial.print(" state, ");
      Serial.print(savedPercent);
      Serial.print("%, ");
      Serial.print(stepperPos[i]);
      Serial.print(" / ");
      Serial.println(fullyOpenMicro);
    } else {
      stepperPos[i] = -1;
    }

    //setup pin modes for each motor
    pinMode(dirPins[i], OUTPUT);      // set stepper pin mode
    pinMode(stepper[i], OUTPUT);      // set stepper pin mode
    pinMode(stepperPower[i], OUTPUT);      // set stepperPower pin mode
    digitalWrite(stepperPower[i], LOW);
    pinMode(endStop[i], INPUT);      // set endstop pin mode
  } //end for loop

  // We start by connecting to a WiFi network
  Serial.println();
  Serial.println("Connecting to WiFi ");
  wifiMulti.addAP(ssid1, password1);
  if( "" != ssid2 ){
      wifiMulti.addAP(ssid2, password2);
  }

  if (!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("STA Failed to configure");
  }
  
  while (wifiMulti.run() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("WiFi connected to: ");
  Serial.println(WiFi.SSID());
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  server.begin();
}

void loop(){
  WiFiClient client = server.available();   // listen for incoming clients

  if (client) {
    //Serial.println("new client");
    String currentLine = "";                   // make a String to hold incoming data from the client
    while (client.connected()) {
      if (client.available()) {                // if there's client data
        char c = client.read();                // read a byte
        if (c == '\n') {                     // check for newline character,
          if (currentLine.length() == 0) {     // if line is blank it means its the end of the client HTTP request
            int pos[NUM_MOTORS];
            client.println("<!DOCTYPE html>"); // open wrap the web page
            client.print("<html><head>");
            client.print("<meta charset='utf-8'>");
            client.print("<meta name='viewport' content='initial-scale=1.0'>");
            client.print("<link rel='shortcut icon' href='https://webbird.net.au/peter/vent_icon.ico'>");
            client.print("<link rel='icon' sizes='256x256' href='https://webbird.net.au/peter/vent_icon.png'>");
            client.print("<link rel='apple-touch-icon-precomposed' sizes='256x256' href='https://webbird.net.au/peter/vent_icon.png'>");
            client.print("<link rel='manifest' href='https://webbird.net.au/peter/vent_manifest.json'>");
            client.print("<meta name='apple-mobile-web-app-capable' content='yes'>");
            client.print("<meta name='mobile-web-app-capable' content='yes'>");
            client.print("<meta name='viewport' content='initial-scale=1.0'>");
            client.print("<title>AC Vent Controls</title>");
            client.print("<style>");
            client.print( "html, body{height:100%;overflow:hidden;margin:0;padding:5px;text-align:center;background:#333333;font-family:sans-serif;}");
            client.print( "h1,h4,a{text-transform:uppercase;color:#DADADA;}");
            client.print( "a{text-decoration:none;}");
            client.print( ".button_row{display:flex;align-items:center;}");
            client.print( ".button{flex-grow:1;flex-basis:1;padding:12px 2px;background:#202021;border:1px solid #5C5C5C;color:#DADADA;margin:0 4px; text-transform:uppercase;cursor:pointer;}");
            client.print( ".status_row{width:100%;height:30px;margin:8px 4px;position:relative;background:#202021;border:1px solid #5C5C5C;}");
            client.print( ".statusbar{width:0%;height:100%;position:absolute;top:0;left:0;background:#18BAC8;transition:width 1s ease;}");
            client.print("</style>");
            client.print("<script type='text/javascript'>");
            client.print( "history.replaceState({}, 'AC Vent Controls', '/');");
            client.print("</script>");
            client.print("</head>");
            client.print("<body><h1><a href=\"/\">AC Vent Controls</a></h1>");
            // Gui buttons start here
            for (int i=0; i < NUM_MOTORS; i++){
              pos[i] = 0;
              if( stepperPos[i] > 0 ){
                Serial.println("Calculating position...");
                Serial.print("StepperPos: ");
                Serial.println(stepperPos[i]);
                Serial.print("EndPos: ");
                Serial.println(fullyOpenMicro);
                Serial.print("Ratio: ");
                float ratio = stepperPos[i] / float(fullyOpenMicro);
                Serial.println(ratio);
                pos[i] = ratio * 100;
              }
              client.print("<h4>");
              client.print( ventNames[i]);
              client.print("</h4>");
              client.print("<div class='button_row'>");
              client.print( "<a class='button' href='/?a=2&m=");
              client.print(   i);
              client.print( "'>Closed</a>");
              client.print( "<a class='button' href='/?a=5&m=");
              client.print(   i);
              client.print( "'>25%</a>");
              client.print( "<a class='button' href='/?a=4&m=");
              client.print(   i);
              client.print( "'>50%</a>");
              client.print( "<a class='button' href='/?a=3&m=");
              client.print(   i);
              client.print( "'>Open</a>");
              client.print("</div>");
              client.print("<div class='status_row'>");
              client.print( "<div class='statusbar' style='width:");
              client.print(   pos[i]);
              client.print( "%'></div>");
              client.print("</div>");

              Serial.print("Position: ");
              Serial.println(pos[i]);
            }
            client.print("</body></html>"); // close wrap the web page
            
            client.println(); // The HTTP response ends with an extra blank line:
          
            break;  // break out of the while loop:
          } else {    // if you got a newline, then clear currentLine:
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;       // add it to the end of the currentLine
        }

        if( c == '\r' && currentLine.startsWith("GET /") ){
          //Serial.println(currentLine);
          // Check to see if the client request was "GET /H" or "GET /L":

          int actionPos = currentLine.indexOf("?a=");
          if( actionPos > 0 ){
            int action = currentLine.substring( actionPos+3, actionPos+4 ).toInt();

            int dist = 0;
            int distPos = currentLine.indexOf("&d=");
            if( distPos > 0 ){
              dist = currentLine.substring( distPos+3, distPos+6 ).toInt() * 100;
            }

            int motorNum = 0;
            int motorPos = currentLine.indexOf("&m=");
            if( motorPos > 0 ){
              motorNum = currentLine.substring( motorPos+3, motorPos+4 ).toInt();
            }
            
            Serial.print("Action: ");
            Serial.println(action);
            Serial.print("Dist: ");
            Serial.println(dist);
            switch( action ){
              case 0: //TEST - spin anti-clockwise
                spinMotor( motorNum, openVent, dist );
                break;
              case 1: //TEST - spin clockwise
                spinMotor( motorNum, closeVent, dist );
                break;
              case 2: //close completely (zero)
                ensureOpen( motorNum, 0 );
                findZero( motorNum, closeVent );
                break;
              case 3: //open completely (zero, if required, then open)
                moveMotorTo( motorNum, fullyOpenMicro );
                break;
              case 4: //open 50% (zero, if required, then open)
                ensureOpen( motorNum, (fullyOpenMicro/2) );
                moveMotorTo( motorNum, (fullyOpenMicro/2) );
                break;
              case 5: //open 25% (zero, if required, then open)
                ensureOpen( motorNum, (fullyOpenMicro/4) );
                moveMotorTo( motorNum, (fullyOpenMicro/4) );
                break;
            }

            //save new state to EEPROM after any movement
            bool eepromUpdated = false;
            for (int i=0; i < NUM_MOTORS; i++){
              if( -1 != stepperPos[i] ){ //ignore unknown positions
                float percent = (stepperPos[i] / float(fullyOpenMicro)) * 100.0;
                int intRatio = percent + 0.5; //round up to whole number
                int lastVal = EEPROM.read(i);
                if( lastVal != intRatio ){ //only write if it has changed (100,000 write limit on EEPROM)
                  EEPROM.write( i, intRatio );
                  eepromUpdated = true;
                }
              }
            }
            if( eepromUpdated ){
              EEPROM.commit();
              Serial.println("EEPROM state updated");
            }
          }
        }
      }
    }
  }

//  Serial.print("Button: ");
//  Serial.println(digitalRead(endStop[0]));
}

/*
 * Move a motor to an absolute position (in microSteps)
 */
void moveMotorTo( int motor, int pos ){
  int motorPos = stepperPos[motor];
  
  Serial.print("Start Position: ");
  Serial.println(stepperPos[motor]);
  if( motorPos <= 0 ){ //motor position unknown
    Serial.println("Zeroing");
    findZero( motor, closeVent );
    motorPos = 0;
  }

  int dist = pos - motorPos;
  int dir = openVent;
  if( dist < 0 ){
    dir = closeVent;
    dist = -dist;
  }

  Serial.print("Distance: ");
  Serial.println(dist);
  Serial.print("Direction: ");
  Serial.println(dir);

  spinMotor( motor, dir, dist );

  Serial.print("End Position: ");
  Serial.println(stepperPos[motor]);
}

/*
 * Spin a motor a relative amount (in microSteps)
 */
void spinMotor( int motor, int dir, int dist ){
  digitalWrite(stepperPower[motor], LOW);
  digitalWrite(dirPins[motor], dir);
  if( 0 == dist ){
    return;
  }
  int addAmount = 1;
  if( closeVent == dir ){
    addAmount = -1;
  }
  
  Serial.print("step delay: ");
  Serial.println(stpDelay);

  for (int i=0; i < dist; i++){
    Serial.println("HIGH");
    digitalWrite(stepper[motor], HIGH);
    stepperPos[motor] += addAmount;
    delayMicroseconds(stpDelay);
    Serial.println("LOW");
    digitalWrite(stepper[motor], LOW );
    delayMicroseconds(stpDelay);
    //safety - stop immediately and reset zero if endstop is pressed
    if( i > 50 && closeVent == dir && LOW == digitalRead(endStop[motor]) ){
      Serial.println("Endstop triggered - phantom checking...");
      delay(1);
      if( LOW == digitalRead(endStop[motor]) ){ //might be phantom trigger (long cables)
        Serial.println("Unexpectedly hit endstop.");
        zeroMotor( motor );
         //the break seems to end the entire function sometimes, so len't make sure the motor is off
        digitalWrite(stepperPower[motor], HIGH);
        break;
      }
    }
  }
  
  digitalWrite(stepperPower[motor], HIGH);
  Serial.print("Current Position: ");
  Serial.println(stepperPos[motor]);
}

// Set motor position tracker to 0 (called when endstop is triggered)
void zeroMotor( int motor ){
  stepperPos[motor] = 0;
}

// Close a vent
void findZero( int motor, int dir, int posMoved ){
  Serial.print("FindZero called for motor: ");
  Serial.println(motor);
  digitalWrite(stepperPower[motor], LOW);
  
  Serial.print("step delay: ");
  Serial.println(stpDelay);

  digitalWrite(dirPins[motor], dir);
  while( HIGH == digitalRead(endStop[motor]) && posMoved < (fullyOpenMicro + 100) ){
    digitalWrite(stepper[motor], HIGH);
    delayMicroseconds(stpDelay);
    digitalWrite(stepper[motor],LOW );
    delayMicroseconds(stpDelay);
    posMoved += 1; //switch likely failed if this exceeds fullyOpenMicro
  }

  //handle issues with long cables causing phanton triggers on endstop
  delay(1);
  if( HIGH == digitalRead(endStop[motor]) && posMoved < fullyOpenMicro ){
    Serial.println("Phantom trigger detected - loop again");
    findZero( motor, dir, posMoved );
  }
  Serial.println("FindZero finished");
  Serial.print("Endstop Status: ");
  Serial.println(digitalRead(endStop[motor]));
  Serial.print("PosMoved: ");
  Serial.print(posMoved);
  Serial.print(" / ");
  Serial.println(fullyOpenMicro);

  digitalWrite(stepperPower[motor], HIGH);
  zeroMotor( motor );
}

// Ensure at least one other vent (ie. total >= 100%) is open before closing
void ensureOpen( int closeMotor, int openStatus ){
  //work out total open position
  for (int i=0; i < NUM_MOTORS; i++){
    if( i != closeMotor ){
      openStatus += stepperPos[i];
    }
  }
  if( openStatus < 0 ){
    openStatus = 0;
  }
  Serial.print("Total Open: ");
  Serial.println(openStatus);

  float ratio = openStatus / float(fullyOpenMicro);
  int openPercent = ratio * 100;
  Serial.print("Open Percent: ");
  Serial.println(openPercent);
  
  if( openPercent < 100 ){
    // Open other vents now - loop through and find first vent which isn't this one
    for (int i=0; i < NUM_MOTORS; i++){
      if( i != closeMotor ){
        moveMotorTo( i, fullyOpenMicro );
        break;
      }
    }
  }
}
