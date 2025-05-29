/*
 *
 #ESP32 controller for AC Vents
 Peter Wells - March 2020

 Version 1.4.0
  Startup state determined by endstop status

 Version 1.3.0
  Added simple JSON API

 Version 1.2.0
  Support microstepping

 Version 1.1.0
  Added debouncing
  Swap buttons with Javascript for <a href... for IOS support
  
 Version 1.0.0
  Initial version
*/
#include <WiFi.h>
//#include <EEPROM.h>


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
  "Room #1",
  "Room #2",
  "Room #3"
};

// Current microstepping setting (no need to vary stepDelay or fullyOpen)
const int microStepping = 8; //1=no microStepping, 32=max (on DRV8825)

// Amount in steps (200 per rotation) to open the vent (ignoring microStepping)
const int fullyOpen = 650;

//Speed of the motor (in Microseconds)
const int stepDelay = 2000; //delay between steps (1000 = fastest, 5000 = pretty slow)

// Wifi / Network setings
const char* ssid    = "SSID"; // Primary WiFi network
const char* password = "password123";

IPAddress local_IP(192, 168, 2, 110);
IPAddress gateway(192, 168, 2, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(192, 168, 1, 90); //optional
IPAddress secondaryDNS(1, 1, 1, 1); //optional
// ---------------------------------------------------------------------------------------------------------------------
// -- END USER CONFIGURATION --
// ---------------------------------------------------------------------------------------------------------------------

// Motor trigger values (invert if motor spins the wrong direction)
const int openVent = LOW;
const int closeVent = HIGH;

int stepperPos[NUM_MOTORS]; // Current position of stepper motors
int fullyOpenMicro = fullyOpen * microStepping; // MicroStepping compensated fully open position (in steps)
int stpDelay = stepDelay / microStepping; //set delay compensating for MicroStepping

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
  // EEPROM.begin(NUM_MOTORS);
  
  for (int i=0; i < NUM_MOTORS; i++){
    //setup pin modes for each motor
    pinMode(dirPins[i], OUTPUT);      // set stepper pin mode
    pinMode(stepper[i], OUTPUT);      // set stepper pin mode
    pinMode(stepperPower[i], OUTPUT);      // set stepperPower pin mode
    digitalWrite(stepperPower[i], HIGH);
    pinMode(endStop[i], INPUT);      // set endstop pin mode
    
    // Setup initial Stepper Pos to either 0 or 100% based on whether endstop is triggered
    stepperPos[i] = fullyOpenMicro; // Default to vent open
    if( checkEndstopStatus( i, 0 ) ){
      stepperPos[i] = 0; // Vent is closed
    }
   
  } //end for loop

  // We start by connecting to a WiFi network
  Serial.println();
  Serial.println("Starting WiFi ");
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
      Serial.println("STA Failed to configure");
  }
  
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected! ");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("ESP Mac Address: ");
  Serial.println(WiFi.macAddress());
  
  server.begin();
}

void loop(){
  WiFiClient client = server.available();      // listen for incoming clients

  if (client) {
    //Serial.println("new client");
    String currentLine = "";                   // make a String to hold incoming data from the client
    int respType = 0; // 0=html, 1=json
    while (client.connected()) {
      if (client.available()) {                // if there's client data
        char c = client.read();                // read a byte
        if( c == '\r' && currentLine.startsWith("GET /") ){
          //Serial.println(currentLine);
          // Check to see if the client request was "GET /H" or "GET /L":
          
          int typePos = currentLine.indexOf("&t="); // 1-digit
          if( typePos > 0 ){
            respType = currentLine.substring( typePos+3, typePos+4 ).toInt(); // type=1 means JSON response
          }

          int actionPos = currentLine.indexOf("?a="); // 1-digit
          if( actionPos > 0 ){
            int action = currentLine.substring( actionPos+3, actionPos+4 ).toInt();

            int dist = 0;
            int distPos = currentLine.indexOf("&d="); // 3-digits (eg. 050)
            if( distPos > 0 ){
              dist = currentLine.substring( distPos+3, distPos+6 ).toInt();
            }
            int distAmount = dist * 100;

            int motorNum = 0;
            int motorPos = currentLine.indexOf("&m="); // 1-digit
            if( motorPos > 0 ){
              motorNum = currentLine.substring( motorPos+3, motorPos+4 ).toInt();
            }
            
            Serial.print("Action: ");
            Serial.println(action);
            Serial.print("Dist: ");
            Serial.println(dist);
            switch( action ){
              case 0: //TEST - spin anti-clockwise
                spinMotor( motorNum, openVent, distAmount );
                break;
              case 1: //TEST - spin clockwise
                spinMotor( motorNum, closeVent, distAmount );
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
              case 6: //open to ratio set by &d= (eg. ?d=050:open50%, ?d=000:close, ?d=100:open100%)
                int moveTo = (fullyOpenMicro / 100) * dist; // remove * 100 applied above (for cases 0 and 1 test code)
                ensureOpen( motorNum, moveTo );
                moveMotorTo( motorNum, moveTo );
                break;
            }

            //save new state to EEPROM after any movement
            // bool eepromUpdated = false;
            // for (int i=0; i < NUM_MOTORS; i++){
            //   if( -1 != stepperPos[i] ){ //ignore unknown positions
            //     float percent = (stepperPos[i] / float(fullyOpenMicro)) * 100.0;
            //     int intRatio = percent + 0.5; //round up to whole number
            //     int lastVal = EEPROM.read(i);
            //     if( lastVal != intRatio ){ //only write if it has changed (100,000 write limit on EEPROM)
            //       EEPROM.write( i, intRatio );
            //       eepromUpdated = true;
            //     }
            //   }
            // }
            // if( eepromUpdated ){
            //   EEPROM.commit();
            //   Serial.println("EEPROM state updated");
            // }
          }
      } else if (c == '\n') {                  // check for newline character,
          if (currentLine.length() == 0) {     // if line is blank it means its the end of the client HTTP request
            int pos[NUM_MOTORS];
            if( respType == 0 ){
                Serial.println("Sending HTML response.");
                // HTML response
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
            } else {
                Serial.println("Sending JSON response.");
                // JSON response - eg. {"0":{"name":"Vent #1","pos":50},"1":...}
                client.println("HTTP/1.1 200 OK");
                client.println("Content-Type: application/json;charset=utf-8");
                client.println("Server: Arduino");
                client.println("Connection: close");
                client.println();
                client.print("{");
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
                  // Print: "0":{"name":"Vent #1","pos":50},
                  client.print( "\"" );
                  client.print( i );
                  client.print( "\":{\"name\":\"" );
                  client.print( ventNames[i] );
                  client.print( "\",\"pos\":" );
                  client.print( pos[i] );
                  client.print( "}" );
                  if( i+1 < NUM_MOTORS ){ //add trailing comma to all but the last motor
                      client.print( "," );
                  }

                  Serial.print("Position: ");
                  Serial.println(pos[i]);
                }
                client.print("}");
            }
            
            client.println(); // The HTTP response ends with an extra blank line:
          
            break;  // break out of the while loop:
          } else {    // if you got a newline, then clear currentLine:
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;       // add it to the end of the currentLine
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
      digitalWrite(stepperPower[motor], HIGH); //turn off the motor
      return;
  }
  int addAmount = 1;
  if( closeVent == dir ){
    addAmount = -1;
  }
  
  Serial.print("step delay: ");
  Serial.println(stpDelay);

  for (int i=0; i < dist; i++){
    digitalWrite(stepper[motor], HIGH);
    stepperPos[motor] += addAmount;
    delayMicroseconds(stpDelay);
    digitalWrite(stepper[motor], LOW );
    delayMicroseconds(stpDelay);
    //safety - stop immediately and reset zero if endstop is pressed
    if( i > 50 && closeVent == dir && LOW == digitalRead(endStop[motor]) ){
      Serial.println("Endstop triggered - phantom checking...");
      if( checkEndstopStatus( motor, 0 ) ){ //might be phantom trigger (long cables)
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

// Check that endstop is pressed (includes 10x 100ms phantom press checks)
bool checkEndstopStatus( int motor, int counter ){
    bool is_triggered = true;
    delayMicroseconds(100);
    if( HIGH == digitalRead(endStop[motor]) ){
        return false;
    } else if( counter < 10 ){
        is_triggered = checkEndstopStatus( motor, (counter+1) );
    }
    
    return is_triggered;
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
  while( HIGH == digitalRead(endStop[motor]) && posMoved < (fullyOpenMicro + 200) ){
    digitalWrite(stepper[motor], HIGH);
    delayMicroseconds(stpDelay);
    digitalWrite(stepper[motor],LOW );
    delayMicroseconds(stpDelay);
    posMoved += 1; //switch likely failed if this exceeds fullyOpenMicro
  }

  //handle issues with long cables causing phanton triggers on endstop
  Serial.println("Endstop triggered - phantom checking...");
  if( !checkEndstopStatus( motor, 0 ) && posMoved < (fullyOpenMicro + 200) ){ //might be phantom trigger (long cables)
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

// Ensure at least one other vent (ie. total >= 70%) is open before closing
void ensureOpen( int closeMotor, int openStatus ){
  // //work out total open position
  // for (int i=0; i < NUM_MOTORS; i++){
  //   if( i != closeMotor ){
  //     openStatus += stepperPos[i];
  //   }
  // }
  // if( openStatus < 0 ){
  //   openStatus = 0;
  // }
  // Serial.print("Total Open: ");
  // Serial.println(openStatus);

  // float ratio = openStatus / float(fullyOpenMicro);
  // int openPercent = ratio * 100;
  // Serial.print("Open Percent: ");
  // Serial.println(openPercent);
  
  // if( openPercent < 70 ){
  //   // Open other vents now - loop through and find first vent which isn't this one
  //   for (int i=0; i < NUM_MOTORS; i++){
  //     if( i != closeMotor ){
  //       moveMotorTo( i, fullyOpenMicro );
  //       break;
  //     }
  //   }
  // }

  // New simple method - make sure at least one of the endstops is not depressed
  bool oneIsOpen = false;
  for (int i=0; i < NUM_MOTORS; i++){
    // Setup initial Stepper Pos to either 0 or 100% based on whether endstop is triggered
    stepperPos[i] = fullyOpenMicro; // Default to vent open
    if( !checkEndstopStatus( i, 0 ) ){
      oneIsOpen = true; // Vent is open
    }
  } //end for loop

  if( !oneIsOpen ){
    // Open other vents now - loop through and find last vent which isn't this one
    for (int i=(NUM_MOTORS-1); i >= 0; i--){
      if( i != closeMotor ){
        moveMotorTo( i, fullyOpenMicro );
        break;
      }
    }
  }
}
