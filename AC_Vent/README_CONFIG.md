# AC Vent Controller Configuration

This project uses a separate configuration file to keep sensitive information out of version control.

## Setup Instructions

1. **Copy the sample configuration:**
   ```bash
   cp config_sample.h config.h
   ```

2. **Edit `config.h` with your actual values:**
   - WiFi SSID and password
   - Room names
   - IP addresses
   - Pin assignments (if different from default)
   - Motor settings

3. **Upload to your ESP32 as usual**

## Important Security Notes

- **`config.h` is ignored by git** and contains sensitive information
- **Never commit `config.h`** to version control
- **`config_sample.h`** is a template that can be committed safely
- Share `config_sample.h` with others but keep your `config.h` private

## Configuration Variables

- `NUM_MOTORS`: Number of vent motors (default: 4)
- `dirPins[]`: Direction control pins for each motor
- `stepper[]`: Step control pins for each motor
- `stepperPower[]`: Power control pins for each motor
- `endStop[]`: Endstop sensor pins for each motor
- `ventNames[]`: Display names for each vent/room
- `microStepping`: Microstepping setting (default: 8)
- `fullyOpen`: Steps for full vent opening (default: 1000)
- `stepDelay`: Delay between steps in microseconds (default: 2000)
- WiFi settings: SSID, password, and IP configuration
