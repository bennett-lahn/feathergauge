# Feather Wave Gauge

[![License: CC0-1.0](https://img.shields.io/badge/License-CC0%201.0-lightgrey.svg)](http://creativecommons.org/publicdomain/zero/1.0/)
[![Arduino](https://img.shields.io/badge/Arduino-Compatible-00979D.svg)](https://www.arduino.cc/)

## Overview

The Feather Wave Gauge is an affordable, open-source instrument designed for coastal researchers, educators, and environmental monitoring. Built around the Adafruit Feather 32u4 Adalogger microcontroller and a high-precision pressure sensor, this system logs water pressure data that can be used to derive wave height, periods, and other hydrodynamic parameters.

The wave gauge is particularly well-suited for:
- Nearshore wave monitoring and coastal dynamics research
- Storm surge and flood monitoring
- Research deployments requiring multiple sensor arrays

Data is logged to an onboard microSD card in CSV format for easy post-processing with standard analysis tools.

**Maintained by:** [NHERI RAPID](https://rapid.designsafe-ci.org/) 


## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Repository Structure](#repository-structure)
- [System Requirements](#system-requirements)
- [Getting Started](#getting-started)
  - [1. Software Installation](#1-software-installation)
  - [2. Library Setup](#2-library-setup)
  - [3. Arduino IDE Configuration](#3-arduino-ide-configuration)
  - [4. Setting the Real-Time Clock](#4-setting-the-real-time-clock)
  - [5. Uploading Main Code](#5-uploading-main-code)
- [Configuration Options](#configuration-options)
- [Operation](#operation)
- [Data Format](#data-format)
- [Deployment Guidelines](#deployment-guidelines)
- [Battery Life](#battery-life)
- [LED Status Indicators](#led-status-indicators)
- [Troubleshooting](#troubleshooting)
- [Post-processing](#post-processing)
- [License](#license)
- [Acknowledgments](#acknowledgments)



## Features

- **Low Cost**: Economical alternative to commercial wave gauges
- **Flexible Sampling**: Supports continuous and burst sampling modes
- **Autonomous Operation**: Battery-powered with long deployment durations, up to several months
- **Real-Time Clock**: Accurate timestamping with RTC module
- **Configurable**: User-adjustable sampling rates and burst parameters

---

## Repository Structure

```
feathergauge/
├── Sketchbooks/
│   ├── feathergauge_code/     # Primary data logging firmware
│   ├── rtc_setup/             # Real-time clock setup utility
│   └── serial_number_generator/ # Tool to flash device identification number
├── analysis_scripts/          # Python scripts for data quality analysis
├── automatic_programming/     # Batch programming scripts and tools
├── build_info/                # Hardware assembly documentation & CAD files
└── libraries/                 # Required Arduino libraries (ZIP files)
```

### Key Components

- **`Sketchbooks/feathergauge_code/`**: The primary Arduino sketch for data logging. Configure sampling parameters here before uploading.
- **`Sketchbooks/rtc_setup/`**: Required utility to synchronize the real-time clock before use.
- **`libraries/`**: All necessary Arduino libraries packaged as ZIP files.
- **`automatic_programming/`**: Tools for programming multiple wave gauges efficiently (Windows and Linux).
- **`build_info/`**: Hardware assembly guide, bill of materials, and 3D models for internal components.


## System Requirements

### Software
- **Arduino IDE** (latest version recommended)
- **Windows 10/11** (for automatic driver installation) or Linux/Mac with appropriate drivers

### Target Hardware
- **Microcontroller**: Adafruit Feather 32u4 Adalogger
- **Pressure Sensor**: SparkFun MS5803, Blue Robotics Bar02 or equivalent MS5837-02BA
- **Storage**: MicroSD card (8GB recommended, large capacity cards may not work correctly)
- **RTC**: DS3231 Real-time clock, connected to microcontroller using SPI


## Getting Started

### 1. Software Installation

**Download the Arduino IDE** from [arduino.cc](https://www.arduino.cc/en/software)

**Configure Board Support:**

1. Open Arduino IDE and navigate to **File → Preferences**
2. Add the Adafruit boards URL to "Additional Boards Manager URLs":
   ```
   https://adafruit.github.io/arduino-board-index/package_adafruit_index.json
   ```
   [Detailed instructions](https://learn.adafruit.com/adafruit-feather-32u4-adalogger/setup)

3. Go to **Tools → Board → Boards Manager**
4. Search for "Adafruit AVR Boards" (leave Type set to "All")
5. Install the board package
6. **Close and reopen Arduino IDE** (required for changes to take effect)

> **Windows 10/11 Users**: Driver installation is automatic. No manual driver installation required.

### 2. Library Setup

1. Download this entire repository as a ZIP file (**Code → Download ZIP**)
2. Extract to a location on your local hard drive
3. In Arduino IDE, navigate to **Sketch → Include Library → Add .ZIP Library...**
4. Navigate to the `libraries/` folder in the extracted repository
5. Add each `.ZIP` file one at a time:
   - `Adafruit_BusIO-master.zip`
   - `Low-Power-1.81.zip`
   - `RTClib-2.1.4.zip`
   - `SD-master.zip`
   - `TimerOne-<version>.zip`

> **Troubleshooting**: If you see compilation errors like `xxx.h: No such file or directory`, close and reopen Arduino IDE after installing each library.

### 3. Arduino IDE Configuration

1. Connect the Feather board to your computer using a micro-USB cable
2. In Arduino IDE, click the board selection dropdown (upper left)
3. Click **"Select other board and port..."**
4. Under **Boards**, search for and select **"Adafruit Feather 32u4"**
5. Under **Ports**, select the COM port labeled with **(USB)**
   - If multiple USB ports appear, try each until successful upload

### 4. Setting the Real-Time Clock

The RTC must be set for proper timestamping. This is a one-time setup unless the RTC battery is removed.

**Setting the Clock:**

1. Open `Sketchbooks/rtc_setup/rtc_setup.ino` in Arduino IDE
2. Ensure the correct board and port are selected
3. Click **Upload**
   - If upload hangs on "Waiting for upload port," press the **small black reset button** on the board
4. Once uploaded, open **Serial Monitor** (magnifying glass icon, top right)
5. Set baud rate to **57600 baud** if message appears corrupted
6. Type the current date/time in the Serial Monitor message box using this format:
   ```
   MMM DD YYYY HH:MM:SS
   ```
   Example: `Dec 19 2025 14:30:00` (24-hour format)
8. Enter the command a few seconds ahead of actual time, then press Enter when time matches
9. The RTC is now synchronized. Ensure a CR1220 coin cell battery is installed for backup power.

### 5. Uploading Main Code

> **Critical**: Follow this sequence exactly for proper operation.

1. Ensure the device is powered off (no USB, no battery)
2. Insert microSD card into SD card slot
3. Connect battery (if applicable)
4. Connect USB cable to computer
5. Open `Sketchbooks/feathergauge_code/feathergauge_code.ino`
6. Configure sampling parameters (see [Configuration Options](#configuration-options))
7. Click **Upload**
   - If upload hangs, press the **reset button** when "Waiting for upload port" appears
8. Once upload completes, disconnect USB cable
9. The red LED will flash **1-6 times** then stop, indicating successful startup

> **⚠️ Error**: Continuously flashing LED indicates a fatal error. See [LED Status Indicators](#led-status-indicators).

---

## Configuration Options

Edit lines 1-50 of `feathergauge_code.ino` before uploading to configure sampling behavior:

### Continuous Sampling Mode
```cpp
SAMPLE_FREQ = 16;  // Sampling frequency in Hz
BURST_SAMPLING = false;
BURST_SAMPLING_ONE_SAMPLE = false;
DELAY_START = false;
```

### Burst Sampling Mode
```cpp
SAMPLE_FREQ = 16;  // Frequency during burst
BURST_SAMPLING = true;
BURST_SAMPLING_ONE_SAMPLE = false;  // Set true for single sample per burst
writeSeconds = 30;   // Sampling duration (seconds)
sleepSeconds = 120;  // Sleep duration between bursts (seconds)
DELAY_START = false;
```

### Single Sample Burst Mode
```cpp
SAMPLE_FREQ = 1;  // Must be 1 Hz for single sample
BURST_SAMPLING = true;
BURST_SAMPLING_ONE_SAMPLE = true;
writeSeconds = 5
sleepSeconds = 120;
DELAY_START = false;
```

> **⚠️ Note**: `DELAY_START` is currently unsupported. Always set to `false`.

**Battery Life Considerations:**
- Higher sampling frequencies reduce battery life
- Burst sampling with longer sleep intervals extends battery life

## Operation

### Starting the Wave Gauge

**After Initial Programming:**
The gauge starts automatically after uploading code (see step 5 above).

**Restarting After Battery Disconnect:**
Plug in the battery. If the gauge starts successfully, you will see 1-6 LED flashes, just as when the wave gauge was programmed.

## Data Format

### File Naming
- Format: `MM-DD-YY.CSV` (date when sampling began)
- Example: `12-19-25.CSV` for December 19, 2025

### CSV Structure
Each file contains:
- **Header row**: Wave gauge serial number and metadata
- **Data rows**: Timestamp, pressure (mbar), temperature (°C)

### Example CSV Output
```csv
W.G. Num: 01,Timestamp,Pressure [mbar],Temp [deg C],Battery [VDC]
2025/9/19,12:19:51:018,1006.20,26.94,4.47
2025/9/19,12:19:51:078,1006.30,26.95,4.47
```

## Deployment Guidelines

### Orientation
**Always deploy with sensor facing down**. This prevents immersion of electronics if a minor leak occurs, maximizing data recovery probability


### Depth Considerations for Wave Monitoring
Sensors should be positioned to detect dynamic pressure fluctuations of the shortest wave period. Wave-induced pressure attenuates exponentially with depth:

- **Decay Coefficient (K)**: Want K ≈ 1.0 for target wave periods
- **Avoid**: Placing sensors too high above seabed for short-period waves
- **Example**: At 3m depth on seabed, wave periods ≤2s will not be detected

![pressuredecayguide](https://github.com/user-attachments/assets/69788a87-37ee-42d7-9b7c-4da35f923b32)
Figure 1. Deployment Guidance for Pressure Attenuation with Depth


### Pre-Deployment Testing (Recommended)
1. Allow gauge to sample in atmospheric pressure for 1-2 minutes
2. Submerge all gauges in bucket of seawater (sensor at bottom)
3. Sample for 1-2 minutes with consistent orientation
4. Use these reference samples for minor calibration adjustments

### Recovery
1. Keep gauge sensor-down during recovery to prevent water intrusion
2. **⚠️ Warning**: If leak occurred, gauge may be under pressure. Do not point at self when opening.
3. Inspect for water with sensor-down orientation
4. If water present: Remove electronics, unplug battery, pour out water
5. Do not reuse water-exposed batteries
6. Allow to dry completely before reinserting electronics

---

## Battery Life

| Configuration | Battery Capacity | Estimated Life |
|--------------|------------------|----------------|
| Single sample burst (2 min interval) | 4400 mAh | ~2 months |
| Single sample burst (2 min interval) | 10500 mAh | >3 months |
| 16 Hz continuous sampling | 4400 mAh | ~3 weeks |
| 16 Hz continuous sampling | 10500 mAh | >1 month |


---

## LED Status Indicators

### Normal Operation
| Flash Count | Meaning |
|-------------|---------|
| 1-6 flashes then off | **Normal** - Program started successfully |

### Error Codes (Continuous Flashing)
| Flash Count | Meaning | Solution |
|-------------|---------|----------|
| 1 flash (repeating) | **SD Card Error** - Not initialized | Check SD card insertion; verify card format; try different card |
| 2 flashes (repeating) | **File Creation Error** - Cannot write to SD | Check SD card has free space; verify card is not write-protected; card may be faulty |

---

## Troubleshooting

### Board Not Recognized by Arduino IDE
- **Cause**: Using power-only USB cable
- **Solution**: Use a data+power micro-USB cable. Check Device Manager (Windows) for COM port.

### Date/Time Incorrect
- **Solution**: Refer to [Setting the Real-Time Clock](#4-setting-the-real-time-clock)
- Ensure CR1220 coin cell battery is installed for RTC backup

### Compilation Errors
- **Library not found**: Close and reopen Arduino IDE after installing libraries
- **Board not selected**: Ensure "Adafruit Feather 32u4" is selected in Tools → Board
- **Port not available**: Check USB cable connection and try different port

### Logger Not Working
1. Upload simple test: **File → Examples → Basics → Blink**
   - If fails: Board hardware issue
   - If works: Check below

2. Check microSD card:
   - Is it inserted correctly?
   - Using recommended 8GB card? (Large capacity cards may not be supported)
   - Is card formatted correctly?

3. Check code configuration:
   - Verify all parameters in lines 1-50 are set correctly
   - Ensure `DELAY_START = false`

### Values Don't Make Sense
1. **Check wiring**: Ensure SCL and SDA are not reversed
2. **Test sensor directly**:
   - Open **File → Examples → BlueRobotics MS5837 Library → MS5837_example**
   - Change `sensor.setModel(MS5837::MS5837_30BA);` to `sensor.setModel(MS5837::MS5837_02BA);`
   - Open Serial Monitor to verify readings

### Upload Hangs on "Waiting for upload port"
- Press the small black reset button on the board when this message appears
- This forces the board into bootloader mode

---

## Post-processing

Data files contain **absolute pressure** readings. Post-processing steps:

**Subtract atmospheric pressure** to obtain gauge pressure
   - Use nearby atmospheric reference time-series (preferred)
   - Or use estimated average atmospheric pressure
   - Conversion: 1 millibar ≈ 1 cm water height

---

## License

This project is licensed under **CC0 1.0 Universal** (Creative Commons Public Domain Dedication) - see the [LICENSE](LICENSE) file for details.

**Exception**: The `serial_number_generator` component (`Sketchbooks/serial_number_generator/`) is licensed under **GNU GPL v3** - see [Sketchbooks/serial_number_generator/license.txt](Sketchbooks/serial_number_generator/license.txt) for details.

**Included Libraries**: Arduino libraries have their own licenses, which can be viewed by searching for them in the Arduino IDE.

---

## Acknowledgments

Special thanks to:

- Bret Webb at the University of South Alabama for his research that developed the feather wave gauge.
- Jordan Cheung from the University of Washington for her help improving the wave gauge's software functionality.
