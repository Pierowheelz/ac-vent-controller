# AC Vent Controller Configuration

This project uses a separate configuration file to keep sensitive information out of version control.

## Library Dependencies

Install via the Arduino IDE Library Manager:

- **TMCStepper** by *teemuatlut* (≥ 0.7.3) — UART control of the TMC2209 drivers and StallGuard4 access.

## Setup Instructions

1. **Copy the sample configuration:**

   ```bash
   cp config_sample.h config.h
   ```

2. **Edit `config.h` with your actual values:**
   - Wi-Fi SSID and password
   - Room names
   - IP addresses
   - Pin assignments (if different from default)
   - TMC2209 driver tuning (current, microstepping, StallGuard threshold)

3. **Upload to your ESP32 as usual.**
   - If you wired the TMC2209 UART bus to GPIO1/GPIO3 (TX0/RX0), unplug or jumper that bus during firmware upload, otherwise the bootloader and the drivers will fight over the line.

## Important Security Notes

- **`config.h` is ignored by git** and contains sensitive information
- **Never commit `config.h`** to version control
- **`config_sample.h`** is a template that can be committed safely
- Share `config_sample.h` with others but keep your `config.h` private

## Configuration Variables

### Hardware pin map (per motor)

- `NUM_MOTORS` — number of vent motors (default: `3`)
- `stepPins[]` — STEP pin (one rising edge per microstep)
- `enablePins[]` — EN pin (active LOW)
- `tmcAddresses[]` — UART address of each TMC2209 (set in hardware via MS1/MS2 strapping; `0b00`, `0b01`, `0b10`, `0b11`)
- `closeShaftDirection[]` — value of the TMC2209 `GCONF.shaft` bit when CLOSING the vent. Flip a motor's value to invert its rotation
- `ventNames[]` — display names for each vent / room

### TMC2209 UART (shared bus)

All three drivers share a single half-duplex UART (TX and RX joined through a 1 kΩ resistor).

- `TMC_UART_NUM` — ESP32 hardware UART to use (`0` = Serial / USB, `1` = Serial1, `2` = Serial2). Default `1`.
- `tmcUartRxPin` — GPIO connected to the TMC2209 PDN_UART line (default `3` = RX0)
- `tmcUartTxPin` — GPIO connected to the TMC2209 PDN_UART line through the 1 kΩ resistor (default `1` = TX0)
- `tmcUartBaud` — UART baud (default `115200`)

### TMC2209 driver tuning

- `tmcSenseResistor` — sense resistor on the driver module (BTT/Watterott TMC2209 V1.x = `0.11` Ω)
- `tmcRunCurrentMA` — motor RMS current while moving (mA)
- `tmcHoldCurrentPct` — idle current as a percentage of run current (`0..100`)
- `microStepping` — microsteps per full step (`1, 2, 4, 8, 16, 32, 64, 128, 256`)
- `fullyOpen` — full-open distance in pre-microstepping steps
- `stepDelay` — half-period between STEP edges in µs (compensated for microstepping in firmware)

### StallGuard4 (sensorless homing)

- `stallGuardThreshold` — `SGTHRS` register value (`0..255`). A stall is registered when `SG_RESULT < 2 * stallGuardThreshold`. **Higher = more sensitive.**
- `stallGuardWarmupSteps` — number of microsteps to ignore at the start of `findZero()` to avoid false-positives during the acceleration ramp
- `stallGuardCheckInterval` — read `SG_RESULT` every N microsteps (lower = quicker reaction at the cost of more UART traffic)

### Wi-Fi / network

- `ssid`, `password`, `local_IP`, `gateway`, `subnet`, `primaryDNS`, `secondaryDNS` — see comments in `config_sample.h`

## Tuning StallGuard4

`findZero()` drives the vent toward the closed mechanical limit until StallGuard4 reports a stall. If you see false stalls (motor stops mid-travel) or missed stalls (motor crashes into the limit and steps lost), tune in this order:

1. **Set `tmcRunCurrentMA`** so the motor has just enough torque for normal operation; less current makes StallGuard far more reliable.
2. **Adjust `stallGuardThreshold`**: raise it if stalls are missed, lower it if false stalls happen mid-travel.
3. **Tune `stepDelay`**: StallGuard works best at moderate speeds. Try slowing down (larger `stepDelay`) if detection is unreliable.
4. **Tune `stallGuardWarmupSteps`** if the motor stalls during the initial acceleration ramp.
