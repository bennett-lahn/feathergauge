# Multi-Feather 32u4 Programming Script (Windows Native)
#
# Compiles Arduino sketches and programs multiple Adafruit Feather 32u4 boards
# using arduino-cli for compilation and avrdude for flashing.
#
# arduino-cli and avrdude are installed automatically via winget if missing.
# All serial communication uses .NET System.IO.Ports.SerialPort directly.
#
# Caution: this script overwrites the current arduino-cli config settings.
#
# Usage: .\program_feather_boards_windows.ps1 [-SkipRtc] [-Debug] [-Help]
#        .\program_feather_boards_windows.ps1 -SketchDir <path> -OutputDir <path>

# Next Steps:
# 1. Use Start-Job to start a thread for each connected board (up to 5 at a time) that performs the complete programming process until all boards are programmed
# 2. Update logging so it works with concurrent jobs; can't flood the console with 5 programs simultaneously
# 3. Add small jitter to WMI queries to reduce load spikes
# 4. Update timer flashing so it happens exactly at the start of a new second?

# If 5 wave gauges overwhelms the USB bus, program with less (CLI flag to set number of threads)

param(
    # Override the main sketch directory (absolute path)
    [string]$SketchDir    = "",
    # Override the compiled-output directory (absolute path)
    [string]$OutputDir    = "",
    # Override the log file path (absolute path)
    [string]$LogFilePath  = "",
    # Skip RTC sync and EEPROM test; flash only the main sketch
    [switch]$SkipRtc,
    # Print verbose output to the console (all output is always logged regardless)
    [switch]$Debug,
    [switch]$Help
)

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

# Winget package IDs - update these if winget cannot locate a package
$WingetArduinoCli = "ArduinoSA.CLI"
$WingetAvrdude    = "AVRDudes.AVRDUDE"

# $ScriptDir  - the automatic_programming\ folder containing this script.
# $ProjectDir - the repository root one level above; holds Sketchbooks\ and libraries\.
$ScriptDir            = if ($PSScriptRoot) { $PSScriptRoot } else { $PWD.Path }
$ProjectDir           = Split-Path $ScriptDir -Parent

# Sketch source directories (relative to the project root)
$ArduinoSketchDir     = if ($SketchDir) { $SketchDir } else { Join-Path $ProjectDir "Sketchbooks\feathergauge_code" }
$RtcSetupSketchDir    = Join-Path $ProjectDir "Sketchbooks\rtc_setup"
$EepromTestSketchDir  = Join-Path $ProjectDir "Sketchbooks\eeprom_test"

# Local library zip files to install (see Install-LocalLibraries)
$LibraryConfigDir     = Join-Path $ProjectDir "libraries"

$BoardFqbn            = "adafruit:avr:feather32u4"

# Compiled binaries are written to a subdirectory inside automatic_programming\
$CompiledSketchDir    = if ($OutputDir) { $OutputDir } else { Join-Path $ScriptDir "compiled_sketches" }

# Log file is written inside automatic_programming\logs\
$LogsDir = Join-Path $ScriptDir "logs"
if (!(Test-Path $LogsDir)) { New-Item -ItemType Directory -Path $LogsDir | Out-Null }
$logFileName = "programming_log_$(Get-Date -Format 'yyyyMMdd_HHmmss').log"
$Script:LogFile = if ($LogFilePath) { $LogFilePath } else { Join-Path $LogsDir $logFileName }

# Seconds to wait for a serial response before timing out
$SerialTimeout        = 15
$SkipRtcSetup         = $SkipRtc.IsPresent
$Script:DebugMode     = $Debug.IsPresent

# Physical USB location path -> current COM port name.
# Populated by Build-UsbPortMap after initial port detection, and kept
# current by Wait-ForPortAtLocation as boards reset and re-enumerate.
$Script:UsbPortMap = @{}

# ---------------------------------------------------------------------------
# Logging helpers
#
# All messages are written to the log file regardless of -Debug.
# Write-Detail is for verbose/internal messages that only appear on the
# console when -Debug is passed. Write-Info/Success/Warn/Err always print.
# ---------------------------------------------------------------------------

function Write-LogLine {
    param([string]$Level, [string]$Message, [string]$Color, [switch]$DetailOnly)
    $line = "[$Level] $Message"
    Add-Content -Path $Script:LogFile -Value $line
    if (-not $DetailOnly -or $Script:DebugMode) {
        Write-Host $line -ForegroundColor $Color
    }
}

function Write-Info    { param([string]$m) Write-LogLine "INFO"    $m "Blue"   }
function Write-Success { param([string]$m) Write-LogLine "SUCCESS" $m "Green"  }
function Write-Warn    { param([string]$m) Write-LogLine "WARNING" $m "Yellow" }
function Write-Err     { param([string]$m) Write-LogLine "ERROR"   $m "Red"    }
function Write-Detail  { param([string]$m) Write-LogLine "INFO"    $m "Gray" -DetailOnly }

# ---------------------------------------------------------------------------
# Update-SessionPath
#
# Re-reads the Machine and User PATH environment variables into the current
# PowerShell session. Call this after a winget install so newly installed
# tools are immediately accessible without reopening the terminal.
# ---------------------------------------------------------------------------
function Update-SessionPath {
    $env:Path = [System.Environment]::GetEnvironmentVariable("Path", "Machine") + ";" +
                [System.Environment]::GetEnvironmentVariable("Path", "User")
}

# ---------------------------------------------------------------------------
# Install-WingetPackage
#
# Checks whether $Command is already on the PATH. If not, installs the winget
# package identified by $PackageId. Returns $true on success, $false on
# failure (missing winget, install error, or command still not found after).
# ---------------------------------------------------------------------------
function Install-WingetPackage {
    param(
        [string]$Command
       ,[string]$PackageId
       ,[string]$DisplayName
    )

    if (Get-Command $Command -ErrorAction SilentlyContinue) {
        Write-Success "$DisplayName already installed"
        return $true
    }

    Write-Info "Installing $DisplayName via winget..."

    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
        Write-Err "winget not found. Please install Windows Package Manager."
        Write-Info "Download from: https://github.com/microsoft/winget-cli/releases"
        return $false
    }

    $wingetOutput = winget install --id $PackageId --accept-package-agreements --accept-source-agreements --disable-interactivity 2>&1
    if ($Script:DebugMode) {
        $wingetOutput | ForEach-Object { Write-Detail "$_" }
    }

    Update-SessionPath

    if (Get-Command $Command -ErrorAction SilentlyContinue) {
        Write-Success "$DisplayName installed successfully"
        return $true
    }

    Write-Err "Failed to install $DisplayName. Restart PowerShell and try again, or install manually."
    return $false
}

# ---------------------------------------------------------------------------
# Install-Dependencies
#
# Entry point for dependency management. Installs arduino-cli and avrdude
# via winget. Returns $true only when both are available; any failure returns
# $false. Serial communication uses .NET System.IO.Ports (built in).
# ---------------------------------------------------------------------------
function Install-Dependencies {
    Write-Info "Checking and installing dependencies..."

    $ok = $true
    if (-not (Install-WingetPackage "arduino-cli" $WingetArduinoCli "arduino-cli")) { $ok = $false }
    if (-not (Install-WingetPackage "avrdude"     $WingetAvrdude    "avrdude"))     { $ok = $false }

    if ($ok) { Write-Success "All dependencies satisfied" }
    return $ok
}

# ---------------------------------------------------------------------------
# Install-CoreLibraries
#
# Installs standard Arduino libraries required by the feathergauge sketches
# using the arduino-cli library manager. Currently installs: SD.
# ---------------------------------------------------------------------------
function Install-CoreLibraries {
    Write-Detail "Ensuring core libraries are installed (SD)..."
    foreach ($lib in @("SD")) {
        arduino-cli lib install $lib 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) {
            Write-Detail "Library installed: $lib"
        } else {
            Write-Warn "Failed to install library: $lib"
        }
    }
}

# ---------------------------------------------------------------------------
# Install-LocalLibraries
#
# Installs custom libraries from the project's libraries\ directory by
# passing each .zip archive to `arduino-cli lib install --zip-path`.
# Only .zip files are supported. Place vendor or modified library zips in
# libraries\ rather than relying on the public registry, so that specific
# known-good versions are always used regardless of upstream changes.
# ---------------------------------------------------------------------------
function Install-LocalLibraries {
    Write-Detail "Installing local library zip files from: $LibraryConfigDir"

    if (-not (Test-Path $LibraryConfigDir)) {
        Write-Warn "libraries\ directory not found: $LibraryConfigDir"
        Write-Detail "Create the directory and add .zip library archives to enable local installs"
        return
    }

    $zips = Get-ChildItem -Path $LibraryConfigDir -Filter "*.zip" -Recurse -ErrorAction SilentlyContinue

    if ($zips.Count -eq 0) {
        Write-Warn "No .zip library files found in $LibraryConfigDir"
        return
    }

    Write-Detail "Found $($zips.Count) library zip(s) to install"

    foreach ($zip in $zips) {
        Write-Detail "Installing library: $($zip.Name)"
        arduino-cli lib install --zip-path $zip.FullName 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) {
            Write-Detail "Installed: $($zip.Name)"
        } else {
            Write-Warn "Failed to install: $($zip.Name)"
        }
    }
}

# ---------------------------------------------------------------------------
# Setup-ArduinoCli
#
# Configures arduino-cli for this project:
#   - Adds the Adafruit board manager URL so adafruit:avr can be found
#   - Enables "unsafe" (zip) library installs
#   - Installs the arduino:avr core (required dependency of adafruit:avr)
#   - Installs the adafruit:avr core
#   - Installs core and local libraries
# ---------------------------------------------------------------------------
function Setup-ArduinoCli {
    Write-Info "Setting up Arduino CLI..."

    $configFile = Join-Path $env:APPDATA "Arduino15\arduino-cli.yaml"
    if (-not (Test-Path $configFile)) {
        arduino-cli config init --overwrite
    }

    arduino-cli config set board_manager.additional_urls https://adafruit.github.io/arduino-board-index/package_adafruit_index.json 2>&1 | Out-Null
    arduino-cli config set library.enable_unsafe_install true 2>&1 | Out-Null
    $indexOutput = arduino-cli core update-index 2>&1
    if ($Script:DebugMode) { $indexOutput | ForEach-Object { Write-Detail "$_" } }
    arduino-cli core install arduino:avr 2>&1 | Out-Null   # dependency of adafruit:avr
    $coreOutput = arduino-cli core install adafruit:avr 2>&1
    if ($Script:DebugMode) { $coreOutput | ForEach-Object { Write-Detail "$_" } }

    Install-CoreLibraries
    Install-LocalLibraries

    Write-Success "Arduino CLI setup complete"
}

# ---------------------------------------------------------------------------
# New-DirectoryStructure
#
# Creates directories that must exist before compilation or programming:
#   - compiled_sketches\  inside automatic_programming\ for build output
#   - libraries\          inside the main sketch folder (arduino-cli expects it)
# ---------------------------------------------------------------------------
function New-DirectoryStructure {
    Write-Detail "Creating directory structure..."
    New-Item -ItemType Directory -Path $CompiledSketchDir -Force | Out-Null
    New-Item -ItemType Directory -Path (Join-Path $ArduinoSketchDir "libraries") -Force | Out-Null
    Write-Detail "Directory structure created"
}

# ---------------------------------------------------------------------------
# Invoke-CompileSketch
#
# Compiles a single Arduino sketch with arduino-cli and writes the output
# (.hex and supporting files) to $CompiledSketchDir. Verifies the sketch
# file exists before attempting compilation and reports success or failure.
#
# Parameters:
#   $SketchPath - directory containing the .ino file
#   $SketchFile - filename of the .ino (used only for existence check and log)
#   $Label      - human-readable name used in log messages (e.g. "RTC setup")
# ---------------------------------------------------------------------------
function Invoke-CompileSketch {
    param(
        [string]$SketchPath
       ,[string]$SketchFile
       ,[string]$Label
    )

    Write-Info "Compiling $Label sketch..."

    if (-not (Test-Path (Join-Path $SketchPath $SketchFile))) {
        Write-Err "$Label sketch file not found: $(Join-Path $SketchPath $SketchFile)"
        return $false
    }

    $compileOutput = arduino-cli compile --fqbn $BoardFqbn --output-dir $CompiledSketchDir $SketchPath 2>&1
    if ($Script:DebugMode) {
        $compileOutput | ForEach-Object { Write-Detail "$_" }
    }

    if ($LASTEXITCODE -eq 0) {
        Write-Success "$Label sketch compiled successfully"
        Write-Detail "Compiled binary: $(Join-Path $CompiledSketchDir "$SketchFile.hex")"
        return $true
    }

    Write-Err "$Label sketch compilation failed"
    return $false
}

# ---------------------------------------------------------------------------
# Build-UsbPortMap
#
# Scans all PnP serial devices whose hardware ID contains Adafruit's vendor
# ID (VID_239A) and records each one's DEVPKEY_Device_LocationPaths value
# alongside its current COM port name. The location path encodes the full
# USB hub topology (e.g. "PCIROOT(0)#PCI(1400)#USB(3)"), which is stable
# across re-enumerations: the same physical socket always yields the same
# path even if the board resets and receives a new COM port number. No
# external tool is needed; all information comes directly from the PnP
# subsystem. Returns the populated hashtable (locationPath -> comPort).
# ---------------------------------------------------------------------------
function Build-UsbPortMap {
    Write-Detail "Scanning PnP for Adafruit devices (VID 239A)..."
    $map = @{}

    $devices = Get-PnpDevice -Class "Ports" -Status "OK" -ErrorAction SilentlyContinue |
        Where-Object { $_.HardwareID -match "VID_239A" }

    foreach ($dev in $devices) {
        if (-not ($dev.FriendlyName -match '\((COM\d+)\)')) { continue }
        $comPort = $Matches[1]

        # DEVPKEY_Device_LocationPaths GUID: {a45c254e-df1c-4efd-8020-67d146a850e0} 37
        $locProp = Get-PnpDeviceProperty -InstanceId $dev.InstanceId `
                       -KeyName "DEVPKEY_Device_LocationPaths" `
                       -ErrorAction SilentlyContinue

        if ($locProp -and $locProp.Data) {
            $locationPath = $locProp.Data[0]
            # Remove composite device suffix from location path if it exists
            $locationPath = $locationPath -replace '#USBMI\(\d+\)', ''
            $map[$locationPath] = $comPort
            Write-Detail "  $comPort -> $locationPath"
        } else {
            Write-Warn "Could not read location path for $comPort - it will not be trackable after reset"
        }
    }

    Write-Success "Found $($map.Count) Adafruit device(s)"
    return $map
}

# ---------------------------------------------------------------------------
# Get-DeviceProductId
#
# Returns the USB product ID (PID) string for the Adafruit device currently
# mapped to $Port, extracted from the device's HardwareID (e.g. "800C").
# Returns $null if the device is not found or has no parseable PID field.
# ---------------------------------------------------------------------------
function Get-DeviceProductId {
    param([string]$Port)

    $device = Get-PnpDevice -Class "Ports" -Status "OK" -ErrorAction SilentlyContinue |
        Where-Object { $_.HardwareID -match "VID_239A" -and $_.FriendlyName -match "\($Port\)" } |
        Select-Object -First 1

    if (-not $device) { return $null }

    $hwIdWithPid = $device.HardwareID |
        Where-Object { $_ -match "PID_([0-9A-Fa-f]+)" } |
        Select-Object -First 1

    if ($hwIdWithPid -and $hwIdWithPid -match "PID_([0-9A-Fa-f]+)") {
        return $Matches[1].ToUpper()
    }
    return $null
}

# ---------------------------------------------------------------------------
# Invoke-BootloaderReset
#
# Opens the COM port at 1200 baud using .NET System.IO.Ports.SerialPort,
# asserts DTR low, then closes the port. This is the standard Caterina
# bootloader reset sequence for the ATmega32u4: the running sketch detects
# the 1200-baud connection followed by DTR going low and reboots into
# bootloader mode. The board will briefly disappear from the system before
# re-enumerating as a bootloader device. Returns $true on success.
# ---------------------------------------------------------------------------
function Invoke-BootloaderReset {
    param([string]$Port)

    Write-Detail "Triggering 1200-baud bootloader reset on $Port..."
    try {
        Write-Detail "  Opening SerialPort: [$Port] at 1200 baud"
        $sp = New-Object System.IO.Ports.SerialPort($Port, 1200)
        $sp.Open()
        $sp.DtrEnable = $false   # DTR low signals the sketch to enter bootloader
        Start-Sleep -Milliseconds 50
        $sp.Close()
        $sp.Dispose()
        return $true
    } catch {
        Write-Err "Failed to trigger reset on ${Port}: $_"
        return $false
    }
}

function Wait-ForPortAtLocation {
    param(
        [Parameter(Mandatory=$true)]
        [string]$TargetLocationPath,

        # When set, any device at the target location whose PID matches this
        # value is skipped. Pass the PID captured before the triggering reset
        # so that the function ignores the device that was already present and
        # only returns once a genuinely new enumeration has appeared.
        [string]$ExcludeProductId = $null,

        [int]$TimeoutSeconds = 5,
        [int]$PollIntervalMs = 500
    )
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

    while ($stopwatch.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        
        # Only query ports that have enumerated successfully
        $devices = Get-PnpDevice -Class "Ports" -Status "OK" -ErrorAction SilentlyContinue |
            Where-Object { $_.HardwareID -match "VID_239A" }

        foreach ($dev in $devices) {
            $locProp = Get-PnpDeviceProperty -InstanceId $dev.InstanceId `
                            -KeyName "DEVPKEY_Device_LocationPaths" `
                            -ErrorAction SilentlyContinue

            if ($locProp -and $locProp.Data) {
                $matchFound = $false
                # Sanitize the OS paths before comparing
                foreach ($rawPath in $locProp.Data) {
                    $cleanPath = $rawPath -replace '#USBMI\(\d+\)', ''
                    if ($cleanPath -eq $TargetLocationPath) {
                        $matchFound = $true
                        break
                    }
                }
                if ($matchFound) {
                    if ($dev.FriendlyName -match '\((COM\d+)\)') {
                        $comPort = $Matches[1]

                        # If a pre-reset PID was supplied, skip the device if
                        # its PID has not changed - it has not yet re-enumerated
                        # as a different device.
                        if ($ExcludeProductId) {
                            $hwIdWithPid = $dev.HardwareID |
                                Where-Object { $_ -match "PID_([0-9A-Fa-f]+)" } |
                                Select-Object -First 1
                            if ($hwIdWithPid -and $hwIdWithPid -match "PID_([0-9A-Fa-f]+)") {
                                $devPid = $Matches[1].ToUpper()
                                if ($devPid -eq $ExcludeProductId.ToUpper()) { continue }
                            }
                        }

                        # Update the global map so the new port is tracked
                        $Script:UsbPortMap[$TargetLocationPath] = $comPort
                        $stopwatch.Stop()
                        return $comPort
                    }
                }
            }
        }

        # Device not found yet, sleep for the poll interval
        Start-Sleep -Milliseconds $PollIntervalMs
    }

    $stopwatch.Stop()
    Write-Warning "No Adafruit device found at location path '$TargetLocationPath' after $($TimeoutSeconds)s."
    return $null
}

# ---------------------------------------------------------------------------
# Invoke-ProgramBoard
#
# Flashes a compiled .hex file onto a single board in three steps:
#
#   1. Retrieve the board's physical location path from $Script:UsbPortMap so
#      it can be found again after reset regardless of COM port changes.
#   2. Trigger the 1200-baud Caterina bootloader reset (Invoke-BootloaderReset)
#      and wait for the bootloader to enumerate at the same location, ignoring
#      the original sketch port (Wait-ForPortAtLocation).
#   3. Flash via avrdude at 57600 baud on the bootloader port. After avrdude
#      completes the board resets into sketch mode; wait for it to re-enumerate
#      (ignoring the bootloader port) and update $Script:UsbPortMap.
#
# Returns the new sketch-mode COM port on success, or $null on any failure.
# Callers should update their $Port variable with the returned value so that
# subsequent serial communication uses the correct port.
#
# Parameters:
#   $Port     - COM port of the running firmware (e.g. "COM9")
#   $HexFile  - absolute path to the compiled .hex file
#   $BoardNum - 1-based index used in log messages
# ---------------------------------------------------------------------------
function Invoke-ProgramBoard {
    param(
        [string]$Port
       ,[string]$HexFile
       ,[int]   $BoardNum
    )

    Write-Detail "Programming board $BoardNum on $Port..."

    # --- Step 1: resolve physical location so we can track the board after reset ---
    $locationPath = $Script:UsbPortMap.Keys |
        Where-Object { $Script:UsbPortMap[$_] -eq $Port } |
        Select-Object -First 1

    if (-not $locationPath) {
        Write-Err "No location path in port map for $Port - cannot track board $BoardNum after reset"
        return $null
    }

    # --- Step 2: trigger reset and wait for bootloader port ---
    # Save the sketch-mode PID now so Wait-ForPortAtLocation can reject the
    # device if it has not yet transitioned to a different (bootloader) PID.
    $sketchPid = Get-DeviceProductId $Port
    if ($sketchPid) { Write-Detail "  Sketch product ID (pre-reset): $sketchPid" }

    if (-not (Invoke-BootloaderReset $Port)) { return $null }

    Write-Detail "Waiting for bootloader to enumerate at location $locationPath..."
    $bootloaderPort = Wait-ForPortAtLocation -TargetLocationPath $locationPath `
                          -ExcludeProductId $sketchPid
    if (-not $bootloaderPort) {
        Write-Err "Bootloader did not enumerate for board $BoardNum"
        return $null
    }
    Write-Detail "Bootloader detected on $bootloaderPort - flashing..."

    # Save the bootloader PID so the post-flash wait can reject the bootloader
    # device and only return once the sketch-mode device has enumerated.
    $bootloaderPid = Get-DeviceProductId $bootloaderPort
    if ($bootloaderPid) { Write-Detail "  Bootloader product ID: $bootloaderPid" }

    # --- Step 3: flash via avrdude ---
    # -C points avrdude at its own config so programmer definitions are always found
    # \\.\COMx notation is required for COM10+ on Windows
    $avrdudeConf = Join-Path $ScriptDir "avrdude.conf"
    $avrOutput = avrdude -p atmega32u4 -c avr109 -P "\\.\$bootloaderPort" -b 57600 -D -U "flash:w:${HexFile}:i" 2>&1
    if ($Script:DebugMode) {
        $avrOutput | ForEach-Object { Write-Detail "$_" }
    }

    if ($LASTEXITCODE -ne 0) {
        Write-Err "avrdude failed for board $BoardNum"
        return $null
    }

    # Wait for the sketch to enumerate (board resets after programming)
    Write-Detail "Waiting for sketch to enumerate at location $locationPath..."
    Start-Sleep -Seconds 2    # give time for arduino to exit bootloader and enumerate
    $sketchPort = Wait-ForPortAtLocation -TargetLocationPath $locationPath `
                      -ExcludeProductId $bootloaderPid
    if (-not $sketchPort) {
        Write-Warn "Board $BoardNum did not re-enumerate after flash - serial comms may fail"
        $sketchPort = $bootloaderPort   # best-effort fallback
    }
    Write-Success "Board $BoardNum programmed successfully - now on $sketchPort"
    return $sketchPort
}

# ---------------------------------------------------------------------------
# Send-TimeViaSerial
#
# Opens a serial connection to $Port at 57600 baud and sends the time string
# (formatted "yyyy-MM-dd HH:mm:ss") to the rtc_setup sketch running on the
# board. Waits up to $SerialTimeout seconds for the board to respond with
# "SUCCESS" or "ERROR". Returns $true on a SUCCESS acknowledgement.
# ---------------------------------------------------------------------------
function Send-TimeViaSerial {
    param([string]$Port)

    Write-Detail "Sending time via serial to $Port..."

    try {
        Write-Detail "  Opening SerialPort: [$Port] at 57600 baud"
        $sp = New-Object System.IO.Ports.SerialPort($Port, 57600)
        $sp.NewLine      = "`n"
        $sp.ReadTimeout  = 500
        $sp.WriteTimeout = 2000

        $sp.DtrEnable = $true

        $sp.Open()
        
        # Wait for the Arduino to finish booting and dump its initial prints
        Start-Sleep -Seconds 2    

        # Drain the buffer of any startup messages
        $startupText = $sp.ReadExisting()
        if ($startupText) {
            $cleanText = $startupText.Trim() -replace '`r?`n', ' | '
            Write-Detail "[ARDUINO STARTUP] $cleanText"
        }

        # Sleep until start of next second (precision: +/- 15ms)
        $now = Get-Date
        $msRemaining = 1000 - $now.Millisecond
        Start-Sleep -Milliseconds $msRemaining

        $currentTime = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
        $currentTimeMs = Get-Date -Format "yyyy-MM-dd HH:mm:ss.fff"
        Write-Detail "Current system time (w/ ms): $currentTimeMs"

        $bytes = [System.Text.Encoding]::ASCII.GetBytes($currentTime + "`n")
        $sp.Write($bytes, 0, $bytes.Length)

        $deadline = (Get-Date).AddSeconds(15) # Assuming 15s global timeout
        
        while ((Get-Date) -lt $deadline) {
            try {
                $line = $sp.ReadLine().Trim()
                Write-Detail "[ARDUINO] $line"

                if ($line -match '^SUCCESS') { 
                    $sp.Close(); $sp.Dispose()
                    return $true  
                }
                if ($line -match '^ERROR') { 
                    $sp.Close(); $sp.Dispose()
                    return $false 
                }
            } catch [System.TimeoutException] { 
                # Expected behavior if waiting for the board to process
            }
        }

        Write-Warn "Timeout waiting for response from $Port"
        $sp.Close(); $sp.Dispose()
        return $false
        
    } catch {
        Write-Err "Serial communication error on ${Port}: $_"
        if ($null -ne $sp -and $sp.IsOpen) {
            $sp.Close(); $sp.Dispose()
        }
        return $false
    }
}

# ---------------------------------------------------------------------------
# Read-AndValidateEepromSerial
#
# Reads serial output from the eeprom_test sketch and looks for the line
# "Read serial number from EEPROM: <value>". Validates that the value
# contains at least one alphanumeric character (i.e. it is not blank or
# filled with 0xFF from an unwritten EEPROM). Returns $true if a valid
# serial number is found within $SerialTimeout seconds.
# ---------------------------------------------------------------------------
function Read-AndValidateEepromSerial {
    param([string]$Port)

    Write-Detail "Reading EEPROM serial value from $Port"

    $prefix = "Read serial number from EEPROM: "

    try {
        Write-Detail "  Opening SerialPort: [$Port] at 57600 baud"
        $sp = New-Object System.IO.Ports.SerialPort($Port, 57600)
        $sp.NewLine     = "`n"
        $sp.ReadTimeout = 500
        $sp.Open()
        Start-Sleep -Seconds 2    # wait for Arduino to finish boot/init

        $eepromValue = $null
        $deadline    = (Get-Date).AddSeconds($SerialTimeout)

        while ((Get-Date) -lt $deadline) {
            try {
                $line = $sp.ReadLine().Trim()
                Write-Detail "[ARDUINO] $line"
                if ($line.StartsWith($prefix)) {
                    $eepromValue = $line.Substring($prefix.Length).Trim()
                    break
                }
            } catch [System.TimeoutException] { }
        }

        $sp.Close(); $sp.Dispose()

        if (-not $eepromValue) {
            Write-Err "No EEPROM value read from $Port"
            return $false
        }
        if ($eepromValue -match '^[1-9][0-9]*$') {
            Write-Success "EEPROM value detected: $eepromValue"
            return $true
        }
        Write-Err "EEPROM value appears uninitialized: $eepromValue"
        return $false
    } catch {
        Write-Err "Serial communication error on ${Port}: $_"
        return $false
    }
}

# ---------------------------------------------------------------------------
# Test-EepromOnBoard
#
# Stage 0 of the programming sequence. Flashes the eeprom_test sketch onto
# the board and reads back the stored serial number over USB serial. A board
# that has never been configured will have an uninitialized EEPROM and will
# fail this test, preventing it from proceeding to RTC setup or main flash.
# ---------------------------------------------------------------------------
function Test-EepromOnBoard {
    param([string]$Port, [int]$BoardNum)

    $location = $Script:UsbPortMap.GetEnumerator() | Where-Object { $_.Value -eq $Port } | Select-Object -ExpandProperty Name
    Write-Detail "Testing EEPROM on board $BoardNum on $Port (location: $location)..."

    $eepromHex  = Join-Path $CompiledSketchDir "eeprom_test.ino.hex"
    $currentPort = Invoke-ProgramBoard $Port $eepromHex $BoardNum
    if (-not $currentPort) {
        Write-Err "Failed to program EEPROM test sketch on board $BoardNum"
        return $null
    }

    if (Read-AndValidateEepromSerial $currentPort) {
        Write-Success "EEPROM appears initialized on board $BoardNum"
        return $currentPort
    }

    Write-Err "EEPROM not initialized or unreadable on board $BoardNum"
    return $null
}

# ---------------------------------------------------------------------------
# Setup-RtcBoard
#
# Stage 1 of the programming sequence. Flashes the rtc_setup sketch, waits
# for the board to boot, then sends the current Windows system time over
# serial so the sketch can write it to the onboard RTC. The board must
# acknowledge with "SUCCESS" within $SerialTimeout seconds.
# ---------------------------------------------------------------------------
function Setup-RtcBoard {
    param([string]$Port, [int]$BoardNum)

    Write-Detail "Setting up RTC on board $BoardNum on $Port..."

    $rtcHex      = Join-Path $CompiledSketchDir "rtc_setup.ino.hex"
    $currentPort = Invoke-ProgramBoard $Port $rtcHex $BoardNum
    if (-not $currentPort) {
        Write-Err "Failed to program RTC setup sketch on board $BoardNum"
        return $null
    }

    if (-not (Send-TimeViaSerial $currentPort)) {
        Write-Err "Failed to set RTC time on board $BoardNum"
        return $null
    }

    Write-Success "RTC setup completed for board $BoardNum"
    return $currentPort
}

# ---------------------------------------------------------------------------
# Invoke-ProgramBoards
#
# Programs all detected boards. When $SkipRtcSetup is $false (default), runs
# the full three-stage sequence per board:
#   Stage 0 - EEPROM test: verifies the board has an initialized serial number
#   Stage 1 - RTC sync:    sets the onboard RTC to current system time
#   Stage 2 - Main flash:  writes feathergauge_code firmware
# When $SkipRtcSetup is $true, only Stage 2 runs on each board.
# Returns $true only when every board completes its required stages.
# ---------------------------------------------------------------------------
function Invoke-ProgramBoards {
    param([string[]]$Ports)

    $mainHex      = Join-Path $CompiledSketchDir "feathergauge_code.ino.hex"
    $successCount = 0
    $totalCount   = $Ports.Count

    if ($SkipRtcSetup) {
        Write-Info "Starting simple programming for $totalCount board(s) (RTC/EEPROM skipped)..."
    } else {
        Write-Info "Starting three-stage programming for $totalCount board(s)..."
        Write-Detail "Stage 0: EEPROM test  |  Stage 1: RTC setup  |  Stage 2: Main program"
    }

    for ($i = 0; $i -lt $Ports.Count; $i++) {
        $port     = $Ports[$i]
        $boardNum = $i + 1

        Write-Info "Processing board $boardNum of $totalCount on $port"

        if (-not $SkipRtcSetup) {
            Write-Info "Stage 0: Testing EEPROM on board $boardNum"
            $port = Test-EepromOnBoard $port $boardNum
            if (-not $port) {
                Write-Warn "Skipping board $boardNum due to EEPROM test failure"
                continue
            }

            Write-Info "Stage 1: Setting up RTC on board $boardNum"
            $port = Setup-RtcBoard $port $boardNum
            if (-not $port) {
                Write-Err "RTC setup failed for board $boardNum - skipping main program"
                continue
            }
        }

        Write-Info "Stage 2: Programming main sketch on board $boardNum"
        $port = Invoke-ProgramBoard $port $mainHex $boardNum
        if ($port) {
            $successCount++
            Write-Success "Board $boardNum completed successfully"
        } else {
            Write-Warn "Main program failed for board $boardNum - continuing with next board..."
        }
    }

    Write-Info "Programming complete: $successCount/$totalCount boards programmed successfully"

    if ($successCount -eq $totalCount) {
        Write-Success "All boards programmed successfully!"
        return $true
    }

    Write-Warn "Some boards failed. Check the log for details."
    return $false
}

# ---------------------------------------------------------------------------
# Show-Usage
#
# Prints parameter documentation and a description of the expected project
# file layout to the console. Called when -Help is passed.
# ---------------------------------------------------------------------------
function Show-Usage {
    Write-Host ""
    Write-Host "Usage: .\program_feather_boards_windows.ps1 [OPTIONS]" -ForegroundColor Yellow
    Write-Host "WARNING: Always unplug all adafruit devices except wave gauges before running script" -ForegroundColor Red
    Write-Host ""
    Write-Host "Options:" -ForegroundColor Yellow
    Write-Host "  -Help                    Show this help message" -ForegroundColor White
    Write-Host "  -SketchDir  <path>       Main sketch directory (default: Sketchbooks\feathergauge_code)" -ForegroundColor White
    Write-Host "  -OutputDir  <path>       Compiled binary output directory (default: automatic_programming\compiled_sketches)" -ForegroundColor White
    Write-Host "  -LogFilePath <path>      Log file path (default: automatic_programming\programming_log_<timestamp>.log)" -ForegroundColor White
    Write-Host "  -SkipRtc                 Skip EEPROM test and RTC sync; flash main sketch only" -ForegroundColor White
    Write-Host "  -Debug                   Print verbose output to the console (full output is always logged)" -ForegroundColor White
    Write-Host ""
    Write-Host "Expected project layout (relative to the repository root):" -ForegroundColor Yellow
    Write-Host "  Sketchbooks\feathergauge_code\feathergauge_code.ino  (main firmware)" -ForegroundColor White
    Write-Host "  Sketchbooks\rtc_setup\rtc_setup.ino                  (RTC sync sketch)" -ForegroundColor White
    Write-Host "  Sketchbooks\eeprom_test\eeprom_test.ino              (EEPROM validation sketch)" -ForegroundColor White
    Write-Host "  libraries\*.zip                                       (custom library archives)" -ForegroundColor White
    Write-Host ""
    Write-Host "The script will:" -ForegroundColor Yellow
    Write-Host "  1. Install arduino-cli, avrdude, Python via winget (if missing)" -ForegroundColor White
    Write-Host "  2. Install pyserial via pip (if missing)" -ForegroundColor White
    Write-Host "  3. Configure Arduino CLI and install board cores and libraries" -ForegroundColor White
    Write-Host "  4. Compile EEPROM test, RTC setup, and main sketches" -ForegroundColor White
    Write-Host "  5. Detect connected Feather 32u4 boards via arduino-cli board list" -ForegroundColor White
    Write-Host "  6. For each board run three stages:" -ForegroundColor White
    Write-Host "       Stage 0 - Flash eeprom_test and verify the stored serial number" -ForegroundColor White
    Write-Host "       Stage 1 - Flash rtc_setup and synchronize the RTC to system time" -ForegroundColor White
    Write-Host "       Stage 2 - Flash the main feathergauge_code firmware" -ForegroundColor White
    Write-Host "  Use -SkipRtc to bypass stages 0 and 1." -ForegroundColor White
    Write-Host ""
    Write-Host "CAUTION: This script overwrites the current arduino-cli config settings." -ForegroundColor Yellow
    Write-Host ""
}

# ---------------------------------------------------------------------------
# Main
#
# Top-level entry point. Runs dependency installation, Arduino CLI setup,
# directory creation, sketch compilation, board detection, and programming
# in sequence. Exits with code 0 on full success, 1 on any failure.
# ---------------------------------------------------------------------------
function Main {
    if ($Help) {
        Show-Usage
        exit 0
    }

    Write-Host "Feather 32u4 Multi-Board Programming Script (Windows)" -ForegroundColor Blue
    Write-Host "======================================================" -ForegroundColor Blue
    Write-Info "Log file: $($Script:LogFile)"

    if (-not (Install-Dependencies)) {
        Write-Err "Dependency installation failed. Exiting."
        exit 1
    }

    Setup-ArduinoCli

    New-DirectoryStructure

    if ($SkipRtcSetup) {
        Write-Info "Skipping RTC setup - compiling main sketch only"
        if (-not (Invoke-CompileSketch $ArduinoSketchDir "feathergauge_code.ino" "main")) { exit 1 }
    } else {
        Write-Detail "Three-stage programming enabled - compiling EEPROM test, RTC setup, and main sketches"
        if (-not (Invoke-CompileSketch $EepromTestSketchDir "eeprom_test.ino"       "EEPROM test")) { exit 1 }
        if (-not (Invoke-CompileSketch $RtcSetupSketchDir   "rtc_setup.ino"         "RTC setup"))   { exit 1 }
        if (-not (Invoke-CompileSketch $ArduinoSketchDir    "feathergauge_code.ino" "main"))        { exit 1 }
    }

    # Discover all Adafruit (VID 239A) devices and build the location->COM map
    # in one PnP scan; no external tool needed
    $Script:UsbPortMap = Build-UsbPortMap
    if ($Script:UsbPortMap.Count -eq 0) {
        Write-Err "No Adafruit devices (VID 239A) found. Exiting."
        exit 1
    }

    $success = Invoke-ProgramBoards @($Script:UsbPortMap.Values)

    if ($success) {
        Write-Success "All operations completed successfully!"
        exit 0
    } else {
        Write-Err "Some operations failed. Check the log for details."
        exit 1
    }
}

Main
