@echo off
TITLE Feather Wave Gauge Programmer
COLOR 0B

echo ===================================================
echo     Feather Wave Gauge Programmer
echo ===================================================
echo.
echo Initializing programmer... Please keep this window open.
echo.

:: Get the exact directory where this batch file is located
set "SCRIPT_DIR=%~dp0"
set "PS_SCRIPT=%SCRIPT_DIR%program_feather_boards_windows.ps1"

:: Verify the PowerShell script exists before running it
if not exist "%PS_SCRIPT%" (
    COLOR 0C
    echo ERROR: Cannot find program_feather_boards_windows.ps1
    echo Please make sure you extracted all files from the ZIP before running.
    echo.
    pause
    exit /b 1
)

:: Launch PowerShell, bypass the execution policy, run script
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%PS_SCRIPT%"

:: Catch the exit code from PowerShell
if %ERRORLEVEL% NEQ 0 (
    COLOR 0C
    echo.
    echo ---------------------------------------------------
    echo [ERROR] The programmer encountered a problem.
    echo Please review the log output above.
    echo ---------------------------------------------------
    pause
    exit /b %ERRORLEVEL%
)

COLOR 0A
echo.
echo ===================================================
echo     Programming Complete! You may close this window.
echo ===================================================
pause
