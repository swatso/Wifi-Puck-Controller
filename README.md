# Wi-Fi Puck Controller

An ESP8266 Wemos D1 mini Pro on a battery-powered moving puck controls two
SG90 servos that raise and lower magnets beneath a diorama. MQTT commands
move both servos smoothly between their configured positions over two seconds.
The servos start at their off positions before the active-high servo power
output is enabled. The onboard LED is on while Wi-Fi is connected.
The active-high ALIVE output on D6 (GPIO12) is asserted at the beginning of
firmware setup to keep the device powered.

## Configuration

Edit the pin and pulse-width `#define`s at the top of `src/main.cpp` for your
wiring and mechanism. The supplied D1, D2 and D5 assignments and 1000/2000 µs
positions are placeholders; check the travel limits before connecting the
mechanism. The ESP8266 Servo library accepts pulse widths from 200 to 3000 µs;
choose safe values for your servos and mechanism. The servos and their power
switch need a suitable supply and common ground with the ESP8266.

Create these local, plaintext files in the project's `data` directory:

* `host.txt`: a unique Wi-Fi hostname, also used as the MQTT client ID.
* `ssid.txt`: Wi-Fi network name.
* `pass.txt`: Wi-Fi password (an empty file for an open network).

Use one value per file. A trailing newline is removed; other spaces are
preserved. These files are ignored by Git and must be uploaded to LittleFS
separately from the firmware. Missing or invalid files prevent Wi-Fi connection
until corrected and the device restarted. Hostnames and SSIDs must not be empty
and are limited to 32 bytes; passwords are limited to 64 bytes.

The controller connects to the MQTT broker at `192.168.0.2:1883` and subscribes
to `track/turnout/node2B/Attach`. A payload beginning with uppercase `T` raises
both magnets; any other payload, including an empty one, lowers them.
It also subscribes to `track/turnout/node2B/KeepAlive`. Only a payload beginning
with uppercase `T` resets the five-minute keep-alive timer. The timer starts at
startup and continues even if Wi-Fi, MQTT, or configuration is unavailable.
If it expires, ALIVE goes low and remains low until the device restarts.
Publish keep-alives without the MQTT retain flag: a retained message could
otherwise reset the timer on reconnection even after the sender has stopped.
Firmware cannot drive ALIVE during reset or before setup runs; the power
circuit must hold the device on through that interval. The timeout is checked
by the main loop, so blocking network operations can delay it.
It publishes the raw A0 ADC reading (0–1023) as decimal text to
`track/sensor/node2B/Battery` once per minute while MQTT is connected. Battery
voltage scaling is not yet defined: use an appropriate voltage divider and
respect the board's A0 input limits.

## Clone, build and upload

Install [Visual Studio Code](https://code.visualstudio.com/) and its
[PlatformIO IDE extension](https://platformio.org/platformio-ide). Clone and
open the project:

```sh
git clone https://github.com/swatso/Wifi-Puck-Controller.git
cd Wifi-Puck-Controller
code .
```

Create the three files described above, connect the board by USB, then use
PlatformIO's **Build**, **Upload**, and **Upload Filesystem Image** commands.
The filesystem image must be uploaded after creating or changing the files.
Alternatively, with PlatformIO Core installed:

```sh
pio run
pio run -t upload
pio run -t uploadfs
```
