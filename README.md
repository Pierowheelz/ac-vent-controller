# Ducted Air Conditioner Vent Controller
This project uses and ESP32-based controller, along with stepper motors and endstop switches (from a 3d printer kit) to control the circular Air Conditioner vents used in many ducted systems.

## Parts Required
* ESP32 Dev Board (NodeMCU-ESP32 DEVKITV1 - 30 pins - based on ESP-WROOM-32)
* 3D Printer Kit (from ebay or Aliexpress)
    * 3x Nema 17 stepper motors + cables
    * 3x Nema 17 mounting brackets
    * 3x Stepper motors drivers - A4988 or DRV8825 or TMC2208 (for quieter operation)
    * 3x RAMPS 1.4 switch endstops + cables
* Perfboard 24 x 18 pins
* Capacitor (100µF or thereabouts)
* 3x 150mm Lead screws + copper nuts (4mm pitch ideal, but any will work)
* DC-DC Buck convertor (LM2596S or similar)
* DC power supply (8-35 V)
* (optional) header-pin sockets for ESP32 and Stepper Drivers to allow quick removal/replacement
* A handfull of M4 screws & nuts
* Duct tape

## Process
1. Print 3x Motor Bracket, Motor Cover, Endstop Clip in PLA or PETG (or other hard-plastic).
1. Print 3x Coupler, Damper and 6x End Holder in TPU (for vibration damping - PLA will work in a pinch though).
1. Tune your Buck convertor to output 3.3v with your DC power supply.
1. Assemble the circuitboard per the schematic.
    * The Engage pins of Stepper Drivers should face toward the top of the board (the top is the side with the capacitor).
    * The 3v3 and VIN pins (and USB port) of the ESP32 board should face downwards (the ESP32 is on the back of the board).
    * (optional) Join the M0, M1 (and M2 if present on your drivers) to 3.3v to set your MicroStepping mode (more microsteps = less vibration but higher-pitch operation). Lookup your driver to find out the MicroStepping setting (you will need this below).
1. Tune the stepper motor current limit (Try this guide: https://www.makerguides.com/a4988-stepper-motor-driver-arduino-tutorial/).
    * Using USB for power (do not plug in the power supply for this).
1. Set the ventNames, microStepping, WiFi parameters in the firmware. Check pin numbers, speed settings (stepDelay) and limit (fullyOpen)
1. Compile and flash the firmware onto your ESP32 dev board using Arduino IDE.
    * Using USB for power (do not plug in the power supply for this).
    * Setup Arduino IDE with ESP32 library like this: https://randomnerdtutorials.com/installing-the-esp32-board-in-arduino-ide-windows-instructions/
1. Bench test by connecting motors and endstop switches.
1. Connect the power supply to the high-voltage side of the buck convertor. This will power up the ESP32.
1. On a computer, navigate to the IP Address of the device.
1. Click to open one of the Vents.
    * It will first spin counter-clockwise to close the vent until the endstop switch is triggered, then clockwise until the limit position is reached.
1. Assemble components and install into vents. Use CAT6 Ethernet cable to extend leads as neccessary (I used RJ45 breakout modules but you could crimp connectors).

Note: There is no security or authentication in this system. Do not open up the port in your Router! For use on your internal network only.

## Images
Some images are from an older version.

![AllParts](/Sample/AllParts.jpg)

![Assembled](/Sample/Assembled.jpg)

![BoartTop](/Sample/BoartTop.jpg)

![BoardRear](/Sample/BoardRear.jpg)

![BoardAssembled](/Sample/BoardAssembled.jpg)

![BoardAssembled2](/Sample/BoardAssembled2.jpg)
