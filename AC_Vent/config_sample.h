/*
 * Sample configuration file for AC Vent Controller (TMC2209 stepper hat)
 * Copy this file to config.h and update with your actual values
 *
 * WARNING: config.h contains sensitive information (Wi-Fi credentials, IPs)
 *          and should NOT be committed to version control.
 */

#ifndef AC_VENT_CONFIG_H
#define AC_VENT_CONFIG_H

#include <Arduino.h>
#include <IPAddress.h>

// ---------------------------------------------------------------------------------------------------------------------
// -- START USER CONFIGURATION --
// ---------------------------------------------------------------------------------------------------------------------

#define NUM_MOTORS 3

// ---- Per-motor physical pins (control STEP / EN via GPIO; DIR is controlled via UART) ---------------------------------
const int stepPins[]   = { 13, 18, 32 }; // STEP pin (one rising edge per micro-step)
const int enablePins[] = { 14, 19, 33 }; // EN pin (active LOW: LOW = motor energised, HIGH = released)

// ---- TMC2209 UART addresses ----------------------------------------------------------------------------------------
// All drivers share a single UART bus. The address of each driver is set in hardware via MS1/MS2 strapping:
//   MS1=GND, MS2=GND  -> 0b00 (Stepper-1)
//   MS1=3V3, MS2=GND  -> 0b01 (Stepper-2)
//   MS1=GND, MS2=3V3  -> 0b10 (Stepper-3)
//   MS1=3V3, MS2=3V3  -> 0b11
const uint8_t tmcAddresses[] = { 0b00, 0b01, 0b10 };

// ---- Per-motor direction inversion --------------------------------------------------------------------------------
// Value of the TMC2209 GCONF.shaft bit when CLOSING the vent. Flip a motor's value to invert its rotation.
const bool closeShaftDirection[] = { true, true, true };

// ---- Display names ------------------------------------------------------------------------------------------------
const char* ventNames[] = {
  "Room 1",
  "Room 2",
  "Room 3"
};

// ---- Shared TMC2209 UART bus --------------------------------------------------------------------------------------
// All three TMC2209 drivers share a half-duplex single-wire UART (RX <-> TX joined through 1 kΩ).
// On most ESP32 dev boards GPIO1/GPIO3 (TX0/RX0) are also wired to the USB-Serial bridge; the firmware therefore uses
// HardwareSerial #1 (UART1) on the GPIO matrix so that USB debug printing on UART0 stays usable when the TMC2209
// header is unplugged. Adjust to suit your board if you wire the bus to a different UART.
#define TMC_UART_NUM    1     // 0 = Serial (USB), 1 = Serial1, 2 = Serial2
const int  tmcUartRxPin = 3;  // ESP32 GPIO connected to the TMC2209 PDN_UART line (RX0 on the ESP32 dev board)
const int  tmcUartTxPin = 1;  // ESP32 GPIO connected to the TMC2209 PDN_UART line through the 1 kΩ resistor (TX0)
const long tmcUartBaud  = 115200;

// ---- TMC2209 driver tuning ----------------------------------------------------------------------------------------
const float    tmcSenseResistor    = 0.11f; // Onboard sense resistor (BTT/Watterott TMC2209 V1.x = 0.11 Ω)
const uint16_t tmcRunCurrentMA     = 800;   // Motor RMS current while moving (mA) - tune to your motor / supply
const uint8_t  tmcHoldCurrentPct   = 50;    // Idle current as percentage of run current (0..100)

// Microstepping (1, 2, 4, 8, 16, 32, 64, 128, 256). Sent over UART at startup.
const int microStepping = 8;

// Steps to fully open the vent (200 full-steps per rev base; pre-microstepping)
const int fullyOpen = 1000;

// Step delay (µs) - delay between half-pulses (smaller = faster). Compensated for microstepping in firmware.
const int stepDelay = 2000;

// ---- StallGuard4 (sensorless homing) ------------------------------------------------------------------------------
// Threshold for SGTHRS register (0..255). A stall is detected when SG_RESULT < 2 * stallGuardThreshold.
// HIGHER value = MORE sensitive (triggers earlier with less force). Tune to your motor / load.
const uint8_t stallGuardThreshold     = 100;
// Skip stall checks for the first N micro-steps after starting findZero, to avoid acceleration false-positives.
const int     stallGuardWarmupSteps   = 200;
// Read SG_RESULT every N micro-steps. Lower = faster reaction, but more UART traffic and slower stepping.
const int     stallGuardCheckInterval = 8;

// ---- Wi-Fi / network ---------------------------------------------------------------------------------------------
const char* ssid     = "YOUR_WIFI_SSID";       // Your Wi-Fi network name
const char* password = "YOUR_WIFI_PASSWORD";   // Your Wi-Fi password

IPAddress local_IP   (192, 168, 1, 100); // Static IP for your ESP32
IPAddress gateway    (192, 168, 1, 1);   // Your router's IP
IPAddress subnet     (255, 255, 255, 0); // Subnet mask
IPAddress primaryDNS (8, 8, 8, 8);       // Google DNS (optional)
IPAddress secondaryDNS(8, 8, 4, 4);      // Google DNS (optional)

// ---------------------------------------------------------------------------------------------------------------------
// -- END USER CONFIGURATION --
// ---------------------------------------------------------------------------------------------------------------------

#endif // AC_VENT_CONFIG_H
