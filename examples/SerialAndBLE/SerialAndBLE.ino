/*
  Improv Wi-Fi over serial and Bluetooth LE at the same time, the way ESPHome does it
  (ESP32 only).

  - Provision from a browser over USB (https://www.improv-wifi.com/ or ESP Web Tools)
    or over BLE (the same page, Home Assistant or the Improv apps).
  - After provisioning, the client offers a link to the small web page this sketch
    serves (the redirect URL).
  - A hostname or device name set by the client is stored in Preferences and used
    again after a restart.

  Wi-Fi and BLE together don't fit the default app partition. In the Arduino IDE
  select Tools > Partition Scheme > Huge APP; in PlatformIO set
  board_build.partitions = huge_app.csv.
*/
#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#include <JBImprov.h>
#include <JBImprovBLE.h>
#include <JBImprovSerial.h>

JBImprov improv;
JBImprovSerial improvSerial(Serial);
JBImprovBLE improvBLE;

Preferences preferences;
WebServer server(80);
bool serverStarted = false;

void handleRoot() {
  String page = "<!DOCTYPE html><html><body><h1>";
  page += improv.deviceName();
  page += "</h1><p>Provisioned with Improv Wi-Fi.</p><p>Hostname: ";
  page += improv.hostname();
  page += "</p></body></html>";
  server.send(200, "text/html", page);
}

void setup() {
  Serial.begin(115200);

  preferences.begin("improv", false);
  String deviceName = preferences.getString("name", "Improv Example");
  String hostname = preferences.getString("hostname", "");

  improv.setFirmware("JBImprov Example", "1.0.0");
  improv.setDeviceName(deviceName.c_str());
  if (hostname.length() > 0) {
    improv.setHostname(hostname.c_str());
  }
  improv.setRedirectUrl("http://{{ip_address}}/");

  improv.onHostnameChanged([](const char* name) { preferences.putString("hostname", name); });
  improv.onDeviceNameChanged([](const char* name) { preferences.putString("name", name); });

  improv.addTransport(improvSerial);
  improv.addTransport(improvBLE);
  improv.begin();

  WiFi.mode(WIFI_STA);
  WiFi.begin();

  server.on("/", handleRoot);
}

void loop() {
  improv.loop();

  if (improv.isProvisioned()) {
    if (!serverStarted) {
      server.begin();
      serverStarted = true;
    }
    server.handleClient();
  }
}
