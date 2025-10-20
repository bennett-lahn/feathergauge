# Windows WSL Programming Guide

This guide explains how to use the Feather 32u4 programming scripts on Windows with WSL. This guide assumes that all files are structured exactly as they appear in the feathergauge [GitHub](https://github.com/bennett-lahn/feathergauge). Clone/download the GitHub repository and then run the programming scripts from there. 

## Quick Start

1. **Keep WSL Terminal Open**: Open a WSL terminal and keep it running to maintain the WSL 2 VM.

2. **Run the PowerShell Wrapper**: From Windows PowerShell or Command Prompt as Administrator:

   **Option A - Modify PowerShell Execution Policy**:
   ```powershell
   Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
   .\program_feather_boards_windows.ps1
   ```
   Note that this will change PowerShell settings to allow other scripts to be run in the future for this user. This change does not require administrator permissions and does not affect security, as any user can modify the execution policy. It only prevents inexperienced users unintentionally running PowerShell scripts. 

   **Option B - Command Prompt**:
   ```cmd
   powershell -ExecutionPolicy Bypass -File .\program_feather_boards_windows.ps1
   ```

   **Option C - One-time execution**:
   ```powershell
   powershell -ExecutionPolicy Bypass -Command "& '.\program_feather_boards_windows.ps1'"
   ```

3. **Follow the Prompts**: The script will automatically:
   - Install usbipd-win if needed
   - Detect ALL connected Adafruit devices (VID 239A)
   - **WARNING**: Bind and attach ALL Adafruit devices to WSL (they will lose Windows functionality)
   - Run the Linux programming script
   - Detach and unbind devices when complete (restoring Windows functionality)

## Prerequisites

- **PowerShell running as Administrator** (REQUIRED)
- **WSL 2** installed and running
- **Windows Package Manager (winget)** for installing usbipd-win
- **Adafruit devices** (any with VID 239A) connected via USB
- **WSL terminal open** to keep the WSL 2 VM running

## Manual Steps (if needed)

If the automatic detection fails, you can manually bind devices:

1. **List USB devices**:
   ```powershell
   usbipd list
   ```

2. **Bind and attach Adafruit devices to WSL**:
   ```powershell
   usbipd bind --busid <BUSID>
   usbipd attach --wsl --busid <BUSID>
   ```
   **Note**: This will make the device unavailable to Windows while bound.

3. **Run the Linux script**:
   ```bash
   wsl
   cd /mnt/c/hw_projects/feathergauge
   ./program_feather_boards.sh
   ```

4. **Detach and unbind devices when done**:
   ```powershell
   usbipd detach --busid <BUSID>
   usbipd unbind --busid <BUSID>
   ```

## Troubleshooting

### "WSL not found"
- Install WSL 2: `wsl --install`
- Restart your computer after installation

### "No Adafruit devices detected"
- Check USB connections
- Install Adafruit drivers
- Try different USB ports
- Check device manager for unrecognized devices
- Verify devices have Adafruit vendor ID (239A)

### "This script MUST be run as Administrator"
- Right-click PowerShell and select "Run as Administrator"
- Or run from Command Prompt as Administrator:
  ```cmd
  powershell -ExecutionPolicy Bypass -File .\program_feather_boards_windows.ps1
  ```

### "Execution of scripts is disabled on this system"
- **Option A - Set execution policy (Recommended)**:
  ```powershell
  Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
  .\program_feather_boards_windows.ps1
  ```
- **Option B - Bypass execution policy**:
  ```cmd
  powershell -ExecutionPolicy Bypass -File .\program_feather_boards_windows.ps1
  ```
- **Option C - One-time execution**:
  ```powershell
  powershell -ExecutionPolicy Bypass -Command "& '.\program_feather_boards_windows.ps1'"
  ```

### "Failed to bind devices"
- Ensure PowerShell is running as Administrator
- Check if devices are already bound: `usbipd list`
- Detach and unbind first: `usbipd detach --busid <BUSID>` then `usbipd unbind --busid <BUSID>`

### "Linux script not found"
- Make sure `program_feather_boards.sh` is in the same directory
- Check file permissions (should be executable)

## File Structure

```
feathergauge/
├── program_feather_boards_windows.ps1    # Windows PowerShell wrapper
├── program_feather_boards.sh             # Linux programming script
├── Sketchbooks/
│   ├── eeprom_test/
│   ├── Codes_v1/
│   │   ├── rtc_setup/
│   │   └── jorch_featherDIY_customizable/
│   └── serial_number_generator/
└── library_config/
    ├── Low-Power-1.81.zip
    └── RTClib-2.1.4.zip
```

## Script Options

The PowerShell wrapper supports these options:

```powershell
.\program_feather_boards_windows.ps1 -Help
.\program_feather_boards_windows.ps1 -WSLPath "wsl" -ScriptPath "program_feather_boards.sh"
```

## Native Linux Usage

If running on native Linux (not WSL), use the script directly:

```bash
./program_feather_boards.sh
```

The script automatically detects the environment and adjusts device detection accordingly.
