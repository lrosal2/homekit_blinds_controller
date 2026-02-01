# HomeKit Blinds Controller

Matter-over-Thread smart blinds controller using ESP32-C6.

## Hardware

| Component | Purpose |
|-----------|---------|
| ESP32-C6 | MCU with native Thread (802.15.4) support |
| 28BYJ-48 | Stepper motor |
| ULN2003 | Stepper driver board |
| XL6009 | Boost converter (for battery stage) |
| TP4056 | Li-ion charger (for battery stage) |
| 2x 18650 | Battery pack (for battery stage) |

## Wiring (Stage 1 - USB powered)

```
ESP32-C6       ULN2003
--------       -------
D0 (GPIO0) --->  IN1
D1 (GPIO1) --->  IN2
D2 (GPIO2) --->  IN3
D3 (GPIO3) --->  IN4
GND      --->  GND

ULN2003 power: connect 5V and GND to an external 5V supply or USB breakout.
The ESP32-C6 is powered separately via its USB port.
```

## Prerequisites

- [ESP-IDF v5.3+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/get-started/)
- ESP32-C6 connected via USB

### Install ESP-IDF (macOS)

```bash
brew install cmake ninja dfu-util python3

mkdir -p ~/esp && cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git -b v5.3
cd esp-idf && ./install.sh esp32c6
```

## Build & Flash

```bash
# Source ESP-IDF environment (run once per terminal session)
source ~/esp/esp-idf/export.sh

# Build
idf.py set-target esp32c6
idf.py build

# Flash (connect ESP32-C6 via USB first)
idf.py flash

# Monitor serial output
idf.py monitor

# Or build + flash + monitor in one command
idf.py flash monitor
```

Press `Ctrl+]` to exit the monitor.

## Project Structure

```
├── CMakeLists.txt              # Top-level build file
├── sdkconfig.defaults          # Default build configuration
├── partitions.csv              # Flash partition layout
├── main/
│   ├── CMakeLists.txt
│   └── app_main.cpp            # Application entry point
└── components/
    └── stepper/
        ├── CMakeLists.txt
        ├── include/
        │   └── stepper_driver.h
        └── stepper_driver.c    # 28BYJ-48 stepper motor driver
```

## Development Stages

- [x] Stage 1: Toolchain & basic motor control (USB powered)
- [ ] Stage 2: Matter Window Covering device
- [ ] Stage 3: Position tracking & persistence
- [ ] Stage 4: Hardening (OTA, error recovery)
- [ ] Stage 5: Battery power integration
- [ ] Stage 6: Final integration & enclosure
