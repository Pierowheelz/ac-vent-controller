/*
 * Sample configuration file for AC Vent Controller
 * Copy this file to config.h and update with your actual values
 *
 * WARNING: config.h contains sensitive information and should NOT be committed to version control
 */

// ---------------------------------------------------------------------------------------------------------------------
// -- START USER CONFIGURATION --
// ---------------------------------------------------------------------------------------------------------------------

#define NUM_MOTORS 3

const int dirPins[] = { //Pins controlling direction of stepper
  15, //Room 1
  5,  //Room 2
  25, //Room 3
  12  //Room 4
};
const int stepper[] = { //Pins controlling stepper steps
  2,
  18,
  33,
  13
};
const int stepperPower[] = { //Pins controlling stepper power
  4,
  19,
  32,
  26
};
const int endStop[] = { // endstop switch pins (active LOW)
  14,
  27,
  34,
  35
};
const char* ventNames[] = { //Display names for each vent/room
  "Room 1",
  "Room 2",
  "Room 3",
  "Room 4"
};

// Current microstepping setting (no need to vary stepDelay); fullyOpen[] is per motor
const int microStepping = 16; //1=no microStepping, 32=max (on DRV8825)

// Full-open travel in full steps (200 per rotation), one entry per motor — multiplied by microStepping at runtime
const int fullyOpen[] = {
  1000,
  1000,
  1000
};
static_assert(sizeof(fullyOpen) / sizeof(fullyOpen[0]) == NUM_MOTORS,
              "fullyOpen[] must have NUM_MOTORS entries");

// Endstop phantom rejection: require this many separate approaches (press events) before
// running the blocking checkEndstopStatus() debounce (findZero homing and spinMotor close).
const int ENDSTOP_PHANTOM_CONFIRM_TRIGGERS = 2;

//Speed of the motor (in Microseconds)
const int stepDelay = 2000; //delay between steps (1000 = fastest, 5000 = pretty slow)

// Wifi / Network settings
const char* ssid    = "YOUR_WIFI_SSID";         // Your WiFi network name
const char* password = "YOUR_WIFI_PASSWORD";    // Your WiFi password

IPAddress local_IP(192, 168, 1, 100);      // Static IP for your ESP32
IPAddress gateway(192, 168, 1, 1);         // Your router's IP
IPAddress subnet(255, 255, 255, 0);       // Subnet mask
IPAddress primaryDNS(8, 8, 8, 8);         // Google DNS (optional)
IPAddress secondaryDNS(8, 8, 4, 4);       // Google DNS (optional)
// ---------------------------------------------------------------------------------------------------------------------
// -- END USER CONFIGURATION --
// ---------------------------------------------------------------------------------------------------------------------
