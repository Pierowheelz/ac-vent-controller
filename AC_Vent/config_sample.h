/*
 * Sample configuration file for AC Vent Controller
 * Copy this file to config.h and update with your actual values
 *
 * WARNING: config.h contains sensitive information and should NOT be committed to version control
 */

// ---------------------------------------------------------------------------------------------------------------------
// -- START USER CONFIGURATION --
// ---------------------------------------------------------------------------------------------------------------------

// Set to 1 if each motor has a redundant second switch wired to endStopAlt[]; 0 = primary endStop[] only
#define USE_ALTERNATE_ENDSTOPS 0

#define NUM_MOTORS 3

const int dirPins[] = { //Pins controlling direction of stepper
  15, //Room 1
  5,  //Room 2
  25  //Room 3
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
const int endStopAlt[] = { // redundant endstop pins (parallel with endStop[])
  26,
  34,
  35
};
const char* ventNames[] = { //Display names for each vent/room
  "Room 1",
  "Room 2",
  "Room 3"
};

// Current microstepping setting (no need to vary stepDelay or fullyOpen)
const int microStepping = 8; //1=no microStepping, 32=max (on DRV8825)

// Amount in steps (200 per rotation) to open the vent (ignoring microStepping)
const int fullyOpen = 1000;

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