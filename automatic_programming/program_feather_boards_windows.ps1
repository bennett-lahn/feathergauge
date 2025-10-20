# PowerShell Wrapper for Feather 32u4 Programming Script
# This script handles USB device binding to WSL and runs the Linux programming script
# Run this from Windows PowerShell or Command Prompt

param(
    [string]$WSLPath = "wsl",
    [string]$ScriptPath = "program_feather_boards.sh",
    [switch]$Help
)

if ($Help) {
    Write-Host "PowerShell Wrapper for Feather 32u4 Programming Script" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Usage: .\program_feather_boards_windows.ps1 [OPTIONS]" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Options:" -ForegroundColor Yellow
    Write-Host "  -WSLPath <path>     Path to WSL executable (default: wsl)" -ForegroundColor White
    Write-Host "  -ScriptPath <path>  Path to the Linux script (default: program_feather_boards.sh)" -ForegroundColor White
    Write-Host "  -Help              Show this help message" -ForegroundColor White
    Write-Host ""
    Write-Host "Prerequisites:" -ForegroundColor Yellow
    Write-Host "  1. PowerShell running as Administrator (REQUIRED)" -ForegroundColor White
    Write-Host "  2. WSL 2 installed and running" -ForegroundColor White
    Write-Host "  3. Keep a WSL terminal open to keep the WSL 2 VM running" -ForegroundColor White
    Write-Host "  4. Linux programming script in the same directory" -ForegroundColor White
    Write-Host ""
    Write-Host "This script will:" -ForegroundColor Yellow
    Write-Host "  1. Install usbipd-win if not present" -ForegroundColor White
    Write-Host "  2. Detect Adafruit devices (VID 239A)" -ForegroundColor White
    Write-Host "  3. Bind and attach devices to WSL" -ForegroundColor White
    Write-Host "  4. Run the Linux programming script" -ForegroundColor White
    Write-Host "  5. Detach and unbind devices when complete" -ForegroundColor White
    exit 0
}

# Colors for output
$Red = "Red"
$Green = "Green"
$Yellow = "Yellow"
$Blue = "Blue"
$Cyan = "Cyan"

function Write-ColorOutput {
    param([string]$Message, [string]$Color = "White")
    Write-Host $Message -ForegroundColor $Color
}

function Write-Info { param([string]$Message) Write-ColorOutput "[INFO] $Message" $Blue }
function Write-Success { param([string]$Message) Write-ColorOutput "[SUCCESS] $Message" $Green }
function Write-Warning { param([string]$Message) Write-ColorOutput "[WARNING] $Message" $Yellow }
function Write-Error { param([string]$Message) Write-ColorOutput "[ERROR] $Message" $Red }

# Check if running as administrator
function Test-Administrator {
    $currentUser = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($currentUser)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

# Check prerequisites
function Test-Prerequisites {
    Write-Info "Checking prerequisites..."
    
    # Check if WSL is available
    try {
        $wslVersion = & $WSLPath --version 2>$null
        if ($LASTEXITCODE -ne 0) {
            Write-Error "WSL not found. Please install WSL 2 first."
            Write-Info "Install with: wsl --install"
            return $false
        }
        Write-Success "WSL found"
    }
    catch {
        Write-Error "WSL not found. Please install WSL 2 first."
        Write-Info "Install with: wsl --install"
        return $false
    }
    
    # Check if script exists
    if (-not (Test-Path $ScriptPath)) {
        Write-Error "Linux script not found: $ScriptPath"
        Write-Info "Make sure the script is in the same directory as this PowerShell script"
        return $false
    }
    Write-Success "Linux script found: $ScriptPath"
    
    return $true
}

# Install usbipd-win if not present
function Install-Usbipd {
    Write-Info "Checking for usbipd-win..."
    
    try {
        $usbipdVersion = & usbipd --version 2>$null
        if ($LASTEXITCODE -eq 0) {
            Write-Success "usbipd-win already installed: $usbipdVersion"
            return $true
        }
    }
    catch {
        # usbipd not found, need to install
    }
    
    Write-Info "Installing usbipd-win..."
    
    # Check if winget is available
    try {
        $wingetVersion = & winget --version 2>$null
        if ($LASTEXITCODE -ne 0) {
            throw "winget not found"
        }
    }
    catch {
        Write-Error "winget not found. Please install Windows Package Manager first."
        Write-Info "Download from: https://github.com/microsoft/winget-cli/releases"
        return $false
    }
    
    # Install usbipd-win
    try {
        Write-Info "Installing usbipd-win via winget..."
        Write-Info "A installation dialogue may appear requiring manual completion. Please wait..."
        
        # Run winget with real-time output capture
        $process = Start-Process -FilePath "winget" -ArgumentList @(
            "install", 
            "--interactive", 
            "--exact", 
            "dorssel.usbipd-win", 
            "--accept-package-agreements", 
            "--accept-source-agreements"
        ) -PassThru -Wait -NoNewWindow -RedirectStandardOutput "winget_output.txt" -RedirectStandardError "winget_error.txt"
        
        # Display the output
        if (Test-Path "winget_output.txt") {
            $output = Get-Content "winget_output.txt" -Raw
            if ($output) {
                Write-Info "Winget output:"
                Write-Host $output -ForegroundColor Gray
            }
            Remove-Item "winget_output.txt" -ErrorAction SilentlyContinue
        }
        
        if (Test-Path "winget_error.txt") {
            $error = Get-Content "winget_error.txt" -Raw
            if ($error) {
                Write-Warning "Winget errors/warnings:"
                Write-Host $error -ForegroundColor Yellow
            }
            Remove-Item "winget_error.txt" -ErrorAction SilentlyContinue
        }
        
        if ($process.ExitCode -eq 0) {
            Write-Success "usbipd-win installed successfully"
            Write-Warning "For this script to work properly with your new usbipd install, you MUST relaunch PowerShell and try again. The script will now terminate."
            return $false
        } else {
            Write-Error "Failed to install usbipd-win (exit code: $($process.ExitCode))"
            return $false
        }
    }
    catch {
        Write-Error "Failed to install usbipd-win: $_"
        return $false
    }
}

# Detect Adafruit devices (VID 239A)
function Get-FeatherDevices {
    Write-Info "Detecting Adafruit devices (VID 239A)..."
    
    try {
        Write-Info "Running usbipd list..."
        $usbipdOutput = & usbipd list 2>&1
        Write-Info "usbipd list output:"
        Write-Host $usbipdOutput -ForegroundColor Gray
        
        # Parse usbipd output - it has "Connected:" and "Persisted:" sections
        $lines = $usbipdOutput -split "`n"
        $featherDevices = @()
        $inConnectedSection = $false
        
        foreach ($line in $lines) {
            $line = $line.Trim()
            
            # Check if we're in the Connected section
            if ($line -eq "Connected:") {
                $inConnectedSection = $true
                continue
            }
            
            # Check if we've moved to Persisted section
            if ($line -eq "Persisted:") {
                $inConnectedSection = $false
                continue
            }
            
            # Skip empty lines and header lines
            if ($line -eq "" -or $line -match "^BUSID\s+VID:PID\s+DEVICE\s+STATE$" -or $line -match "^GUID\s+DEVICE$") {
                continue
            }
            
            # Only process lines in the Connected section
            if ($inConnectedSection) {
                # Parse the line: BUSID VID:PID DEVICE STATE
                # Example: "1-3    0b05:19b6  USB Input Device                                              Not shared"
                if ($line -match "^\s*(\S+)\s+([0-9a-fA-F]{4}:[0-9a-fA-F]{4})\s+(.+?)\s+(\S+)$") {
                    $busid = $matches[1]
                    $vidpid = $matches[2]
                    $device = $matches[3].Trim()
                    $state = $matches[4]
                    
                    # Match any Adafruit device (VID 239A)
                    if ($vidpid -match "^239A:") {
                        $deviceObj = [PSCustomObject]@{
                            BUSID = $busid
                            'VID:PID' = $vidpid
                            DEVICE = $device
                            STATE = $state
                        }
                        $featherDevices += $deviceObj
                        Write-Info "Found Adafruit device: $device (BUSID: $busid, VID:PID: $vidpid)"
                    }
                }
            }
        }
        
        if ($featherDevices.Count -eq 0) {
            Write-Warning "No Adafruit devices detected"
            Write-Info "Make sure Adafruit devices are connected and drivers are installed"
            return @()
        }
        
        Write-Success "Found $($featherDevices.Count) Adafruit device(s)"
        return $featherDevices
    }
    catch {
        Write-Error "Failed to detect devices: $_"
        return @()
    }
}

# Bind devices to WSL
function Bind-DevicesToWSL {
    param([array]$Devices)
    
    Write-Info "Binding devices to WSL..."
    $boundDevices = @()
    
    foreach ($device in $Devices) {
        Write-Info "Binding device $($device.BUSID) to WSL..."
        try {
            # Bind the device
            Write-Info "Running: usbipd bind --busid $($device.BUSID)"
            $bindOutput = & usbipd bind --busid $device.BUSID 2>&1
            Write-Host $bindOutput -ForegroundColor Gray
            
            if ($LASTEXITCODE -eq 0) {
                Write-Info "Attaching device $($device.BUSID) to WSL..."
                Write-Info "Running: usbipd attach --wsl --busid $($device.BUSID)"
                # Attach the device to WSL
                $attachOutput = & usbipd attach --wsl --busid $device.BUSID 2>&1
                Write-Host $attachOutput -ForegroundColor Gray
                
                if ($LASTEXITCODE -eq 0) {
                    $boundDevices += $device
                    Write-Success "Successfully bound and attached device $($device.BUSID)"
                } else {
                    Write-Warning "Failed to attach device $($device.BUSID) to WSL"
                }
            } else {
                Write-Warning "Failed to bind device $($device.BUSID)"
            }
        }
        catch {
            Write-Warning "Failed to bind device $($device.BUSID): $_"
        }
    }
    
    if ($boundDevices.Count -eq 0) {
        Write-Error "No devices were successfully bound to WSL"
        return $false
    }
    
    Write-Success "Successfully bound and attached $($boundDevices.Count) device(s) to WSL"
    return $boundDevices
}

# Unbind devices from WSL
function Unbind-DevicesFromWSL {
    param([array]$Devices)
    
    Write-Info "Detaching and unbinding devices from WSL..."
    
    foreach ($device in $Devices) {
        Write-Info "Detaching device $($device.BUSID)..."
        try {
            # Detach the device from WSL first
            Write-Info "Running: usbipd detach --busid $($device.BUSID)"
            $detachOutput = & usbipd detach --busid $device.BUSID 2>&1
            Write-Host $detachOutput -ForegroundColor Gray
            
            if ($LASTEXITCODE -eq 0) {
                Write-Info "Unbinding device $($device.BUSID)..."
                Write-Info "Running: usbipd unbind --busid $($device.BUSID)"
                # Then unbind the device
                $unbindOutput = & usbipd unbind --busid $device.BUSID 2>&1
                Write-Host $unbindOutput -ForegroundColor Gray
                
                if ($LASTEXITCODE -eq 0) {
                    Write-Success "Successfully detached and unbound device $($device.BUSID)"
                } else {
                    Write-Warning "Failed to unbind device $($device.BUSID)"
                }
            } else {
                Write-Warning "Failed to detach device $($device.BUSID)"
            }
        }
        catch {
            Write-Warning "Failed to detach/unbind device $($device.BUSID): $_"
        }
    }
}

# Run the Linux programming script
function Invoke-LinuxScript {
    Write-Info "Running Linux programming script..."
    
    try {
        # Change to the script directory and run it
        $scriptDir = Split-Path -Parent $ScriptPath
        $scriptName = Split-Path -Leaf $ScriptPath
        
        Write-Info "Executing: $WSLPath bash -c 'cd $scriptDir && ./$scriptName'"
        Write-Info "Running Linux programming script..."
        
        # Run WSL command and capture output
        $wslCommand = "cd '$scriptDir' && ./$scriptName"
        $wslOutput = & $WSLPath bash -c $wslCommand 2>&1
        
        # Display the output
        Write-Host $wslOutput -ForegroundColor Cyan
        
        if ($LASTEXITCODE -eq 0) {
            Write-Success "Linux script completed successfully"
            return $true
        } else {
            Write-Error "Linux script failed with exit code $LASTEXITCODE"
            return $false
        }
    }
    catch {
        Write-Error "Failed to run Linux script: $_"
        return $false
    }
}

# Main execution
function Main {
    Write-ColorOutput "PowerShell Wrapper for Feather 32u4 Programming Script" $Cyan
    Write-ColorOutput "=====================================================" $Cyan
    Write-ColorOutput ""
    
    # Check if running as administrator
    if (-not (Test-Administrator)) {
        Write-Error "This script MUST be run as Administrator"
        Write-Info "Right-click PowerShell and select 'Run as Administrator'"
        Write-Info "Or run from Command Prompt as Administrator:"
        Write-Info "  powershell -ExecutionPolicy Bypass -File .\program_feather_boards_windows.ps1"
        Write-ColorOutput ""
        exit 1
    }
    
    # Check prerequisites
    if (-not (Test-Prerequisites)) {
        Write-Error "Prerequisites check failed. Exiting."
        exit 1
    }
    
    # Install usbipd-win if needed
    if (-not (Install-Usbipd)) {
        Write-Error "Failed to install usbipd-win or restart is required. Exiting."
        exit 1
    }
    
    # Detect Adafruit devices
    $devices = Get-FeatherDevices
    if ($devices.Count -eq 0) {
        Write-Error "No Adafruit devices found. Exiting."
        exit 1
    }
    
    # Warning about device binding
    Write-Warning "WARNING: This script will bind ALL connected Adafruit devices (VID 239A) to WSL."
    Write-Warning "These devices will lose their Windows functionality while bound."
    Write-Warning "Devices will be automatically unbound when the script completes."
    Write-ColorOutput ""
    Write-Info "Found $($devices.Count) Adafruit device(s) to bind:"
    foreach ($device in $devices) {
        Write-Info "  - $($device.DEVICE) (BUSID: $($device.BUSID), VID:PID: $($device.'VID:PID'))"
    }
    Write-ColorOutput ""
    Write-Info "Press Ctrl+C to cancel, or any key to continue..."
    $null = $Host.UI.RawUI.ReadKey("NoEcho,IncludeKeyDown")
    
    # Bind devices to WSL
    $boundDevices = Bind-DevicesToWSL $devices
    if (-not $boundDevices) {
        Write-Error "Failed to bind devices to WSL. Exiting."
        exit 1
    }
    
    Write-ColorOutput ""
    Write-Info "Devices bound to WSL. Starting programming process..."
    Write-ColorOutput ""
    
    # Run the Linux script
    $scriptSuccess = $false
    try {
        $scriptSuccess = Invoke-LinuxScript
    }
    finally {
        # Always try to unbind devices, even if script failed
        Write-ColorOutput ""
        Unbind-DevicesFromWSL $boundDevices
    }
    
    Write-ColorOutput ""
    if ($scriptSuccess) {
        Write-Success "Programming completed successfully!"
        exit 0
    } else {
        Write-Error "Programming failed. Check the output above for details."
        exit 1
    }
}

# Run main function
Main
