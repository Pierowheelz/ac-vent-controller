# Ducted Air Conditioner Vent Controller
This project uses an ESP32-based controller, along with stepper motors and endstop switches (from a 3d printer kit) to control the circular Air Conditioner vents used in many ducted systems.

## Parts Required
* ESP32 Dev Board (NodeMCU-ESP32 DEVKITV1 - 30 pins - based on ESP-WROOM-32)
* 3D Printer Kit (from ebay or Aliexpress)
    * 3x Nema 17 stepper motors + cables
    * 3x Nema 17 mounting brackets
    * 3x Stepper motors drivers - A4988 or DRV8825 or TMC2208 (for quieter operation)
    * 3x RAMPS 1.4 switch endstops + cables (Note, swap the trigger switch for a waterproof one for more reliable operation, but keep the switch PCB)
* Order the PCB from the Gerber files (or custom-build with Perfboard 24 x 18 pins)
* 3x 150mm Lead screws + copper nuts (4mm pitch ideal, but any will work)
* DC-DC Buck convertor TRS2433 (or you can use an LM2596 set to 3.3v if DIYing the board)
* DC power supply (8-35 V)
* (optional) header-pin sockets for ESP32 and Stepper Drivers to allow quick removal/replacement
* A handfull of M4 screws & nuts
* Duct tape

## Process
1. Order the custom PCM from the Gerber files in the Circuit Board V2/ directory.
1. Print 3x Motor Bracket, Motor Cover, Endstop Clip in PLA or PETG (or other hard-plastic).
1. Print 3x Coupler, Damper and 6x End Holder in TPU (for vibration damping - PLA will work in a pinch though). Metal couplers from a 3d printer kit can also be used.
1. Assemble the circuitboard per the KiCad schematic or using the silk screen guides.
1. Tune the stepper motor current limit (Try this guide: https://www.makerguides.com/a4988-stepper-motor-driver-arduino-tutorial/).
    * Using USB for power (do not plug in the power supply for this).
1. Set the ventNames, microStepping, WiFi parameters in the firmware (see AC_Vent/README_CONFIG.md for details).
1. Compile and flash the firmware onto your ESP32 dev board using Arduino IDE.
    * Using USB for power (do not plug in the power supply for this).
    * NOTE: Setup Arduino IDE with ESP32 library like this: https://randomnerdtutorials.com/installing-the-esp32-board-in-arduino-ide-windows-instructions/
1. Bench test by connecting motors and endstop switches.
1. Connect the power supply to the high-voltage side of the buck convertor. This will power up the ESP32.
1. On a computer, navigate to the IP Address of the device.
1. Click to open one of the Vents.
    * It will first spin counter-clockwise to close the vent until the endstop switch is triggered, then clockwise until the limit position is reached.
1. Assemble components and install into vents. Tip: use Ethernet cable to extend leads as neccessary (I used RJ45 breakout modules but you could crimp connectors).

Note: There is no security or authentication in this system. Do not open up the port in your Router! For use on your internal network only.

## Usage via Rest API
Get vents status: `GET http://192.168.2.110/?&t=1`

Sample return:
```
{
	"0": {
		"name": "Room #1",
		"pos": 30
	},
	"1": {
		"name": "Room #2",
		"pos": 60
	},
	"2": {
		"name": "Room #3",
		"pos": 100
	}
}
```

Open vent `#1` to 100%: `GET http://192.168.2.110/?a=6&t=1&m=0&d=100`
Here `a` is the operating mode, where mode 6 corresponds to: open to ratio set by &d= (eg. ?d=050:open50%, ?d=000:close, ?d=100:open100%).
Also, `t` is the response type (1 = JSON).
And, `m` is the motor number (0-2).

The return format is identical to the 'get status' request.


## Images
Note: Images are from the prototype.

![AllParts](/Sample/AllParts.jpg)

![Assembled](/Sample/Assembled.jpg)

![BoartTop](/Sample/BoartTop.jpg)

![BoardRear](/Sample/BoardRear.jpg)

![BoardAssembled](/Sample/BoardAssembled.jpg)

![BoardAssembled2](/Sample/BoardAssembled2.jpg)
