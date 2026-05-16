/*
 *
 #ESP32 controller for AC Vents
 Peter Wells - March 2020

Version 1.7.0
 Moved motor controls to a second FreeRTOS thread (Core 0)
 Requests return instantly, reporting target position
 Added ability to set fully open position per-motor
 More resilient phantom endstop detection

Version 1.6.0
 Added acceleration ramping to the motor

 Version 1.5.0
  Moved configuration to config.h
  Added WiFi connection management

 Version 1.4.0
  Startup state determined by endstop status (rather than EEPROM)

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
#include <esp_task_wdt.h>
#include "config.h"

// Configuration variables are now loaded from config.h
// Copy config_sample.h to config.h and update with your actual values

// Motor trigger values (invert if motor spins the wrong direction)
const int openVent = LOW;
const int closeVent = HIGH;

volatile int stepperPos[NUM_MOTORS];      // Current actual position (updated by motor task)
volatile int targetPos[NUM_MOTORS];       // Target position (set by request handler)
volatile bool closeRequested[NUM_MOTORS]; // True when close-to-endstop is requested
volatile bool motorBusy[NUM_MOTORS];      // True while a motor is actively moving
/** Set true when a raw endstop press is seen before checkEndstopStatus() confirms (phantom path). */
volatile bool endstopTriggerPending[NUM_MOTORS];
/** Microstepping-compensated full-open position per motor (steps). Filled in setup() from fullyOpen[] */
int fullyOpenMicro[NUM_MOTORS];
int stpDelay = stepDelay / microStepping; //set delay compensating for MicroStepping

WiFiServer server(80);
TaskHandle_t motorTaskHandle = NULL;

//function defaults
void spinMotor( int motor=0, int dir=LOW, int dist=1 );
void findZero( int motor=0, int dir=LOW );
void ensureOpen( int closeMotor=0, int openStatus=0 );
void checkWiFiConnection();
void motorTask( void* parameter );
void serviceWatchdogAndYield( int stepCounter );
bool checkEndstopStatus( int motor );
//end function defaults

/**
 * @brief True if the closed endstop reads pressed (active LOW).
 * @param motor Motor index in endStop[].
 */
static bool endstopPressed(int motor) {
  return digitalRead(endStop[motor]) == LOW;
}

/**
 * @brief True when homing may continue stepping along the current segment.
 *
 * When homingPressEvents is null: raw pin HIGH means released.
 * When homingPressEvents is non-null (findZero): updates endstopTriggerPending and
 * homingPressEvents for phantom deferral — returns true while the pin is HIGH, or
 * while a LOW read is not yet confirmed by checkEndstopStatus(). Returns false only
 * when checkEndstopStatus() confirms the endstop (stop homing). On HIGH, clears
 * pending and resets homingPressEvents.
 *
 * @param motor              Motor index in endStop[].
 * @param homingPressEvents  Optional; when set, findZero phantom / confirm logic applies.
 * @return False only when the endstop is debounce-confirmed; otherwise keep stepping.
 */
static bool endstopReleased(int motor, int* homingPressEvents = nullptr) {
  const bool pinHigh = digitalRead(endStop[motor]) == HIGH;

  if (homingPressEvents == nullptr) {
    return pinHigh;
  }

  if (pinHigh) {
    endstopTriggerPending[motor] = false;
    *homingPressEvents = 0;
    return true;
  }

  endstopTriggerPending[motor] = true;
  *homingPressEvents += 1;

  if (*homingPressEvents < ENDSTOP_PHANTOM_CONFIRM_TRIGGERS) {
    return true;
  }

  if (checkEndstopStatus( motor )) {
    endstopTriggerPending[motor] = false;
    return false;
  }

  Serial.println("Phantom trigger detected - continuing...");
  *homingPressEvents = 0;
  return true;
}

/** Delay between endstop validation samples (milliseconds). */
static const int ENDSTOP_SAMPLE_DELAY_MS = 2;
/** Number of endstop validation samples taken per check. */
static const int ENDSTOP_SAMPLE_COUNT = 10;
/** Minimum pressed samples required to confirm a trigger. */
static const int ENDSTOP_PRESSED_THRESHOLD = 7;

// Check and maintain WiFi connection
void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection lost. Attempting to reconnect...");
    WiFi.disconnect();
    delay(1000);

    if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
      Serial.println("STA Failed to configure");
    }

    WiFi.begin(ssid, password);

    unsigned long wifiStartTime = millis();
    const unsigned long WIFI_TIMEOUT = 10000; // 10 second timeout

    while (WiFi.status() != WL_CONNECTED && (millis() - wifiStartTime) < WIFI_TIMEOUT) {
      delay(500);
      Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("");
      Serial.println("WiFi reconnected!");
      Serial.println("IP address: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("");
      Serial.println("WiFi reconnection failed!");
    }
  }
}

void setup()
{
  Serial.begin(115200);
  // initialize EEPROM with predefined size (for power outage recovery of position)
  // EEPROM.begin(NUM_MOTORS);
  
  for (int i=0; i < NUM_MOTORS; i++){
    fullyOpenMicro[i] = fullyOpen[i] * microStepping;
    //setup pin modes for each motor
    pinMode(dirPins[i], OUTPUT);      // set stepper pin mode
    pinMode(stepper[i], OUTPUT);      // set stepper pin mode
    pinMode(stepperPower[i], OUTPUT);      // set stepperPower pin mode
    digitalWrite(stepperPower[i], HIGH);
    pinMode(endStop[i], INPUT_PULLUP);      // endstops are active LOW; pull-up prevents floating inputs

    // Setup initial Stepper Pos to either 0 or 100% based on whether endstop is triggered
    stepperPos[i] = fullyOpenMicro[i]; // Default to vent open
    if( checkEndstopStatus( i ) ){
      stepperPos[i] = 0; // Vent is closed
    }

    targetPos[i] = stepperPos[i];
    closeRequested[i] = false;
    motorBusy[i] = false;
   
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

  xTaskCreatePinnedToCore(
    motorTask,
    "MotorTask",
    4096,
    NULL,
    1,
    &motorTaskHandle,
    0  // Core 0 (loop runs on Core 1)
  );
}

void loop(){
  checkWiFiConnection();  // Check and maintain WiFi connection

  WiFiClient client = server.available();      // listen for incoming clients

  if (client) {
    String currentLine = "";                   // make a String to hold incoming data from the client
    int respType = 0; // 0=html, 1=json
    unsigned long requestStartTime = millis(); // Add timeout tracking
    const unsigned long REQUEST_TIMEOUT = 5000; // 5 second timeout
    bool requestProcessed = false;
    const int MAX_LINE_LENGTH = 2048; // Prevent memory exhaustion

    while (client.connected() && !requestProcessed && (millis() - requestStartTime) < REQUEST_TIMEOUT) {
      if (client.available()) {                // if there's client data
        char c = client.read();                // read a byte
        if( c == '\r' && currentLine.startsWith("GET /") ){
          // Check to see if the client request was "GET /H" or "GET /L":
          
          // Basic request validation
          if (!currentLine.startsWith("GET /")) {
            Serial.println("Invalid request format");
            client.stop();
            return;
          }

          int typePos = currentLine.indexOf("&t="); // 1-digit
          if( typePos > 0 ){
            respType = currentLine.substring( typePos+3, typePos+4 ).toInt(); // type=1 means JSON response
          }

          int actionPos = currentLine.indexOf("?a="); // 1-digit
          if( actionPos > 0 ){
            int action = currentLine.substring( actionPos+3, actionPos+4 ).toInt();

            // Validate action is within valid range
            if (action < 0 || action > 6) {
              Serial.println("Invalid action parameter");
              client.stop();
              return;
            }

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

            // Validate motor number is within valid range
            if (motorNum < 0 || motorNum >= NUM_MOTORS) {
              Serial.println("Invalid motor number");
              client.stop();
              return;
            }
            
            Serial.print("Action: ");
            Serial.println(action);
            Serial.print("Dist: ");
            Serial.println(dist);
            switch( action ){
              case 0: //TEST - spin anti-clockwise
                targetPos[motorNum] = stepperPos[motorNum] + distAmount;
                break;
              case 1: //TEST - spin clockwise
                targetPos[motorNum] = stepperPos[motorNum] - distAmount;
                break;
              case 2: //close completely (zero)
                closeRequested[motorNum] = true;
                targetPos[motorNum] = 0;
                break;
              case 3: //open completely
                targetPos[motorNum] = fullyOpenMicro[motorNum];
                break;
              case 4: //open 50%
                targetPos[motorNum] = fullyOpenMicro[motorNum] / 2;
                break;
              case 5: //open 25%
                targetPos[motorNum] = fullyOpenMicro[motorNum] / 4;
                break;
              case 6: //open to ratio set by &d= (eg. ?d=050:open50%, ?d=000:close, ?d=100:open100%)
                targetPos[motorNum] = (fullyOpenMicro[motorNum] / 100) * dist;
                if (targetPos[motorNum] == 0) {
                  closeRequested[motorNum] = true;
                }
           
                break;
            }
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
                  if( targetPos[i] > 0 ){
                    Serial.println("Calculating position...");
                    Serial.print("TargetPos: ");
                    Serial.println(targetPos[i]);
                    Serial.print("EndPos: ");
                    Serial.println(fullyOpenMicro[i]);
                    Serial.print("Ratio: ");
                    float ratio = targetPos[i] / float(fullyOpenMicro[i]);
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
                  if( targetPos[i] > 0 ){
                    Serial.println("Calculating position...");
                    Serial.print("TargetPos: ");
                    Serial.println(targetPos[i]);
                    Serial.print("EndPos: ");
                    Serial.println(fullyOpenMicro[i]);
                    Serial.print("Ratio: ");
                    float ratio = targetPos[i] / float(fullyOpenMicro[i]);
                    Serial.println(ratio);
                    pos[i] = ratio * 100;
                  }
                  // Print: "0":{"name":"Vent #1","pos":50,"busy":false},
                  client.print( "\"" );
                  client.print( i );
                  client.print( "\":{\"name\":\"" );
                  client.print( ventNames[i] );
                  client.print( "\",\"pos\":" );
                  client.print( pos[i] );
                  client.print( ",\"busy\":" );
                  client.print( motorBusy[i] ? "true" : "false" );
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

            requestProcessed = true; // Mark request as processed
            break;  // break out of the while loop:
          } else {    // if you got a newline, then clear currentLine:
            currentLine = "";
          }
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          if (currentLine.length() < MAX_LINE_LENGTH) {  // Prevent memory exhaustion
            currentLine += c;       // add it to the end of the currentLine
          } else {
            Serial.println("Request line too long - possible attack or malformed request");
            client.stop();
            return;
          }
        }
      }
    }

    // Handle timeout - close connection if request wasn't processed
    if (!requestProcessed && (millis() - requestStartTime) >= REQUEST_TIMEOUT) {
      Serial.println("Request timeout - closing connection");
      client.stop();
    }
  }
}

/**
 * @brief FreeRTOS task that drives motors toward their target positions.
 *
 * Runs on Core 0 in a continuous loop. For each motor it checks whether a
 * close-to-endstop was requested (closeRequested) or whether targetPos
 * differs from stepperPos, and performs the appropriate move. If an endstop
 * interrupts a positional move and no new target has been set, targetPos is
 * synced to the actual position to avoid retry loops.
 *
 * @param parameter Unused FreeRTOS task parameter.
 */
void motorTask( void* parameter ){
  esp_task_wdt_add(NULL);
  for(;;){
    esp_task_wdt_reset();
    for( int i = 0; i < NUM_MOTORS; i++ ){
      if( closeRequested[i] ){
        motorBusy[i] = true;
        closeRequested[i] = false;
        ensureOpen( i, 0 );
        findZero( i, closeVent );
        motorBusy[i] = false;
      } else if( targetPos[i] != stepperPos[i] ){
        motorBusy[i] = true;
        int target = targetPos[i];
        ensureOpen( i, target );
        moveMotorTo( i, target );
        // Sync target if endstop interrupted the move and no new target was set
        if( stepperPos[i] != target && targetPos[i] == target ){
          targetPos[i] = stepperPos[i];
        }
        motorBusy[i] = false;
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

/*
 * Move a motor to an absolute position (in microSteps)
 */
void moveMotorTo( int motor, int pos ){
  int motorPos = stepperPos[motor];
  
  Serial.print("Start Position: ");
  Serial.println(stepperPos[motor]);
  if( motorPos < 0 ){ //motor position unknown
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

/**
 * @brief Feed this task watchdog and briefly yield so IDLE tasks can run.
 *
 * @param stepCounter Current loop step index.
 */
void serviceWatchdogAndYield( int stepCounter ){
  if( stepCounter % 100 == 0 ){
    esp_task_wdt_reset();
    vTaskDelay(1 / portTICK_PERIOD_MS);
  }
}

/**
 * @brief Spin a motor a relative amount (in microSteps) with linear acceleration/deceleration.
 *
 * The ramp zone length uses this motor's fullyOpenMicro[motor] (5% of full travel), capped at
 * half the total distance so accel and decel zones never overlap. Step delay
 * linearly interpolates from ACCEL_START_MULTIPLIER * stpDelay down to stpDelay.
 *
 * @param motor Motor index.
 * @param dir   Direction pin value (openVent / closeVent).
 * @param dist  Number of microsteps to move.
 */
void spinMotor( int motor, int dir, int dist ){
  static const int ACCEL_START_MULTIPLIER = 3;

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

  int rampSteps = fullyOpenMicro[motor] / 20;
  if( rampSteps > dist / 2 ){
    rampSteps = dist / 2;
  }
  int maxDelay = stpDelay * ACCEL_START_MULTIPLIER;

  Serial.print("step delay: ");
  Serial.println(stpDelay);
  Serial.print("ramp steps: ");
  Serial.println(rampSteps);

  for (int i=0; i < dist; i++){
    int currentDelay = stpDelay;
    if( rampSteps > 0 ){
      int rampPos = min(i, dist - 1 - i);
      if( rampPos < rampSteps ){
        currentDelay = maxDelay - (long)(maxDelay - stpDelay) * rampPos / rampSteps;
      }
    }

    digitalWrite(stepper[motor], HIGH);
    stepperPos[motor] += addAmount;
    delayMicroseconds(currentDelay);
    digitalWrite(stepper[motor], LOW );
    delayMicroseconds(currentDelay);

    // Keep watchdog healthy and allow IDLE0 to run during long moves.
    serviceWatchdogAndYield(i);

    //safety - stop immediately and reset zero if either endstop is pressed
    if( i > 50 && closeVent == dir && endstopPressed(motor) ){
      Serial.println("Endstop triggered - phantom checking...");
      Serial.println("Endstop triggered - phantom checking...");
      if( checkEndstopStatus( motor ) ){ //might be phantom trigger (long cables)
        Serial.println("Unexpectedly hit endstop.");
        zeroMotor( motor );
         //the break seems to end the entire function sometimes, so let's make sure the motor is off
        digitalWrite(stepperPower[motor], HIGH);
        break;
      }
    }
  }
  
  digitalWrite(stepperPower[motor], HIGH);
  Serial.print("Current Position: ");
  Serial.println(stepperPos[motor]);
}

/**
 * @brief Confirm an endstop trigger by sampling over time to reject noise.
 *
 * @param motor Motor index.
 * @return true if the trigger is confirmed, false otherwise.
 */
bool checkEndstopStatus( int motor ){
    int pressedSamples = 0;

    for( int i = 0; i < ENDSTOP_SAMPLE_COUNT; i++ ){
      if( endstopPressed(motor) ){
        pressedSamples += 1;
      }
      delay(ENDSTOP_SAMPLE_DELAY_MS);
    }

    return pressedSamples >= ENDSTOP_PRESSED_THRESHOLD;
}

// Set motor position tracker to 0 (called when endstop is triggered)
void zeroMotor( int motor ){
  stepperPos[motor] = 0;
}

/**
 * @brief Close a vent by driving toward the endstop with start acceleration
 *        and position-based deceleration toward 25% speed before the expected
 *        zero (from stepperPos at entry).
 *
 * First steps ramp delay from ACCEL_START_MULTIPLIER * stpDelay down to
 * stpDelay (same zone length as spinMotor). In the last approach zone before
 * the tracked position would reach zero, delay ramps from stpDelay up to
 * 4 * stpDelay (25% step rate). If the endstop is still not reached after the
 * expected zero, stepping continues at that crawl speed until the endstop or
 * step limit.
 *
 * Phantom deferral and confirmation live in endstopReleased(motor, &homingPressEvents).
 * If the step limit (fullyOpenMicro[motor] + 50) is reached without a confirmed
 * endstop, the motor is assumed to be at zero (endstop likely failed).
 *
 * @param motor Motor index.
 * @param dir   Direction pin value (typically closeVent).
 */
void findZero( int motor, int dir ){
  static const int ACCEL_START_MULTIPLIER = 3;
  /** Step delay multiplier at end of approach (25% speed = 4x period). */
  static const int FINDZERO_CRAWL_DELAY_MULT = 4;

  Serial.print("FindZero called for motor: ");
  Serial.println(motor);
  digitalWrite(stepperPower[motor], LOW);

  Serial.print("step delay: ");
  Serial.println(stpDelay);

  const int estStepsToZero = stepperPos[motor];
  int rampAccelSteps = fullyOpenMicro[motor] / 20;
  int decelRampSteps = min( rampAccelSteps, max( 1, abs( estStepsToZero ) ) );
  int maxDelay = stpDelay * ACCEL_START_MULTIPLIER;
  const int crawlDelay = stpDelay * FINDZERO_CRAWL_DELAY_MULT;
  int posMoved = 0;
  int stepLimit = fullyOpenMicro[motor] + 50;
  int homingPressEvents = 0;

  endstopTriggerPending[motor] = false;

  Serial.print("ramp accel steps: ");
  Serial.println(rampAccelSteps);
  Serial.print("ramp decel steps: ");
  Serial.println(decelRampSteps);

  digitalWrite(dirPins[motor], dir);

  // Step while endstop is released and within step limit
  while( endstopReleased( motor, &homingPressEvents ) && posMoved < stepLimit ){
    int currentDelay = stpDelay;
    if( posMoved < rampAccelSteps ){
      currentDelay = maxDelay - (long)(maxDelay - stpDelay) * posMoved / rampAccelSteps;
    }

    const int remaining = estStepsToZero - posMoved;
    if( remaining < decelRampSteps ){
      int decelDelay;
      if( remaining <= 0 ){
        decelDelay = crawlDelay;
      } else {
        decelDelay = stpDelay + (long)(crawlDelay - stpDelay) * (decelRampSteps - remaining) / decelRampSteps;
      }
      if( decelDelay > currentDelay ){
        currentDelay = decelDelay;
      }
    }

    digitalWrite(stepper[motor], HIGH);
    delayMicroseconds(currentDelay);
    digitalWrite(stepper[motor], LOW);
    delayMicroseconds(currentDelay);
    posMoved += 1;

    // Keep watchdog healthy and allow IDLE0 to run during long zeroing.
    serviceWatchdogAndYield(posMoved);
  }

  if( posMoved >= stepLimit ){
    Serial.println("WARNING: Endstop never triggered - assuming zero position");
  } else {
    Serial.println("Endstop confirmed");
  }

  Serial.println("FindZero finished");
  Serial.print("Endstop Status: ");
  Serial.println(digitalRead(endStop[motor]));
  Serial.print("PosMoved: ");
  Serial.print(posMoved);
  Serial.print(" / ");
  Serial.println(fullyOpenMicro[motor]);

  digitalWrite(stepperPower[motor], HIGH);
  endstopTriggerPending[motor] = false;
  zeroMotor( motor );
}

// Ensure at least one other vent is open before closing
void ensureOpen( int closeMotor, int openStatus ){
  // DISABLED - we have a permanent open vent
  // // New simple method - make sure at least one of the endstops is not depressed
  // bool oneIsOpen = false;
  // for (int i=0; i < NUM_MOTORS; i++){
  //   if( !checkEndstopStatus( i ) && i != closeMotor ){
  //     oneIsOpen = true; // Vent is open
  //   }
  // } //end for loop

  // if( !oneIsOpen ){
  //   // Open other vents now - loop through and find last vent which isn't this one
  //   for (int i=(NUM_MOTORS-1); i >= 0; i--){
  //     if( i != closeMotor ){
  //       moveMotorTo( i, fullyOpenMicro[i] );
  //       break;
  //     }
  //   }
  // }
}
