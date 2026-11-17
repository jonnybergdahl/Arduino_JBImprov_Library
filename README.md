# Arduino_JBImprov_Library

An Arduino library for [Improv Wi-Fi](https://www.improv-wifi.com/) provisioning over serial and Bluetooth LE.

## What is this library?

**JBImprov** lets users send Wi-Fi credentials to your device without hardcoding them in the firmware. It works the same way as in ESPHome: from a browser over USB, or from a browser, Home Assistant or a phone over Bluetooth.

It implements the device side of both Improv protocols:

- **[Improv Serial](https://www.improv-wifi.com/serial/)**: works with [ESP Web Tools](https://esphome.github.io/esp-web-tools/) install pages and the [improv-wifi.com](https://www.improv-wifi.com/) page (Chrome, Edge).
- **[Improv BLE](https://www.improv-wifi.com/ble/)**: works with the improv-wifi.com page, the Home Assistant [Improv via BLE](https://www.home-assistant.io/integrations/improv_ble/) integration and the Improv apps.

You can use one or both. They share a single Wi-Fi state, so a device provisioned over BLE also reports *provisioned* over serial.

### Features

- All RPC commands: Wi-Fi settings, current state, identify, device info, Wi-Fi scan, hostname, device name and network state
- Redirect URL after provisioning, with `{{ip_address}}`, `{{hostname}}` and `{{device_name}}` placeholders
- BLE authorization (e.g. a button press) with timeout, as recommended by the specification
- Works next to your own log output on the same serial port
- Non-blocking: everything runs from `loop()`
- No dependencies outside the Arduino core

### Supported boards

| Board | Serial | BLE |
|---|---|---|
| ESP32, ESP32-S3, ESP32-C3, ESP32-C6 (Arduino core 2.x and 3.x) | ✅ | ✅ |
| ESP32-S2 | ✅ | ❌ (no Bluetooth) |
| ESP8266 | ✅ | ❌ (no Bluetooth) |
| Other boards | ✅ with your own [Wi-Fi adapter](#other-boards) | ❌ |

## Installation

### Arduino IDE

JBImprov is available in the Arduino Library Manager:

1. Select **Sketch** > **Include Library** > **Manage Libraries...** (or the **Library Manager** in the sidebar).
2. Search for **JBImprov**.
3. Select **Install**.

### PlatformIO

```ini
lib_deps =
    https://github.com/jonnybergdahl/Arduino_JBImprov_Library.git
```

### Partition scheme for BLE

Wi-Fi and Bluetooth together take about 1.6 MB of flash on ESP32 core 3.x, more than the default 1.25 MB app partition. Pick a bigger app partition:

- Arduino IDE: **Tools** > **Partition Scheme** > **Huge APP (3MB No OTA/1MB SPIFFS)**, or a scheme with OTA if your board has more flash
- PlatformIO: `board_build.partitions = huge_app.csv`

Serial-only sketches fit the default partition.

## Usage

### Improv Serial

```c++
#include <WiFi.h>  // ESP8266WiFi.h on the ESP8266
#include <JBImprov.h>
#include <JBImprovSerial.h>

JBImprov improv;
JBImprovSerial improvSerial(Serial);

void setup() {
  Serial.begin(115200);

  improv.setFirmware("MyFirmware", "1.0.0");
  improv.setDeviceName("Kitchen Sensor");
  improv.addTransport(improvSerial);
  improv.begin();

  // Reconnect with stored credentials, if there are any.
  WiFi.mode(WIFI_STA);
  WiFi.begin();
}

void loop() {
  improv.loop();
}
```

Flash the sketch, open [improv-wifi.com](https://www.improv-wifi.com/), select **Connect device to Wi-Fi** and pick the serial port.

You can keep printing log output on `Serial`. Clients skip anything that isn't an Improv frame. JBImprov reads all incoming bytes on the port, though. If your sketch needs serial input too, call `improvSerial.setAutoRead(false)`, read the port yourself and pass each byte to `improvSerial.handleByte(byte)`. It returns `false` for bytes that aren't Improv data.

On boards with native USB (ESP32-S3, ESP32-C3, ESP32-C6), enable **Tools** > **USB CDC On Boot** so that `Serial` is the USB port.

### Improv BLE

```c++
#include <WiFi.h>
#include <JBImprov.h>
#include <JBImprovBLE.h>

JBImprov improv;
JBImprovBLE improvBLE;

void setup() {
  improv.setFirmware("MyFirmware", "1.0.0");
  improv.setDeviceName("Kitchen Sensor");  // Also the advertised BLE name
  improv.onIdentify([]() { /* blink a LED */ });

  improv.addTransport(improvBLE);
  improv.begin();

  WiFi.mode(WIFI_STA);
  WiFi.begin();
}

void loop() {
  improv.loop();
}
```

`JBImprovBLE` owns the ESP32 BLE stack, so don't run other BLE server code next to it. Like ESPHome, it stops advertising once the device is provisioned, and starts again if the Wi-Fi connection is lost for more than 10 seconds. Call `improvBLE.setAdvertiseWhenProvisioned(true)` to keep it available.

#### Authorization

By default, anyone within Bluetooth range can provision the device while it isn't connected to Wi-Fi. To require a physical action first:

```c++
improvBLE.setAuthorizationRequired(true);
improvBLE.setAuthorizationTimeout(60000);  // Default: one minute

void loop() {
  improv.loop();
  if (digitalRead(BUTTON_PIN) == LOW) {
    improvBLE.authorize();
  }
}
```

Until `authorize()` is called, the device reports *authorization required*. It rejects Wi-Fi credentials, hostname changes and device name changes.

### Serial and BLE together

Add both transports to the same `JBImprov` instance:

```c++
JBImprov improv;
JBImprovSerial improvSerial(Serial);
JBImprovBLE improvBLE;

void setup() {
  Serial.begin(115200);
  improv.addTransport(improvSerial);
  improv.addTransport(improvBLE);
  improv.begin();
}
```

See [examples/SerialAndBLE](https://github.com/jonnybergdahl/Arduino_JBImprov_Library/blob/main/examples/SerialAndBLE/SerialAndBLE.ino). It also serves a web page as the redirect URL and stores the hostname and device name in `Preferences`.

## Configuration

Full code documentation is available at https://jonnybergdahl.github.io/jbimprov/.

Call these before `begin()`.

| Method | Description |
|---|---|
| `setFirmware(name, version)` | Firmware name and version in device info. Not copied, string literals are fine. |
| `setDeviceName(name)` | User-facing name. Defaults to the hostname. |
| `setHostname(hostname)` | Hostname used when connecting. Defaults to the core's hostname, e.g. `esp32s3-4F44C0`. Returns `false` for an invalid hostname. |
| `setHardware(hardware)` | Override the hardware string. Defaults to the chip, e.g. `ESP32-S3`. |
| `setRedirectUrl(url)` | URL the client offers after provisioning, e.g. `"http://{{ip_address}}/"`. Not copied. |
| `setConnectTimeout(ms)` | How long to wait for a connection before reporting *unable to connect*. Default 30 s. |
| `addTransport(transport)` | Add a `JBImprovSerial` or `JBImprovBLE`. Up to three transports. |

### Callbacks

| Method | Called when |
|---|---|
| `onProvisioned(void(const char* ssid, const char* password))` | The device connected with credentials from a client |
| `onStateChanged(void(JBImprovState state))` | The state changes (`Ready`, `Provisioning`, `Provisioned`) |
| `onIdentify(void())` | A client asks the device to identify itself. Setting this enables the identify capability. |
| `onHostnameChanged(void(const char* hostname))` | A client sets a new hostname |
| `onDeviceNameChanged(void(const char* name))` | A client sets a new device name |

On ESP32 and ESP8266 the callbacks can be lambdas that capture variables (`std::function`).

### Status

| Method | Returns |
|---|---|
| `state()` | `JBImprovState::Ready`, `Provisioning` or `Provisioned` |
| `isProvisioned()` | `true` while connected to Wi-Fi |
| `deviceName()`, `hostname()` | The current values, including ones set by a client |

## What is stored where

- **Wi-Fi credentials** are stored by the ESP32 and ESP8266 cores, which is the default (`WiFi.persistent(true)`). After a restart, `WiFi.begin()` with no arguments reconnects. To store them yourself instead, use the `onProvisioned` callback.
- **Hostname and device name** set by a client are only kept in RAM. Store them in the `onHostnameChanged` and `onDeviceNameChanged` callbacks, and pass them back with `setHostname()` and `setDeviceName()` at startup, as the SerialAndBLE example does. The BLE name is set at startup, so a new device name shows up in BLE scans after a restart.
- On ESP32 core 2.x, a new hostname takes effect when the Wi-Fi station interface restarts, for example after a reboot.

## Other boards

On boards other than the ESP32 and ESP8266, derive a class from `JBImprovWiFi` and pass it to the constructor. It has methods to connect, report the connection status, scan, and get the IP address. See [src/JBImprovWiFi.h](https://github.com/jonnybergdahl/Arduino_JBImprov_Library/blob/main/src/JBImprovWiFi.h).

```c++
class MyWiFi : public JBImprovWiFi {
  // isConnected(), connect(), connectStatus(), localIP(), startScan(), scanStatus(), scanResult() ...
};

MyWiFi myWiFi;
JBImprov improv(myWiFi);
```

## Testing

[improv-client](https://github.com/jonnybergdahl/Python-improv-client) is a command-line client for testing from a computer:

```bash
improv-client serial /dev/ttyUSB0 info
improv-client serial /dev/ttyUSB0 scan
improv-client discover
improv-client ble <address> provision MyNetwork
```

## Development

```bash
pio test -e native            # Unit tests on the host
scripts/build_examples.sh     # Compile all examples for ESP32 (core 2.x and 3.x), ESP32-S2, ESP32-S3 and ESP8266
./build.sh                    # Build the Doxygen docs and publish them to jonnybergdahl.github.io/jbimprov
```

The docs use the [doxygen-awesome-css](https://github.com/jothepro/doxygen-awesome-css) theme as a Git submodule. After cloning, run `git submodule update --init`.

## License

MIT, see [LICENSE](https://github.com/jonnybergdahl/Arduino_JBImprov_Library/blob/main/LICENSE).
