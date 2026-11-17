/*
  Improv Wi-Fi over serial.

  Flash this sketch to an ESP32 or ESP8266, then open https://www.improv-wifi.com/
  in Chrome or Edge, select "Connect device to Wi-Fi" and pick the serial port. Pick a
  network and enter the password. ESP Web Tools install pages work the same way.

  The credentials are stored by the Wi-Fi core, so the device reconnects by itself
  after a restart.
*/
#include <Arduino.h>
#if defined(ESP32)
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>
#endif

#include <JBImprov.h>
#include <JBImprovSerial.h>

JBImprov improv;
JBImprovSerial improvSerial(Serial);

void setup() {
  Serial.begin(115200);

  improv.setFirmware("JBImprov Serial Example", "1.0.0");
  improv.setDeviceName("Improv Serial Example");

  improv.onProvisioned([](const char* ssid, const char* password) {
    (void)password;
    Serial.print("Connected to ");
    Serial.println(ssid);
  });

  improv.addTransport(improvSerial);
  improv.begin();

  // Reconnect with stored credentials, if there are any.
  WiFi.mode(WIFI_STA);
  WiFi.begin();
}

void loop() {
  improv.loop();
}
