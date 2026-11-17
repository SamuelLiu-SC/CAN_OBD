@echo off
setlocal

if not defined ARDUINO_CLI set "ARDUINO_CLI=E:\arduino\cli\arduino-cli_1.5.2-rc.1_Windows_64bit\arduino-cli.exe"
set "FQBN=arduino:renesas_uno:unor4wifi"
set "SKETCH=%~dp0CAN_OBD_LCD"
set "PORT=%~1"

if not exist "%ARDUINO_CLI%" (
  echo Arduino CLI not found: "%ARDUINO_CLI%" 1>&2
  echo Set ARDUINO_CLI to the full path of arduino-cli.exe. 1>&2
  exit /b 1
)

if defined PORT goto build
echo Available boards:
call "%ARDUINO_CLI%" board list
if errorlevel 1 (
  echo Failed to list boards. 1>&2
  exit /b 1
)
set /p "PORT=Enter the Arduino COM port, for example COM3: "
if not defined PORT (
  echo No port supplied. 1>&2
  exit /b 1
)

:build
echo Building sketch...
call "%ARDUINO_CLI%" compile --fqbn "%FQBN%" "%SKETCH%"
if errorlevel 1 (
  echo Build failed; upload and monitor skipped. 1>&2
  exit /b 1
)

echo Uploading to %PORT%...
call "%ARDUINO_CLI%" upload --port "%PORT%" --fqbn "%FQBN%" "%SKETCH%"
if errorlevel 1 (
  echo Upload failed; monitor skipped. 1>&2
  exit /b 1
)

echo Opening serial monitor at 115200 baud. Press Ctrl+C to exit.
call "%ARDUINO_CLI%" monitor --port "%PORT%" --config baudrate=115200
if errorlevel 1 (
  echo Serial monitor failed. 1>&2
  exit /b 1
)
exit /b 0
