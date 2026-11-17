# UNO R4 WiFi OBD-II CAN display

This example shows engine speed, vehicle speed, coolant temperature, calculated
engine load, absolute throttle position, intake air temperature and fuel level
on an I2C HD44780-compatible 1602 LCD. It sends standard OBD-II Mode 01 requests only; it
does not clear trouble codes or change vehicle settings. It is an active CAN
device, not a passive monitor.

![Project Dashboard](images/3369039b-8758-479d-9019-2007c17d3b82.jfif)

## Compatibility and safety

The sketch supports **11-bit identifiers at 500 kbit/s** on the OBD-II CAN
pins (ISO 15765-4), with single-frame responses. It is not an ELM327 interface:
29-bit CAN, 250 kbit/s, multi-frame messages and non-CAN OBD protocols are not
implemented. Not all vehicles or ECUs support every PID, and gateways may block
requests. The first matching response to each request is used; different ECUs
can answer the same PID. `--` means a value has not arrived or is over 15
seconds old; it does not mean zero.

Test on a CAN bench or simulator first. Connect only when parked, never operate
the equipment while driving, and disconnect it if the vehicle reports faults.
Incorrect wiring or an incompatible transceiver can disrupt the vehicle network.
The R4's CAN controller **requires an external CAN transceiver**; do not connect
Arduino pins directly to CAN-H or CAN-L.

## Hardware and wiring

- Arduino UNO R4 WiFi, USB power, and a **5 V logic-compatible automotive
  high-speed CAN transceiver module** (for example, a TJA1050-based module,
  with TXD/RXD connected directly to the transceiver, not an MCP2515 SPI
  controller). Check the module's actual supply, pinout, logic levels and
  enable/standby settings against its datasheet.
- A 5 V HD44780-compatible 1602 LCD with a PCF8574 or MCP23008 I2C expander
  backpack supported by the `hd44780_I2Cexp` library.
- OBD-II male breakout/cable. Verify pin numbers on the connector: do not rely
  on the apparent orientation of a drawing.

| Connection | Arduino / other connection |
|---|---|
| Transceiver TXD | D10 (CAN TX) |
| Transceiver RXD | D13 (CAN RX) |
| Transceiver VCC / GND | 5V / GND |
| Transceiver CAN-H / CAN-L | OBD-II pin 6 / pin 14 |
| OBD-II signal ground, pin 5 | Arduino GND and transceiver GND |
| LCD backpack VCC / GND | 5V / GND |
| LCD backpack SDA / SCL | A4 (SDA) / A5 (SCL), or corresponding SDA / SCL header pins |

Use the main SDA/SCL pins, not the 3.3 V Qwiic connector. Check the backpack's
pin labels and supply requirements; adjust its contrast potentiometer if the
display is blank. The library automatically detects the LCD address and common
backpack pin mappings, so no `0x27` or `0x3F` address needs to be set in the
sketch. Connect only this LCD to the I2C bus during automatic detection, since
probing can interfere with other devices.

Use a properly regulated and protected power source. This example assumes USB
power and **does not connect OBD-II pin 16**, which carries vehicle battery
voltage. Never connect that pin to the Arduino 5 V rail. Use a protected
automotive-rated regulator if powering from the car, and avoid unsafe ground
connections. Turn off power when making connections.

The vehicle bus normally already has two 120-ohm termination resistors. This
device is a short stub: **disable/remove any termination resistor on the
transceiver module** when connecting to the car. A separate bench bus needs
correct termination at its two ends.

## Build and upload with Arduino CLI

From this directory in PowerShell, using the supplied Arduino CLI path:

```powershell
$cli = 'E:\arduino\cli\arduino-cli_1.5.2-rc.1_Windows_64bit\arduino-cli.exe'
& $cli core update-index
& $cli core install arduino:renesas_uno
& $cli lib install hd44780
& $cli board list
& $cli compile --fqbn arduino:renesas_uno:unor4wifi .\CAN_OBD_LCD
& $cli upload -p COM3 --fqbn arduino:renesas_uno:unor4wifi .\CAN_OBD_LCD
& $cli monitor -p COM3 -c baudrate=115200
```

Replace `COM3` with the port reported by `board list`. The board core supplies
`Arduino_CAN` and `Wire`; `hd44780` by Bill Perry is installed separately by
the command above.
If you use a different [Arduino CLI installation](https://arduino.github.io/arduino-cli/latest/installation/),
adjust `$cli` accordingly.

After installing the core and library, run the Windows batch file from
PowerShell to build, upload, and open the serial monitor:

```powershell
.\build_upload_monitor.bat COM3
```

Replace `COM3` with your board's port. If no port is supplied, the script lists
boards and prompts for one. Press Ctrl+C to exit the monitor. The script stops
if a step fails and does not install dependencies. It uses the CLI path above;
set the `ARDUINO_CLI` environment variable to the full executable path if your
installation is elsewhere.

If LCD initialization fails, an error code is printed to Serial and the sketch
stops before starting CAN. Check the I2C wiring, power, contrast and supported
backpack type; the library's `I2CexpDiag` example can help diagnose the display.

The two LCD rows automatically scroll down one value every second, wrapping
back to RPM after fuel level. Values are requested sequentially, one per second,
so each of the seven PIDs is requested approximately every seven seconds.
Temperatures are in Celsius; percentages are truncated to whole numbers.
A timeout is logged to Serial after 750 ms. CAN initialization
failure is shown on the LCD; transmit failures are logged and shown as
`CAN TX error`. A successful CAN enqueue does not prove that the ECU received or
answered the request. If values remain `--`, check ignition, wiring, transceiver
mode, vehicle protocol and PID support. Some vehicles only respond when the
engine is running; follow the vehicle and test equipment safety instructions.

Reference: [Arduino UNO R4 WiFi CAN tutorial](https://docs.arduino.cc/tutorials/uno-r4-wifi/can/).
