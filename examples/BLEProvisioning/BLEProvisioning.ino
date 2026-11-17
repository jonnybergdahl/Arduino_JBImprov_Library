/*
  Improv Wi-Fi over Bluetooth LE (ESP32 only).

  Flash this sketch, then open https://www.improv-wifi.com/ in Chrome or Edge and
  select "Connect device to Wi-Fi" under Bluetooth. Home Assistant also discovers the
  device through its Improv via BLE integration.

  For security, credentials are only accepted after the BOOT button has been pressed;
  the authorization lasts one minute. Set REQUIRE_BUTTON to false to skip that.
  Selecting "Identify" in the client blinks the built-in LED.

  Wi-Fi and BLE together don't fit the default app partition. In the Arduino IDE
  select Tools > Partition Scheme > Huge APP; in PlatformIO set
  board_build.partitions = huge_app.csv.
*/
#include <Arduino.h>
#include <WiFi.h>

#include <JBImprov.h>
#include <JBImprovBLE.h>

#define REQUIRE_BUTTON true
#define BUTTON_PIN 0  // BOOT button on most ESP32 boards

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

JBImprov improv;
JBImprovBLE improvBLE;

uint32_t identifyUntil = 0;

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  improv.setFirmware("JBImprov BLE Example", "1.0.0");
  improv.setDeviceName("Improv BLE Example");

  improv.onIdentify([]() { identifyUntil = millis() + 5000; });
  improv.onStateChanged([](JBImprovState state) {
    Serial.print("Improv state: ");
    Serial.println((int)state);
  });
  improv.onProvisioned([](const char* ssid, const char* password) {
    (void)password;
    Serial.print("Connected to ");
    Serial.println(ssid);
  });

  improvBLE.setAuthorizationRequired(REQUIRE_BUTTON);
  improv.addTransport(improvBLE);
  improv.begin();

  WiFi.mode(WIFI_STA);
  WiFi.begin();
}

void loop() {
  improv.loop();

  if (REQUIRE_BUTTON && digitalRead(BUTTON_PIN) == LOW) {
    improvBLE.authorize();
  }

  bool blink = (int32_t)(identifyUntil - millis()) > 0 && (millis() / 250) % 2;
  digitalWrite(LED_BUILTIN, blink ? HIGH : LOW);
}
