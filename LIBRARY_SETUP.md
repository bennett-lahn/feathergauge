# Library Setup Guide

This guide explains how to set up the required libraries for the Feather 32u4 programming script.

## Overview

The script installs libraries from local files in the `library_config/` directory instead of downloading them from the internet. This approach provides better control over library versions and ensures consistent builds.

## Required Libraries

Your project needs these Arduino libraries:

1. **RTClib** - Real-time clock library
2. **TimerOne** - Timer interrupt library  
3. **LowPower** - Power management library
4. **SparkFun MS5803** - Pressure sensor library
5. **MS5837** - Alternative pressure sensor library

## Library File Formats

The script supports these library formats:

### 1. Compressed Archives
- `.zip` files
- `.tar.gz` files
- `.tar.bz2` files
- `.tar.xz` files

### 2. Library Directories
- Directories containing `library.properties` or `library.json`
- Must follow Arduino library structure

## Setting Up Libraries

### Method 1: Download Library Archives

1. **Create the library_config directory:**
   ```bash
   mkdir -p library_config
   ```

2. **Download required libraries:**
   
   **RTClib:**
   ```bash
   cd library_config
   wget https://github.com/adafruit/RTClib/archive/refs/heads/master.zip -O RTClib.zip
   ```
   
   **TimerOne:**
   ```bash
   wget https://github.com/PaulStoffregen/TimerOne/archive/refs/heads/master.zip -O TimerOne.zip
   ```
   
   **LowPower:**
   ```bash
   wget https://github.com/LowPowerLab/LowPower/archive/refs/heads/master.zip -O LowPower.zip
   ```
   
   **SparkFun MS5803:**
   ```bash
   wget https://github.com/sparkfun/SparkFun_MS5803-14BA_Breakout_Arduino_Library/archive/refs/heads/main.zip -O SparkFun_MS5803.zip
   ```
   
   **MS5837:**
   ```bash
   wget https://github.com/bluerobotics/MS5837/archive/refs/heads/master.zip -O MS5837.zip
   ```

### Method 2: Clone Library Repositories

1. **Create the library_config directory:**
   ```bash
   mkdir -p library_config
   ```

2. **Clone repositories:**
   ```bash
   cd library_config
   
   # Clone each library
   git clone https://github.com/adafruit/RTClib.git
   git clone https://github.com/PaulStoffregen/TimerOne.git
   git clone https://github.com/LowPowerLab/LowPower.git
   git clone https://github.com/sparkfun/SparkFun_MS5803-14BA_Breakout_Arduino_Library.git SparkFun_MS5803
   git clone https://github.com/bluerobotics/MS5837.git
   ```

### Method 3: Manual Library Installation

1. **Download libraries from Arduino IDE Library Manager:**
   - Open Arduino IDE
   - Go to Tools → Manage Libraries
   - Search for each required library
   - Click "More info" → "Download ZIP"
   - Save to `library_config/` directory

2. **Or download from GitHub releases:**
   - Visit each library's GitHub page
   - Go to Releases section
   - Download the latest release archive
   - Place in `library_config/` directory

## Directory Structure

After setup, your `library_config/` directory should look like this:

```
library_config/
├── RTClib.zip                          # or RTClib/ directory
├── TimerOne.zip                        # or TimerOne/ directory
├── LowPower.zip                        # or LowPower/ directory
├── SparkFun_MS5803.zip                 # or SparkFun_MS5803/ directory
├── MS5837.zip                          # or MS5837/ directory
└── [any other custom libraries]
```

## Verification

Run the troubleshooting script to verify your library setup:

```bash
./troubleshoot_feather.sh
```

Look for this section in the output:
```
=== File Structure Check ===
[SUCCESS] Library config directory found: ./library_config
[INFO]   Found 5 library file(s) in library_config/
```

## Custom Libraries

To add custom libraries:

1. **Place library files in `library_config/`:**
   ```bash
   cp my_custom_library.zip library_config/
   # or
   cp -r my_custom_library/ library_config/
   ```

2. **Ensure proper library structure:**
   ```
   my_custom_library/
   ├── library.properties    # Required for Arduino library
   ├── src/                  # Source files
   │   └── MyLibrary.cpp
   ├── examples/             # Optional examples
   └── README.md             # Optional documentation
   ```

## Troubleshooting

### Common Issues

**"No library files found in library_config/"**
- Ensure the `library_config/` directory exists
- Check that library files are in the correct format
- Verify file permissions

**"Failed to install: [library_name]"**
- Check that the library file is not corrupted
- Ensure the library follows Arduino library structure
- Try extracting and re-archiving the library

**"Library not found during compilation"**
- Verify the library was installed successfully
- Check Arduino CLI library list: `arduino-cli lib list`
- Ensure library name matches what's used in the sketch

### Library Installation Verification

Check installed libraries:
```bash
arduino-cli lib list
```

Check specific library:
```bash
arduino-cli lib list | grep RTClib
```

## Library Management

### Updating Libraries

1. **Download new versions to `library_config/`**
2. **Run the programming script** - it will reinstall libraries
3. **Or manually reinstall:**
   ```bash
   arduino-cli lib uninstall RTClib
   arduino-cli lib install library_config/RTClib.zip
   ```

### Removing Libraries

```bash
arduino-cli lib uninstall [library_name]
```

### Listing All Libraries

```bash
arduino-cli lib list
```

## Best Practices

1. **Version Control:** Add `library_config/` to your `.gitignore` if libraries are large
2. **Documentation:** Keep a `library_versions.txt` file listing library versions
3. **Backup:** Keep backup copies of working library versions
4. **Testing:** Test with one board before programming multiple boards
5. **Consistency:** Use the same library versions across all development machines

## Example library_versions.txt

```
RTClib: 2.1.1
TimerOne: 1.1.0
LowPower: 1.81
SparkFun_MS5803: 1.0.0
MS5837: 1.0.0
```

This approach ensures reproducible builds and gives you full control over the library versions used in your project.
