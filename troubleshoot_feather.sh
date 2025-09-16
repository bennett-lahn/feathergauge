#!/bin/bash

# Feather 32u4 Troubleshooting Script
# This script helps diagnose common issues with Feather board programming

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check system information
check_system() {
    log_info "=== System Information ==="
    echo "OS: $(uname -a)"
    echo "User: $(whoami)"
    echo "Groups: $(groups)"
    echo "Current directory: $(pwd)"
    echo ""
}

# Check required tools
check_tools() {
    log_info "=== Checking Required Tools ==="
    
    local tools=("arduino-cli" "avrdude" "python3")
    local all_found=true
    
    for tool in "${tools[@]}"; do
        if command -v "$tool" &> /dev/null; then
            local version=$($tool --version 2>/dev/null | head -n1 || echo "version unknown")
            log_success "$tool: $version"
        else
            log_error "$tool: NOT FOUND"
            all_found=false
        fi
    done
    
    if [ "$all_found" = false ]; then
        log_error "Some required tools are missing!"
        echo ""
        log_info "Installation commands:"
        echo "  Ubuntu/Debian:"
        echo "    sudo apt-get install avrdude python3"
        echo "    curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh"
        echo ""
        echo "  macOS:"
        echo "    brew install arduino-cli avrdude python3"
        echo ""
        return 1
    fi
    
    log_success "All required tools found!"
    echo ""
    return 0
}

# Check USB devices
check_usb_devices() {
    log_info "=== USB Device Detection ==="
    
    # Check for common USB serial devices
    local devices_found=false
    
    for pattern in "/dev/ttyUSB*" "/dev/ttyACM*" "/dev/tty.usbmodem*" "/dev/tty.usbserial*"; do
        for device in $pattern; do
            if [ -e "$device" ]; then
                log_success "Found device: $device"
                devices_found=true
                
                # Check permissions
                if [ -r "$device" ] && [ -w "$device" ]; then
                    log_success "  Permissions: OK (read/write)"
                else
                    log_warning "  Permissions: Limited (may need sudo or group membership)"
                fi
                
                # Check if device is in use
                if lsof "$device" &>/dev/null; then
                    log_warning "  Status: In use by another process"
                else
                    log_success "  Status: Available"
                fi
            fi
        done
    done
    
    if [ "$devices_found" = false ]; then
        log_warning "No USB serial devices found"
        echo ""
        log_info "Troubleshooting steps:"
        echo "  1. Check USB cable (use data cable, not just power)"
        echo "  2. Try different USB port"
        echo "  3. Check if device shows up in lsusb:"
        lsusb 2>/dev/null || echo "    lsusb command not available"
        echo "  4. Check dmesg for USB events:"
        dmesg | tail -10 | grep -i usb || echo "    No recent USB events in dmesg"
    fi
    
    echo ""
}

# Check Arduino CLI configuration
check_arduino_cli() {
    log_info "=== Arduino CLI Configuration ==="
    
    if [ ! -f ~/.arduino15/arduino-cli.yaml ]; then
        log_warning "Arduino CLI not initialized"
        log_info "Run: arduino-cli config init"
        return 1
    fi
    
    log_success "Arduino CLI configuration found"
    
    # Check if Adafruit core is installed
    if arduino-cli core list | grep -q "adafruit:avr"; then
        log_success "Adafruit AVR core installed"
    else
        log_warning "Adafruit AVR core not installed"
        log_info "Run: arduino-cli core install adafruit:avr"
    fi
    
    # Check installed libraries
    log_info "Installed libraries:"
    arduino-cli lib list | grep -E "(RTClib|TimerOne|LowPower|SparkFun|MS5837)" || log_warning "Required libraries not found"
    
    echo ""
}

# Check file structure
check_file_structure() {
    log_info "=== File Structure Check ==="
    
    local script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    local sketch_dir="$script_dir/Sketchbooks/Codes_v1/jorch_featherDIY_customizable"
    local library_config_dir="$script_dir/library_config"
    
    # Check main script
    if [ -f "$script_dir/program_feather_boards.sh" ]; then
        log_success "Main script found: program_feather_boards.sh"
        if [ -x "$script_dir/program_feather_boards.sh" ]; then
            log_success "  Script is executable"
        else
            log_warning "  Script is not executable (run: chmod +x program_feather_boards.sh)"
        fi
    else
        log_error "Main script not found: program_feather_boards.sh"
    fi
    
    # Check sketch file
    if [ -f "$sketch_dir/jorch_featherDIY_customizable.ino" ]; then
        log_success "Sketch file found: $sketch_dir/jorch_featherDIY_customizable.ino"
    else
        log_error "Sketch file not found: $sketch_dir/jorch_featherDIY_customizable.ino"
    fi
    
    # Check library config directory
    if [ -d "$library_config_dir" ]; then
        log_success "Library config directory found: $library_config_dir"
        local library_count=$(find "$library_config_dir" -type f \( -name "*.zip" -o -name "*.tar.gz" -o -name "*.tar.bz2" -o -name "*.tar.xz" \) | wc -l)
        local dir_count=$(find "$library_config_dir" -type d -exec test -f {}/library.properties -o -f {}/library.json \; -print | wc -l)
        local total_libraries=$((library_count + dir_count))
        log_info "  Found $total_libraries library file(s) in library_config/"
    else
        log_warning "Library config directory not found: $library_config_dir"
        log_info "  Create this directory and add your library files (.zip, .tar.gz, or library directories)"
    fi
    
    echo ""
}

# Test compilation
test_compilation() {
    log_info "=== Compilation Test ==="
    
    local script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    local sketch_dir="$script_dir/Sketchbooks/Codes_v1/jorch_featherDIY_customizable"
    
    if [ ! -f "$sketch_dir/jorch_featherDIY_customizable.ino" ]; then
        log_error "Cannot test compilation - sketch file not found"
        return 1
    fi
    
    log_info "Testing sketch compilation..."
    
    if arduino-cli compile --fqbn adafruit:avr:feather32u4 "$sketch_dir" 2>/dev/null; then
        log_success "Compilation test passed"
    else
        log_error "Compilation test failed"
        log_info "Try running: arduino-cli compile --fqbn adafruit:avr:feather32u4 --verbose $sketch_dir"
    fi
    
    echo ""
}

# Test device communication
test_device_communication() {
    log_info "=== Device Communication Test ==="
    
    local devices_found=false
    
    for pattern in "/dev/ttyUSB*" "/dev/ttyACM*" "/dev/tty.usbmodem*" "/dev/tty.usbserial*"; do
        for device in $pattern; do
            if [ -e "$device" ]; then
                devices_found=true
                log_info "Testing communication with $device..."
                
                # Test basic avrdude communication
                if timeout 5 avrdude -p atmega32u4 -c avr109 -P "$device" -b 1200 -r -D -U flash:r:/dev/null:i 2>/dev/null; then
                    log_success "  Communication OK with $device"
                else
                    log_warning "  Communication failed with $device"
                fi
            fi
        done
    done
    
    if [ "$devices_found" = false ]; then
        log_warning "No devices found to test"
    fi
    
    echo ""
}

# Provide recommendations
provide_recommendations() {
    log_info "=== Recommendations ==="
    
    echo "Based on the diagnostic results:"
    echo ""
    
    # Check if user is in dialout group
    if ! groups | grep -q dialout; then
        log_warning "User not in dialout group - this may cause permission issues"
        echo "  Fix: sudo usermod -a -G dialout $USER (then logout/login)"
        echo ""
    fi
    
    # Check if arduino-cli is properly configured
    if ! arduino-cli core list | grep -q "adafruit:avr"; then
        log_warning "Adafruit AVR core not installed"
        echo "  Fix: arduino-cli core install adafruit:avr"
        echo ""
    fi
    
    # Check if required libraries are installed
    local missing_libs=()
    for lib in "RTClib" "TimerOne" "LowPower" "SparkFun MS5803" "MS5837"; do
        if ! arduino-cli lib list | grep -q "$lib"; then
            missing_libs+=("$lib")
        fi
    done
    
    if [ ${#missing_libs[@]} -gt 0 ]; then
        log_warning "Missing required libraries: ${missing_libs[*]}"
        echo "  Fix: arduino-cli lib install \"${missing_libs[*]}\""
        echo ""
    fi
    
    log_info "If all checks pass, try running: ./program_feather_boards.sh"
}

# Main function
main() {
    echo "Feather 32u4 Troubleshooting Script"
    echo "===================================="
    echo ""
    
    check_system
    check_tools
    check_usb_devices
    check_arduino_cli
    check_file_structure
    test_compilation
    test_device_communication
    provide_recommendations
    
    echo ""
    log_info "Troubleshooting complete!"
}

# Run main function
main "$@"
