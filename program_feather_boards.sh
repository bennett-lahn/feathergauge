#!/bin/bash

# Multi-Feather 32u4 Programming Script
# This script compiles Arduino code and programs multiple Adafruit Feather 32u4 boards
# using arduino-cli for compilation and avrdude for programming

# Caution: this file overwrites your current arduino-cli cofig settings

set -e  # Exit on any error

# Configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARDUINO_SKETCH_DIR="${SCRIPT_DIR}/Sketchbooks/Codes_v1/jorch_featherDIY_customizable"
RTC_SETUP_SKETCH_DIR="${SCRIPT_DIR}/Sketchbooks/Codes_v1/rtc_setup"
EEPROM_TEST_SKETCH_DIR="${SCRIPT_DIR}/Sketchbooks/eeprom_test"
LIBRARY_CONFIG_DIR="${SCRIPT_DIR}/libraries"
BOARD_FQBN="adafruit:avr:feather32u4"
COMPILED_SKETCH_DIR="${SCRIPT_DIR}/compiled_sketches"
LOG_FILE="${SCRIPT_DIR}/programming_log_$(date +%Y%m%d_%H%M%S).log"
SERIAL_TIMEOUT=30
SKIP_RTC_SETUP=false

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Logging functions
log() {
    echo -e "$1" | tee -a "$LOG_FILE"
}

log_info() {
    log "${BLUE}[INFO]${NC} $1"
}

log_success() {
    log "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    log "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    log "${RED}[ERROR]${NC} $1"
}

# Check if required tools are installed
check_dependencies() {
    log_info "Checking dependencies..."
    
    local missing_tools=()
    
    if ! command -v arduino-cli &> /dev/null; then
        missing_tools+=("arduino-cli")
    fi
    
    if ! command -v avrdude &> /dev/null; then
        missing_tools+=("avrdude")
    fi
    
    if ! command -v python3 &> /dev/null; then
        missing_tools+=("python3")
    fi
    
    if [ ${#missing_tools[@]} -ne 0 ]; then
        log_error "Missing required tools: ${missing_tools[*]}"
        log_info "Install instructions:"
        log_info "  arduino-cli: sudo snap install arduino-cli https://arduino.github.io/arduino-cli/latest/installation/"
        log_info "  avrdude: sudo apt-get install avrdude (Ubuntu/Debian) or brew install avrdude (macOS)"
        log_info "  python3: sudo apt-get install python3 (Ubuntu/Debian) or brew install python3 (macOS)"
        exit 1
    fi
    
    log_success "All dependencies found"
}

# Setup Arduino CLI
setup_arduino_cli() {
    log_info "Setting up Arduino CLI..."
    
    # Initialize Arduino CLI if not already done
    if [ ! -f ~/.arduino15/arduino-cli.yaml ]; then
        arduino-cli config init --overwrite
    fi
    
    # Persist Adafruit boards manager URL in config and install the cores
    arduino-cli config set board_manager.additional_urls https://adafruit.github.io/arduino-board-index/package_adafruit_index.json
    # Allow installing libraries from ZIPs
    arduino-cli config set library.enable_unsafe_install true
    arduino-cli core update-index
    # Install base Arduino AVR core (dependency) and Adafruit AVR core
    arduino-cli core install arduino:avr || true
    arduino-cli core install adafruit:avr
    
    # Ensure common core libraries are available
    install_core_libraries
    
    # Install libraries from local files
    # It is more likely that updates to these libraries will break things, so a local copy is maintained instead
    install_local_libraries
    
    log_success "Arduino CLI setup complete"
}

# Install common Arduino core libraries used by sketches
install_core_libraries() {
    log_info "Ensuring core libraries are installed (SD)..."
    local core_libs=(`SD)
    for lib in "${core_libs[@]}"; do
        if arduino-cli lib install "$lib"; then
            log_success "Library installed: $lib"
        else
            log_warning "Failed to install library: $lib"
        fi
    done
}

# Install libraries from local files in libraries directory
install_local_libraries() {
    log_info "Installing libraries from local files..."
    
    local library_config_dir="${SCRIPT_DIR}/libraries"
    local libraries_dir="${SCRIPT_DIR}/libraries"
    
    # Create libraries directory if it doesn't exist
    mkdir -p "$libraries_dir"
    
    # Check if library_config directory exists
    if [ ! -d "$library_config_dir" ]; then
        log_warning "libraries directory not found: $library_config_dir"
        log_info "Please create the directory and add your library files"
        return 1
    fi
    
    # Find and install library files
    local library_files=()
    
    # Look for common library file patterns
    for pattern in "*.zip" "*.tar.gz" "*.tar.bz2" "*.tar.xz"; do
        while IFS= read -r -d '' file; do
            library_files+=("$file")
        done < <(find "$library_config_dir" -name "$pattern" -print0 2>/dev/null)
    done
    
    # Also look for library directories
    while IFS= read -r -d '' dir; do
        if [ -f "$dir/library.properties" ] || [ -f "$dir/library.json" ]; then
            library_files+=("$dir")
        fi
    done < <(find "$library_config_dir" -type d -print0 2>/dev/null)
    
    if [ ${#library_files[@]} -eq 0 ]; then
        log_warning "No library files found in $library_config_dir"
        log_info "Supported formats: .zip, .tar.gz, .tar.bz2, .tar.xz, or library directories"
        log_info "Library directories should contain library.properties or library.json"
        return 1
    fi
    
    log_info "Found ${#library_files[@]} library file(s) to install"
    
    # Ensure user libraries directory exists
    local user_libraries_dir="$HOME/Arduino/libraries"
    mkdir -p "$user_libraries_dir"
    
    # Install each library
    for library_file in "${library_files[@]}"; do
        local library_name=$(basename "$library_file")
        log_info "Installing library: $library_name"
        
        if [ -f "$library_file" ]; then
            # ZIP/TAR archive install via arduino-cli
            if arduino-cli lib install --zip-path "$library_file"; then
                log_success "Successfully installed: $library_name"
            else
                log_warning "Failed to install: $library_name"
            fi
        elif [ -d "$library_file" ]; then
            # Copy library directory directly into user libraries
            local dest_dir="$user_libraries_dir/$(basename "$library_file")"
            rm -rf "$dest_dir"
            if cp -R "$library_file" "$dest_dir"; then
                log_success "Copied library directory: $library_name"
            else
                log_warning "Failed to copy library directory: $library_name"
            fi
        else
            log_warning "Unknown library item type: $library_name"
        fi
    done
}

# Create required directory structure
create_directory_structure() {
    log_info "Creating directory structure..."
    
    mkdir -p "$COMPILED_SKETCH_DIR"
    mkdir -p "$ARDUINO_SKETCH_DIR/libraries"
    
    log_success "Directory structure created"
}

# Compile the Arduino sketch
compile_sketch() {
    log_info "Compiling Arduino sketch..."
    
    if [ ! -f "$ARDUINO_SKETCH_DIR/jorch_featherDIY_customizable.ino" ]; then
        log_error "Sketch file not found: $ARDUINO_SKETCH_DIR/jorch_featherDIY_customizable.ino"
        exit 1
    fi
    
    # Compile the sketch
    # In the future, could use CPP flags to make code configuration modifiable without editing code
    arduino-cli compile \
        --fqbn "$BOARD_FQBN" \
        --output-dir "$COMPILED_SKETCH_DIR" \
        "$ARDUINO_SKETCH_DIR"
    
    if [ $? -eq 0 ]; then
        log_success "Sketch compiled successfully"
        log_info "Compiled binary: $COMPILED_SKETCH_DIR/jorch_featherDIY_customizable.ino.hex"
    else
        log_error "Sketch compilation failed"
        exit 1
    fi
}

# Compile the RTC setup sketch
compile_rtc_setup_sketch() {
    log_info "Compiling RTC setup sketch..."
    
    if [ ! -f "$RTC_SETUP_SKETCH_DIR/rtc_setup.ino" ]; then
        log_error "RTC setup sketch file not found: $RTC_SETUP_SKETCH_DIR/rtc_setup.ino"
        exit 1
    fi
    
    # Compile the RTC setup sketch
    arduino-cli compile \
        --fqbn "$BOARD_FQBN" \
        --output-dir "$COMPILED_SKETCH_DIR" \
        "$RTC_SETUP_SKETCH_DIR"
    
    if [ $? -eq 0 ]; then
        log_success "RTC setup sketch compiled successfully"
        log_info "Compiled binary: $COMPILED_SKETCH_DIR/rtc_setup.ino.hex"
    else
        log_error "RTC setup sketch compilation failed"
        exit 1
    fi
}

# Compile the EEPROM test sketch
compile_eeprom_test_sketch() {
    log_info "Compiling EEPROM test sketch..."
    
    if [ ! -f "$EEPROM_TEST_SKETCH_DIR/eeprom_test.ino" ]; then
        log_error "EEPROM test sketch file not found: $EEPROM_TEST_SKETCH_DIR/eeprom_test.ino"
        exit 1
    fi
    
    arduino-cli compile \
        --fqbn "$BOARD_FQBN" \
        --output-dir "$COMPILED_SKETCH_DIR" \
        "$EEPROM_TEST_SKETCH_DIR"
    
    if [ $? -eq 0 ]; then
        log_success "EEPROM test sketch compiled successfully"
        log_info "Compiled binary: $COMPILED_SKETCH_DIR/eeprom_test.ino.hex"
    else
        log_error "EEPROM test sketch compilation failed"
        exit 1
    fi
}

# Detect connected serial ports
detect_ports() {
    log_info "Detecting connected serial ports..."
    
    # Find all USB serial devices (common patterns for Feather boards)
    local ports=()
    
    # Check if running in WSL (usbipd devices)
    if [ -n "$WSL_DISTRO_NAME" ] || [ -n "$WSLENV" ]; then
        log_info "Running in WSL - checking for usbipd devices..."
        # In WSL, usbipd devices typically appear as /dev/ttyUSB* or /dev/ttyACM*
        for device in /dev/ttyUSB* /dev/ttyACM*; do
            if [ -e "$device" ]; then
                # Check if device is actually a Feather 32u4 by trying to communicate
                if check_feather_device "$device"; then
                    ports+=("$device")
                    log_info "Found Feather 32u4 at: $device (WSL usbipd device)"
                fi
            fi
        done
    else
        # Native Linux - check for all common patterns
        for device in /dev/ttyUSB* /dev/ttyACM* /dev/tty.usbmodem* /dev/tty.usbserial*; do
            if [ -e "$device" ]; then
                # Check if device is actually a Feather 32u4 by trying to communicate
                if check_feather_device "$device"; then
                    ports+=("$device")
                    log_info "Found Feather 32u4 at: $device"
                fi
            fi
        done
    fi
    
    if [ ${#ports[@]} -eq 0 ]; then
        log_warning "No Feather 32u4 devices detected"
        log_info "Make sure boards are connected and drivers are installed"
        if [ -n "$WSL_DISTRO_NAME" ] || [ -n "$WSLENV" ]; then
            log_info "WSL detected - make sure devices are bound with usbipd"
            log_info "Run from Windows: usbipd bind --busid <BUSID>"
        else
            log_info "Common device paths: /dev/ttyUSB*, /dev/ttyACM*, /dev/tty.usbmodem*"
        fi
        return 1
    fi
    
    log_success "Found ${#ports[@]} Feather 32u4 device(s): ${ports[*]}"
    echo "${ports[@]}"
}

# Check if a device is a Feather 32u4
check_feather_device() {
    local device="$1"
    
    # Try to get device info using arduino-cli
    if arduino-cli board list | grep -q "$device"; then
        return 0
    fi
    
    # Alternative check using lsusb (if available)
    if command -v lsusb &> /dev/null; then
        if lsusb | grep -i "adafruit\|feather\|32u4" &> /dev/null; then
            return 0
        fi
    fi
    
    return 1
}

# Program a single board
program_board() {
    local device="$1"
    local hex_file="$2"
    local board_num="$3"
    
    log_info "Programming board $board_num on $device..."
    
    # Program the device with bootloader reset using -r flag
    # The -r flag performs 1200 bps reset and then programs at 57600 bps
    if avrdude -p atmega32u4 -c avr109 -P "$device" -b 1200 -r -D -U "flash:w:$hex_file:i"; then
        log_success "Successfully programmed board $board_num on $device"
        return 0
    else
        log_error "Failed to program board $board_num on $device"
        return 1
    fi
}

# Setup RTC on a single board
setup_rtc_board() {
    local device="$1"
    local board_num="$2"
    
    log_info "Setting up RTC on board $board_num on $device..."
    
    # Program the RTC setup sketch
    local rtc_hex_file="$COMPILED_SKETCH_DIR/rtc_setup.ino.hex"
    if ! program_board "$device" "$rtc_hex_file" "$board_num"; then
        log_error "Failed to program RTC setup sketch on board $board_num"
        return 1
    fi
    
    # Wait for the board to boot and initialize
    log_info "Waiting for board to initialize..."
    sleep 3

    # Get current system time
    local current_time=$(date '+%Y-%m-%d %H:%M:%S')
    log_info "Current system time: $current_time"
    
    # Send time via serial communication
    if ! send_time_via_serial "$device" "$current_time"; then
        log_error "Failed to set RTC time on board $board_num"
        return 1
    fi
    
    log_success "RTC setup completed for board $board_num"
    return 0
}

# Send time via serial communication
send_time_via_serial() {
    local device="$1"
    local time_string="$2"
    
    log_info "Sending time to $device: $time_string"
    
    # Use python3 for serial communication
    python3 -c "
import serial
import time
import sys

try:
    # Open serial connection
    ser = serial.Serial('$device', 57600, timeout=10)
    time.sleep(2)  # Wait for Arduino to initialize
    
    # Send time string
    ser.write(b'$time_string\\n')
    ser.flush()
    
    # Read response
    start_time = time.time()
    while time.time() - start_time < $SERIAL_TIMEOUT:
        if ser.in_waiting > 0:
            line = ser.readline().decode('utf-8').strip()
            print(f'[ARDUINO] {line}')
            if 'SUCCESS' in line:
                ser.close()
                sys.exit(0)
            elif 'ERROR' in line:
                ser.close()
                sys.exit(1)
        time.sleep(0.1)
    
    print('Timeout waiting for response')
    ser.close()
    sys.exit(1)
    
except Exception as e:
    print(f'Serial communication error: {e}')
    sys.exit(1)
" 2>&1 | tee -a "$LOG_FILE"
    
    return $?
}

# Read EEPROM serial number via serial and validate it is initialized
read_and_validate_eeprom_serial() {
    local device="$1"
    
    log_info "Reading EEPROM serial value from $device"
    
    python3 -c "
import serial
import time
import sys

PREFIX = 'Read serial number from EEPROM: '

try:
    ser = serial.Serial('$device', 57600, timeout=10)
    time.sleep(2)
    deadline = time.time() + $SERIAL_TIMEOUT
    eeprom_value = None
    while time.time() < deadline:
        if ser.in_waiting > 0:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            print(f'[ARDUINO] {line}')
            if line.startswith(PREFIX):
                eeprom_value = line[len(PREFIX):].strip()
                break
        time.sleep(0.1)
    ser.close()
    if not eeprom_value:
        print('ERROR: No EEPROM value read')
        sys.exit(1)
    # Basic validation: non-empty, at least one alphanumeric character
    if any(ch.isalnum() for ch in eeprom_value):
        print(f'SUCCESS: EEPROM value detected: {eeprom_value}')
        sys.exit(0)
    print(f'ERROR: EEPROM value appears uninitialized: {eeprom_value!r}')
    sys.exit(1)
except Exception as e:
    print(f'Serial communication error: {e}')
    sys.exit(1)
" 2>&1 | tee -a "$LOG_FILE"
    
    return $?
}

# Program EEPROM test sketch and validate EEPROM value on a single board
test_eeprom_on_board() {
    local device="$1"
    local board_num="$2"
    
    log_info "Testing EEPROM on board $board_num on $device..."
    local eeprom_hex_file="$COMPILED_SKETCH_DIR/eeprom_test.ino.hex"
    if ! program_board "$device" "$eeprom_hex_file" "$board_num"; then
        log_error "Failed to program EEPROM test sketch on board $board_num"
        return 1
    fi
    
    # Wait briefly for boot
    sleep 2
    
    if read_and_validate_eeprom_serial "$device"; then
        log_success "EEPROM appears initialized on board $board_num"
        return 0
    else
        log_error "EEPROM not initialized or unreadable on board $board_num"
        return 1
    fi
}

# Program all detected boards with RTC setup
program_all_boards() {
    local ports=("$@")
    local main_hex_file="$COMPILED_SKETCH_DIR/jorch_featherDIY_customizable.ino.hex"
    local success_count=0
    local total_count=${#ports[@]}
    
    log_info "Starting two-stage programming for $total_count board(s)..."
    log_info "Stage 0: EEPROM test, Stage 1: RTC setup, Stage 2: Main program"
    
    for i in "${!ports[@]}"; do
        local port="${ports[$i]}"
        local board_num=$((i + 1))
        
        log_info "Processing board $board_num of $total_count on $port"

        # Stage 0: Check EEPROM
        log_info "Stage 0: Testing EEPROM on board $board_num"
        if ! test_eeprom_on_board "$port" "$board_num"; then
            log_warning "Skipping board $board_num due to EEPROM test failure"
            continue
        fi
        
        # Stage 1: Setup RTC
        log_info "Stage 1: Setting up RTC on board $board_num"
        if ! setup_rtc_board "$port" "$board_num"; then
            log_error "RTC setup failed for board $board_num, skipping main program"
            continue
        fi
        
        # Small delay between RTC setup and main programming
        sleep 2
        
        # Stage 2: Program main sketch
        log_info "Stage 2: Programming main sketch on board $board_num"
        if program_board "$port" "$main_hex_file" "$board_num"; then
            ((success_count++))
            log_success "Board $board_num completed both stages successfully"
        else
            log_warning "Main program failed for board $board_num (RTC was set successfully)"
        fi
        
        # Small delay between boards
        sleep 1
    done
    
    log_info "Two-stage programming complete: $success_count/$total_count boards completed successfully"
    
    if [ $success_count -eq $total_count ]; then
        log_success "All boards programmed successfully with RTC setup!"
        return 0
    else
        log_warning "Some boards failed to complete both stages. Check the log for details."
        return 1
    fi
}

# Main function
main() {
    log_info "Starting Feather 32u4 Multi-Board Programming Script"
    log_info "Log file: $LOG_FILE"
    
    # Check dependencies
    check_dependencies
    
    # Setup Arduino CLI
    setup_arduino_cli
    
    # Create directory structure
    create_directory_structure
    
    # Compile sketches
    if [ "$SKIP_RTC_SETUP" = true ]; then
        log_info "Skipping RTC setup - compiling main sketch only"
        compile_sketch
    else
        log_info "Two-stage programming enabled - compiling EEPROM test, RTC setup, and main sketches"
        compile_eeprom_test_sketch
        compile_rtc_setup_sketch
        compile_sketch
    fi
    
    # Detect connected ports
    local ports
    if ! ports=($(detect_ports)); then
        log_error "No Feather 32u4 devices found. Exiting."
        exit 1
    fi
    
    # Program all boards
    if [ "$SKIP_RTC_SETUP" = true ]; then
        # Simple programming without RTC setup
        program_boards_simple "${ports[@]}"
    else
        # Two-stage programming with RTC setup
        program_all_boards "${ports[@]}"
    fi
    
    if [ $? -eq 0 ]; then
        log_success "All operations completed successfully!"
        exit 0
    else
        log_error "Some operations failed. Check the log for details."
        exit 1
    fi
}

# Simple programming without RTC setup
program_boards_simple() {
    local ports=("$@")
    local hex_file="$COMPILED_SKETCH_DIR/jorch_featherDIY_customizable.ino.hex"
    local success_count=0
    local total_count=${#ports[@]}
    
    log_info "Starting simple programming for $total_count board(s)..."
    
    for i in "${!ports[@]}"; do
        local port="${ports[$i]}"
        local board_num=$((i + 1))
        
        log_info "Processing board $board_num of $total_count on $port"
        
        if program_board "$port" "$hex_file" "$board_num"; then
            ((success_count++))
        else
            log_warning "Board $board_num programming failed, continuing with next board..."
        fi
        
        # Small delay between boards
        sleep 1
    done
    
    log_info "Programming complete: $success_count/$total_count boards programmed successfully"
    
    if [ $success_count -eq $total_count ]; then
        log_success "All boards programmed successfully!"
        return 0
    else
        log_warning "Some boards failed to program. Check the log for details."
        return 1
    fi
}

# Show usage information
show_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -h, --help         Show this help message"
    echo "  -s, --sketch       Specify sketch directory (default: jorch_featherDIY_customizable)"
    echo "  -o, --output       Specify output directory for compiled sketches"
    echo "  -l, --log          Specify log file location"
    echo "  --skip-rtc         Skip RTC setup and only program main sketch"
    echo ""
    echo "Required file structure:"
    echo "  Sketchbooks/Codes_v1/jorch_featherDIY_customizable/"
    echo "    └── jorch_featherDIY_customizable.ino"
    echo "  Sketchbooks/Codes_v1/rtc_setup/"
    echo "    └── rtc_setup.ino"
    echo "  Sketchbooks/eeprom_test/"
    echo "    └── eeprom_test.ino"
    echo "  libraries/"
    echo "    └── [library files: .zip, .tar.gz, or library directories]"
    echo ""
    echo "Required libraries (installed from libraries/):"
    echo "  - RTClib"
    echo "  - TimerOne" 
    echo "  - LowPower"
    echo "  - SparkFun MS5803 14-20 Bar Pressure Sensor"
    echo "  - MS5837"
    echo "  - Any other custom libraries"
    echo ""
    echo "The script will:"
    echo "  1. Check for required tools (arduino-cli, avrdude, python3)"
    echo "  2. Setup Arduino CLI and install required libraries"
    echo "  3. Compile the Arduino sketches (EEPROM test + RTC setup + main program)"
    echo "  4. Detect connected Feather 32u4 boards"
    echo "  5. Program each board with stages:"
    echo "     - Stage 0: Flash EEPROM test sketch and verify EEPROM value"
    echo "     - Stage 1: Flash RTC setup program and set current time"
    echo "     - Stage 2: Flash main program"
    echo "  6. Use --skip-rtc to skip RTC setup and only flash main program (EEPROM test still runs)"
    echo "CAUTION: This script overwrites the current arduino-cli config settings. Back them up if you want to keep them."
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_usage
            exit 0
            ;;
        -s|--sketch)
            ARDUINO_SKETCH_DIR="$2"
            shift 2
            ;;
        -o|--output)
            COMPILED_SKETCH_DIR="$2"
            shift 2
            ;;
        -l|--log)
            LOG_FILE="$2"
            shift 2
            ;;
        --skip-rtc)
            SKIP_RTC_SETUP=true
            shift
            ;;
        *)
            log_error "Unknown option: $1"
            show_usage
            exit 1
            ;;
    esac
done

# Run main function
main "$@"
