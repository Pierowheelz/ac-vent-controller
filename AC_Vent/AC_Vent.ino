/*
 * ESP32 controller for AC Vents (TMC2209 + StallGuard4 edition)
 * Peter Wells - March 2020 .. April 2026
 *
 * Version 2.0.3
 *  motorRequiresHoming renamed to motorHomed (inverse): two flags remain — motorPositionKnown
 *  (HTTP just set/confirmed target) vs motorHomed (mechanical zero known this boot); both needed.
 *
 * Version 2.0.2
 *  "Stay at 100%" first command: motorPositionKnown + not-yet-homed triggers findZero then
 *  move to target even when target already matched assumed position (avoids boot-time motion).
 *
 * Version 2.0.1
 *  Lazy homing: each motor assumes fully open until the first move or explicit close; then
 *  StallGuard findZero establishes zero.
 *
 * Version 2.0.0
 *  Switched to the TMC2209 stepper hat:
 *    - Driver configuration (microstepping, current, direction, StallGuard) is sent via the
 *      shared single-wire UART bus at startup; STEP and EN remain on dedicated GPIOs.
 *    - findZero() uses StallGuard4 to detect the closed mechanical limit (no endstops).
 *    - All endstop-related code paths have been removed.
 *
 * Version 1.7.0  Moved motor controls to a second FreeRTOS thread (Core 0)
 * Version 1.6.0  Added redundant endstop support, acceleration ramping
 * Version 1.5.0  Moved configuration to config.h, added Wi-Fi connection management
 * Version 1.4.0  Startup state determined by endstop status (rather than EEPROM)
 * Version 1.3.0  Added simple JSON API
 * Version 1.2.0  Support microstepping
 * Version 1.1.0  Added debouncing, JS button swap for iOS
 * Version 1.0.0  Initial version
 */

#include <Arduino.h>
#include <HardwareSerial.h>
#include <TMCStepper.h>
#include <WiFi.h>
#include <esp_task_wdt.h>

#include "config.h"

// ---------------------------------------------------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------------------------------------------------

void  checkWiFiConnection();
void  motorTask(void* parameter);
void  serviceWatchdogAndYield(int stepCounter);
void  setMotorDirection(int motor, bool closing);
void  enableMotor(int motor);
void  disableMotor(int motor);
void  pulseStep(int motor, int delayMicros);
void  configureDriver(int motor);
void  spinMotor(int motor, bool closing, int dist);
void  moveMotorTo(int motor, int pos);
void  findZero(int motor);
void  zeroMotor(int motor);
void  ensureOpen(int closeMotor, int openStatus);

// ---------------------------------------------------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------------------------------------------------

/** Logical "open" / "close" intent (independent of per-motor shaft inversion). */
constexpr bool DIR_OPEN  = false;
constexpr bool DIR_CLOSE = true;

/** EN pin levels for the TMC2209 (active LOW). */
constexpr uint8_t EN_ACTIVE  = LOW;   // motor energised
constexpr uint8_t EN_RELEASE = HIGH;  // motor coil current off

/** Acceleration ramp multiplier (start delay = stpDelay * ACCEL_START_MULTIPLIER). */
constexpr int ACCEL_START_MULTIPLIER = 3;
/** Crawl multiplier used as the motor approaches the expected zero during findZero. */
constexpr int FINDZERO_CRAWL_DELAY_MULT = 4;

/** Microstepping-compensated full-open position, in microsteps. */
const int fullyOpenMicro = fullyOpen * microStepping;
/** Microstepping-compensated step half-period, in microseconds. */
const int stpDelay       = stepDelay / microStepping;

/** Current measured position of each motor, in microsteps (updated by motorTask). */
volatile int  stepperPos[NUM_MOTORS];
/** Requested target position, in microsteps (set by request handler / startup). */
volatile int  targetPos[NUM_MOTORS];
/** True when a sensorless close-to-zero has been requested. */
volatile bool closeRequested[NUM_MOTORS];
/** True while a motor is actively moving. */
volatile bool motorBusy[NUM_MOTORS];
/**
 * False until StallGuard findZero() has succeeded for this motor since boot (mechanical zero known).
 * Distinct from motorPositionKnown: after homing, many HTTP actions set position fresh true while
 * motorHomed stays true so we do not re-stall on every identical command.
 */
volatile bool motorHomed[NUM_MOTORS];
/**
 * True after an HTTP request sets or confirms this motor's target position (including "stay at 100%").
 * motorTask clears it after acting. While !motorHomed, together with this flag it forces a reconcile
 * homing pass even when target already matched the assumed position; stays false at boot so idle does not home.
 */
volatile bool motorPositionKnown[NUM_MOTORS];

/** Shared half-duplex UART used to talk to every TMC2209 driver.
 *  Aliases the pre-defined ESP32 HardwareSerial instance selected via TMC_UART_NUM in config.h. */
#if TMC_UART_NUM == 0
HardwareSerial& tmcSerial = Serial;
#elif TMC_UART_NUM == 1
HardwareSerial& tmcSerial = Serial1;
#elif TMC_UART_NUM == 2
HardwareSerial& tmcSerial = Serial2;
#else
#error "TMC_UART_NUM must be 0, 1 or 2"
#endif

/** Allocated TMC2209 driver objects (one per motor). */
TMC2209Stepper* drivers[NUM_MOTORS] = { nullptr };

WiFiServer    server(80);
TaskHandle_t  motorTaskHandle = NULL;

// ---------------------------------------------------------------------------------------------------------------------
// Wi-Fi
// ---------------------------------------------------------------------------------------------------------------------

/**
 * @brief Reconnect to Wi-Fi if the current link has dropped.
 *        Applies the static IP configuration from config.h on every attempt.
 */
void checkWiFiConnection() {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  Serial.println("WiFi connection lost. Attempting to reconnect...");
  WiFi.disconnect();
  delay(1000);

  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("STA Failed to configure");
  }

  WiFi.begin(ssid, password);

  const unsigned long WIFI_TIMEOUT = 10000;
  unsigned long wifiStartTime = millis();
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

// ---------------------------------------------------------------------------------------------------------------------
// TMC2209 helpers
// ---------------------------------------------------------------------------------------------------------------------

/**
 * @brief Write the GCONF.shaft bit so the next STEP pulse moves in the requested direction.
 *
 * Uses per-motor inversion from `closeShaftDirection[]` so logical close/open semantics are
 * independent of the physical motor wiring.
 *
 * @param motor   Motor index.
 * @param closing True to drive toward the closed limit, false to drive toward fully-open.
 */
void setMotorDirection(int motor, bool closing) {
  const bool shaftValue = closing ? closeShaftDirection[motor] : !closeShaftDirection[motor];
  drivers[motor]->shaft(shaftValue);
  // Allow the shaft register write to take effect on the next step.
  delayMicroseconds(200);
}

/**
 * @brief Drive the motor's EN pin LOW so the coils are energised.
 * @param motor Motor index.
 */
void enableMotor(int motor) {
  digitalWrite(enablePins[motor], EN_ACTIVE);
}

/**
 * @brief Drive the motor's EN pin HIGH so the coils are released.
 * @param motor Motor index.
 */
void disableMotor(int motor) {
  digitalWrite(enablePins[motor], EN_RELEASE);
}

/**
 * @brief Emit a single STEP pulse with matching HIGH/LOW half-periods.
 *
 * @param motor       Motor index.
 * @param delayMicros Half-period delay in microseconds (HIGH and LOW each held this long).
 */
void pulseStep(int motor, int delayMicros) {
  digitalWrite(stepPins[motor], HIGH);
  delayMicroseconds(delayMicros);
  digitalWrite(stepPins[motor], LOW);
  delayMicroseconds(delayMicros);
}

/**
 * @brief Push the full per-driver configuration over UART.
 *
 * Configures StealthChop (required for StallGuard4), per-motor StallGuard threshold and run/hold
 * currents, microstepping and the initial shaft polarity. Logs the connection test result for each
 * driver so wiring problems are visible in the serial monitor.
 *
 * @param motor Motor index.
 */
void configureDriver(int motor) {
  TMC2209Stepper* d = drivers[motor];

  d->begin();
  d->toff(5);                  // enable internal driver (toff > 0)
  d->blank_time(24);           // chopper blank time
  d->I_scale_analog(false);    // ignore VREF, use UART-defined current
  d->internal_Rsense(false);   // module has external sense resistors
  d->mstep_reg_select(true);   // microsteps via UART (ignore MS1/MS2 strapping for µstep selection)
  d->microsteps(microStepping);

  // Currents
  const float holdMultiplier = constrain(tmcHoldCurrentPct, 0, 100) / 100.0f;
  d->rms_current(tmcRunCurrentMA[motor], holdMultiplier);
  d->iholddelay(8);
  d->TPOWERDOWN(20);

  // StealthChop (required for StallGuard4 on the TMC2209)
  d->pwm_autoscale(true);
  d->pwm_autograd(true);
  d->en_spreadCycle(false);

  // StallGuard4: keep StallGuard active across the velocity range we ever step at, then publish the threshold.
  d->TCOOLTHRS(0xFFFFF);
  d->SGTHRS(stallGuardThreshold[motor]);

  // Initial shaft polarity. Direction is overridden per move via setMotorDirection().
  d->shaft(closeShaftDirection[motor]);

  uint8_t connStatus = d->test_connection();
  Serial.print("TMC2209 #");
  Serial.print(motor);
  Serial.print(" (addr 0x");
  Serial.print(tmcAddresses[motor], HEX);
  Serial.print(") test_connection=");
  Serial.print(connStatus);
  Serial.println(connStatus == 0 ? " OK" : " FAIL");
}

// ---------------------------------------------------------------------------------------------------------------------
// Setup / loop
// ---------------------------------------------------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);

  // STEP / EN pin setup (motors released until first move)
  for (int i = 0; i < NUM_MOTORS; i++) {
    pinMode(stepPins[i], OUTPUT);
    digitalWrite(stepPins[i], LOW);
    pinMode(enablePins[i], OUTPUT);
    digitalWrite(enablePins[i], EN_RELEASE);
  }

  // Bring up the shared TMC2209 UART bus and configure every driver.
  tmcSerial.begin(tmcUartBaud, SERIAL_8N1, tmcUartRxPin, tmcUartTxPin);
  delay(50);

  for (int i = 0; i < NUM_MOTORS; i++) {
    drivers[i] = new TMC2209Stepper(&tmcSerial, tmcSenseResistor, tmcAddresses[i]);
    configureDriver(i);

    // Mechanical position is unknown after boot; assume 100% open for both tracked and target
    // position until the first HTTP command (motorPositionKnown) runs findZero while !motorHomed.
    stepperPos[i]          = fullyOpenMicro;
    targetPos[i]           = fullyOpenMicro;
    closeRequested[i]      = false;
    motorBusy[i]           = false;
    motorHomed[i]          = false;
    motorPositionKnown[i] = false;
  }

  // Wi-Fi
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

void loop() {
  checkWiFiConnection();

  WiFiClient client = server.available();

  if (!client) {
    return;
  }

  String currentLine = "";
  int respType = 0; // 0=html, 1=json
  unsigned long requestStartTime = millis();
  const unsigned long REQUEST_TIMEOUT = 5000;
  bool requestProcessed = false;
  const int MAX_LINE_LENGTH = 2048;

  while (client.connected() && !requestProcessed && (millis() - requestStartTime) < REQUEST_TIMEOUT) {
    if (!client.available()) {
      continue;
    }
    char c = client.read();
    if (c == '\r' && currentLine.startsWith("GET /")) {
      if (!currentLine.startsWith("GET /")) {
        Serial.println("Invalid request format");
        client.stop();
        return;
      }

      int typePos = currentLine.indexOf("&t=");
      if (typePos > 0) {
        respType = currentLine.substring(typePos + 3, typePos + 4).toInt();
      }

      int actionPos = currentLine.indexOf("?a=");
      if (actionPos > 0) {
        int action = currentLine.substring(actionPos + 3, actionPos + 4).toInt();

        if (action < 0 || action > 6) {
          Serial.println("Invalid action parameter");
          client.stop();
          return;
        }

        int dist = 0;
        int distPos = currentLine.indexOf("&d=");
        if (distPos > 0) {
          dist = currentLine.substring(distPos + 3, distPos + 6).toInt();
        }
        int distAmount = dist * 100;

        int motorNum = 0;
        int motorPos = currentLine.indexOf("&m=");
        if (motorPos > 0) {
          motorNum = currentLine.substring(motorPos + 3, motorPos + 4).toInt();
        }

        if (motorNum < 0 || motorNum >= NUM_MOTORS) {
          Serial.println("Invalid motor number");
          client.stop();
          return;
        }

        Serial.print("Action: ");
        Serial.println(action);
        Serial.print("Dist: ");
        Serial.println(dist);
        switch (action) {
          case 0: // TEST - spin "open" direction by distAmount
            targetPos[motorNum] = stepperPos[motorNum] + distAmount;
            break;
          case 1: // TEST - spin "close" direction by distAmount
            targetPos[motorNum] = stepperPos[motorNum] - distAmount;
            break;
          case 2: // close completely (zero via StallGuard)
            closeRequested[motorNum] = true;
            targetPos[motorNum] = 0;
            break;
          case 3: // open completely
            targetPos[motorNum] = fullyOpenMicro;
            break;
          case 4: // open 50%
            targetPos[motorNum] = fullyOpenMicro / 2;
            break;
          case 5: // open 25%
            targetPos[motorNum] = fullyOpenMicro / 4;
            break;
          case 6: // open to ratio set by &d= (000=close, 050=50%, 100=fully open)
            targetPos[motorNum] = (fullyOpenMicro / 100) * dist;
            if (targetPos[motorNum] == 0) {
              closeRequested[motorNum] = true;
            }
            break;
        }
        // So motorTask can re-home when the first command matches assumed position (e.g. 100% open).
        motorPositionKnown[motorNum] = true;
      }
    } else if (c == '\n') {
      if (currentLine.length() == 0) {
        int pos[NUM_MOTORS];
        if (respType == 0) {
          Serial.println("Sending HTML response.");
          client.println("<!DOCTYPE html>");
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
          client.print("html, body{height:100%;overflow:hidden;margin:0;padding:5px;text-align:center;background:#333333;font-family:sans-serif;}");
          client.print("h1,h4,a{text-transform:uppercase;color:#DADADA;}");
          client.print("a{text-decoration:none;}");
          client.print(".button_row{display:flex;align-items:center;}");
          client.print(".button{flex-grow:1;flex-basis:1;padding:12px 2px;background:#202021;border:1px solid #5C5C5C;color:#DADADA;margin:0 4px; text-transform:uppercase;cursor:pointer;}");
          client.print(".status_row{width:100%;height:30px;margin:8px 4px;position:relative;background:#202021;border:1px solid #5C5C5C;}");
          client.print(".statusbar{width:0%;height:100%;position:absolute;top:0;left:0;background:#18BAC8;transition:width 1s ease;}");
          client.print("</style>");
          client.print("<script type='text/javascript'>");
          client.print("history.replaceState({}, 'AC Vent Controls', '/');");
          client.print("</script>");
          client.print("</head>");
          client.print("<body><h1><a href=\"/\">AC Vent Controls</a></h1>");
          for (int i = 0; i < NUM_MOTORS; i++) {
            pos[i] = 0;
            if (targetPos[i] > 0) {
              Serial.println("Calculating position...");
              Serial.print("TargetPos: ");
              Serial.println(targetPos[i]);
              Serial.print("EndPos: ");
              Serial.println(fullyOpenMicro);
              Serial.print("Ratio: ");
              float ratio = targetPos[i] / float(fullyOpenMicro);
              Serial.println(ratio);
              pos[i] = ratio * 100;
            }
            client.print("<h4>");
            client.print(ventNames[i]);
            client.print("</h4>");
            client.print("<div class='button_row'>");
            client.print("<a class='button' href='/?a=2&m=");
            client.print(i);
            client.print("'>Closed</a>");
            client.print("<a class='button' href='/?a=5&m=");
            client.print(i);
            client.print("'>25%</a>");
            client.print("<a class='button' href='/?a=4&m=");
            client.print(i);
            client.print("'>50%</a>");
            client.print("<a class='button' href='/?a=3&m=");
            client.print(i);
            client.print("'>Open</a>");
            client.print("</div>");
            client.print("<div class='status_row'>");
            client.print("<div class='statusbar' style='width:");
            client.print(pos[i]);
            client.print("%'></div>");
            client.print("</div>");

            Serial.print("Position: ");
            Serial.println(pos[i]);
          }
          client.print("</body></html>");
        } else {
          Serial.println("Sending JSON response.");
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: application/json;charset=utf-8");
          client.println("Server: Arduino");
          client.println("Connection: close");
          client.println();
          client.print("{");
          for (int i = 0; i < NUM_MOTORS; i++) {
            pos[i] = 0;
            if (targetPos[i] > 0) {
              Serial.println("Calculating position...");
              Serial.print("TargetPos: ");
              Serial.println(targetPos[i]);
              Serial.print("EndPos: ");
              Serial.println(fullyOpenMicro);
              Serial.print("Ratio: ");
              float ratio = targetPos[i] / float(fullyOpenMicro);
              Serial.println(ratio);
              pos[i] = ratio * 100;
            }
            client.print("\"");
            client.print(i);
            client.print("\":{\"name\":\"");
            client.print(ventNames[i]);
            client.print("\",\"pos\":");
            client.print(pos[i]);
            client.print(",\"busy\":");
            client.print(motorBusy[i] ? "true" : "false");
            client.print("}");
            if (i + 1 < NUM_MOTORS) {
              client.print(",");
            }

            Serial.print("Position: ");
            Serial.println(pos[i]);
          }
          client.print("}");
        }

        client.println();

        requestProcessed = true;
        break;
      } else {
        currentLine = "";
      }
    } else if (c != '\r') {
      if (currentLine.length() < MAX_LINE_LENGTH) {
        currentLine += c;
      } else {
        Serial.println("Request line too long - possible attack or malformed request");
        client.stop();
        return;
      }
    }
  }

  if (!requestProcessed && (millis() - requestStartTime) >= REQUEST_TIMEOUT) {
    Serial.println("Request timeout - closing connection");
    client.stop();
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// Motor task
// ---------------------------------------------------------------------------------------------------------------------

/**
 * @brief FreeRTOS task that drives motors toward their target positions.
 *
 * Runs on Core 0 in a continuous loop. For each motor it checks whether a sensorless close
 * was requested (closeRequested) or whether targetPos differs from stepperPos, and performs
 * the appropriate move. If the motor is not yet homed (!motorHomed) and motorPositionKnown is set
 * (HTTP just touched that motor), findZero runs even when target already equals assumed position (e.g.
 * first command "100% open"), then moveMotorTo syncs to target. closeRequested always runs
 * findZero and sets motorHomed. motorPositionKnown avoids homing at idle boot. If a StallGuard
 * event interrupts a positional move and no new target has been set, targetPos is synced to
 * the actual position to avoid retry loops.
 *
 * @param parameter Unused FreeRTOS task parameter.
 */
void motorTask(void* parameter) {
  esp_task_wdt_add(NULL);
  for (;;) {
    esp_task_wdt_reset();
    for (int i = 0; i < NUM_MOTORS; i++) {
      if (closeRequested[i]) {
        motorBusy[i] = true;
        closeRequested[i] = false;
        ensureOpen(i, 0);
        findZero(i);
        motorHomed[i]         = true;
        motorPositionKnown[i] = false;
        motorBusy[i] = false;
      } else if (targetPos[i] != stepperPos[i] || (!motorHomed[i] && motorPositionKnown[i])) {
        motorBusy[i] = true;
        if (!motorHomed[i]) {
          ensureOpen(i, targetPos[i]);
          findZero(i);
          motorHomed[i] = true;
        }
        int target = targetPos[i];
        ensureOpen(i, target);
        moveMotorTo(i, target);
        if (stepperPos[i] != target && targetPos[i] == target) {
          targetPos[i] = stepperPos[i];
        }
        motorPositionKnown[i] = false;
        motorBusy[i] = false;
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// ---------------------------------------------------------------------------------------------------------------------
// Motion primitives
// ---------------------------------------------------------------------------------------------------------------------

/**
 * @brief Move a motor to an absolute position (in microsteps).
 *
 * Homing before the first move is handled in motorTask via !motorHomed && motorPositionKnown.
 *
 * @param motor Motor index.
 * @param pos   Target position in microsteps (0 .. fullyOpenMicro).
 */
void moveMotorTo(int motor, int pos) {
  int motorPos = stepperPos[motor];

  Serial.print("Start Position: ");
  Serial.println(stepperPos[motor]);

  int dist = pos - motorPos;
  bool closing = false;
  if (dist < 0) {
    closing = true;
    dist = -dist;
  }

  Serial.print("Distance: ");
  Serial.println(dist);
  Serial.print("Closing: ");
  Serial.println(closing ? "yes" : "no");

  spinMotor(motor, closing, dist);

  Serial.print("End Position: ");
  Serial.println(stepperPos[motor]);
}

/**
 * @brief Feed this task's watchdog and briefly yield so IDLE tasks can run.
 *
 * @param stepCounter Current loop step index.
 */
void serviceWatchdogAndYield(int stepCounter) {
  if (stepCounter % 100 == 0) {
    esp_task_wdt_reset();
    vTaskDelay(1 / portTICK_PERIOD_MS);
  }
}

/**
 * @brief Spin a motor a relative amount (in microsteps) with linear acceleration / deceleration.
 *
 * The ramp zone length is min(dist/2, fullyOpenMicro/20). The step half-period linearly interpolates
 * from ACCEL_START_MULTIPLIER * stpDelay down to stpDelay over the ramp, mirrors at the end for
 * deceleration, and the motor is released after the move completes.
 *
 * @param motor   Motor index.
 * @param closing True to drive in the closing direction, false to drive open.
 * @param dist    Number of microsteps to move.
 */
void spinMotor(int motor, bool closing, int dist) {
  if (dist <= 0) {
    disableMotor(motor);
    return;
  }

  setMotorDirection(motor, closing);
  enableMotor(motor);

  const int addAmount = closing ? -1 : 1;

  int rampSteps = fullyOpenMicro / 20;
  if (rampSteps > dist / 2) {
    rampSteps = dist / 2;
  }
  const int maxDelay = stpDelay * ACCEL_START_MULTIPLIER;

  Serial.print("step delay: ");
  Serial.println(stpDelay);
  Serial.print("ramp steps: ");
  Serial.println(rampSteps);

  for (int i = 0; i < dist; i++) {
    int currentDelay = stpDelay;
    if (rampSteps > 0) {
      int rampPos = min(i, dist - 1 - i);
      if (rampPos < rampSteps) {
        currentDelay = maxDelay - (long)(maxDelay - stpDelay) * rampPos / rampSteps;
      }
    }

    pulseStep(motor, currentDelay);
    stepperPos[motor] += addAmount;

    serviceWatchdogAndYield(i);
  }

  disableMotor(motor);
  Serial.print("Current Position: ");
  Serial.println(stepperPos[motor]);
}

// ---------------------------------------------------------------------------------------------------------------------
// Sensorless homing
// ---------------------------------------------------------------------------------------------------------------------

/**
 * @brief Reset a motor's tracked position to zero (called once StallGuard confirms the closed limit).
 * @param motor Motor index.
 */
void zeroMotor(int motor) {
  stepperPos[motor] = 0;
}

/**
 * @brief Drive the vent toward the closed limit until StallGuard4 fires (or the step limit is hit).
 *
 * The motor accelerates from a slow start to the configured stpDelay over fullyOpenMicro/20 steps,
 * then decelerates back to a crawl as it approaches the expected zero (estimated from the current
 * tracked position). Slowing down protects the mechanism and improves StallGuard repeatability.
 *
 * After an effective warmup of `min(stallGuardWarmupSteps, max(0, estStepsToZero))` micro-steps
 * (so already-near-zero homing does not ignore StallGuard for the full config warmup),
 * the SG_RESULT register is polled every `stallGuardCheckInterval` steps. A stall is registered when
 * SG_RESULT < 2 * stallGuardThreshold[motor], matching the same behaviour the DIAG output would
 * produce. If the step limit
 * (fullyOpenMicro + 50) is reached without a stall, zero is assumed (stall threshold likely
 * needs tuning) and a warning is logged.
 *
 * @param motor Motor index.
 */
void findZero(int motor) {
  Serial.print("FindZero called for motor: ");
  Serial.println(motor);

  setMotorDirection(motor, DIR_CLOSE);
  enableMotor(motor);

  Serial.print("step delay: ");
  Serial.println(stpDelay);

  const int estStepsToZero = stepperPos[motor];
  const int effectiveStallWarmupSteps =
      min(stallGuardWarmupSteps, max(0, estStepsToZero));
  int       rampAccelSteps = min(fullyOpenMicro / 20, max(1, abs(estStepsToZero)));
  const int maxDelay       = stpDelay * ACCEL_START_MULTIPLIER;
  const int crawlDelay     = stpDelay * FINDZERO_CRAWL_DELAY_MULT;
  const int stepLimit      = fullyOpenMicro + 50;
  const uint16_t stallTrigger = (uint16_t)stallGuardThreshold[motor] * 2;

  Serial.print("ramp accel steps: ");
  Serial.println(rampAccelSteps);

  int  posMoved = 0;
  bool stalled  = false;

  while (posMoved < stepLimit && !stalled) {
    int currentDelay = stpDelay;

    if( estStepsToZero <= 10 ) {
      // Crawl at the slow speed for really short moves.
      currentDelay = crawlDelay;
    } else {
      // Acceleration ramp at the start of the move.
      if (posMoved < rampAccelSteps) {
        currentDelay = maxDelay - (long)(maxDelay - stpDelay) * posMoved / rampAccelSteps;
      }

      // Deceleration ramp as we approach the expected zero - if we overshoot, keep crawling at
      // the slow approach speed until StallGuard fires or the step limit is hit.
      const int remaining = estStepsToZero - posMoved;
      if (remaining < rampAccelSteps) {
        int decelDelay;
        if (remaining <= 0) {
          decelDelay = crawlDelay;
        } else {
          decelDelay = stpDelay + (long)(crawlDelay - stpDelay) * (rampAccelSteps - remaining) / rampAccelSteps;
        }
        if (decelDelay > currentDelay) {
          currentDelay = decelDelay;
        }
      }
    }

    pulseStep(motor, currentDelay);
    posMoved += 1;

    serviceWatchdogAndYield(posMoved);

    // Poll StallGuard once we're past the startup transient (or immediately for really short moves).
    if (posMoved > effectiveStallWarmupSteps &&
        (posMoved % stallGuardCheckInterval) == 0) {
      const uint16_t sg = drivers[motor]->SG_RESULT();
      if (sg < stallTrigger) {
        Serial.print("StallGuard triggered at step ");
        Serial.print(posMoved);
        Serial.print(" (SG_RESULT=");
        Serial.print(sg);
        Serial.print(", trigger<");
        Serial.print(stallTrigger);
        Serial.println(")");
        stalled = true;
      }
    }
  }

  if (!stalled) {
    Serial.println("WARNING: StallGuard never triggered within stepLimit - assuming zero position");
  }

  Serial.println("FindZero finished");
  Serial.print("PosMoved: ");
  Serial.print(posMoved);
  Serial.print(" / ");
  Serial.println(fullyOpenMicro);

  disableMotor(motor);
  zeroMotor(motor);
}

// ---------------------------------------------------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------------------------------------------------

/**
 * @brief Hook to ensure another vent stays open before closing the requested one.
 *
 * Currently disabled - this installation has a permanent open vent, so no action is required.
 * Retained for compatibility with older callers.
 *
 * @param closeMotor Motor index that is about to close (unused).
 * @param openStatus Requested target position for closeMotor (unused).
 */
void ensureOpen(int closeMotor, int openStatus) {
  (void)closeMotor;
  (void)openStatus;
  // DISABLED - a permanent open vent makes interlocking unnecessary.
}
