# Ducted Air Conditioner Vent Controller
This project uses and ESP32-based controller, along with stepper motors and endstop switches (from a 3d printer kit) to control the circular Air Conditioner vents used in many ducted systems.
![All Parts](/Sample/All Parts.png)
![Assembled](/Sample/Assembled.png)

## Parts Required
* ESP32 Dev Board (WROOM 32)
* 3D Printer Kit (from ebay or Aliexpress)
    * 3x Nema 17 stepper motors + cables
    * 3x Nema 17 mounting brackets
    * 3x Stepper motors drivers - A4988 or DRV8825 or TMC2208 (for quieter operation)
    * 3x RAMPS 1.4 switch endstops + cables
* Perfboard 24 x 18 pins
* Capacitor (somewhere around 100µF)
* 3x 150mm Lead screws + copper nuts
* DC-DC Buck convertor (LM2596S or similar)
* Power supply (8-35 V)

## Process
1. Tune your Buck convertor to output 3.3v
1. Assemble the circuitboard per the schematic.
1. Tune the stepper motor current limit (Try this guide: https://www.makerguides.com/a4988-stepper-motor-driver-arduino-tutorial/)
1. Set the ventNames, microStepping, WiFi parameters in the firmware. Check pin numbers, speed settings (stepDelay) and limit (fullyOpen)
1. Compile and flash the firmware onto your ESP32 dev board using Arduino IDE (do not plug in the power supply for this).
1. Bench test by connecting motors and endstop switches.
1. Connect the power supply to the high-voltage side of the buck convertor. This will power up the ESP32.
1. On a computer, navigate to the IP Address of the device.
1. Click to open one of the Vents.
    * It will first spin counter-clockwise to close the vent until the endstop switch is triggered, then clockwise until the limit position is reached.
1. Assemble components and install into vents. Use CAT6 Ethernet cable to extend leads as neccessary (I used RJ45 breakout modules but you could crimp connectors).
