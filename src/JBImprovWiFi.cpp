#include "JBImprovWiFi.h"

#if defined(JBIMPROV_HAS_DEFAULT_WIFI)

#include <Arduino.h>
#include <string.h>

#if defined(ESP32)
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#endif

static void copyString(char* buffer, size_t size, const char* value) {
  if (size == 0) {
    return;
  }
  strncpy(buffer, value == nullptr ? "" : value, size - 1);
  buffer[size - 1] = '\0';
}

void JBImprovDefaultWiFi::begin() {}

void JBImprovDefaultWiFi::ensureStationMode() {
  auto mode = WiFi.getMode();
  if (mode == WIFI_AP) {
    WiFi.mode(WIFI_AP_STA);
  } else if (mode != WIFI_STA && mode != WIFI_AP_STA) {
    WiFi.mode(WIFI_STA);
  }
}

bool JBImprovDefaultWiFi::isConnected() {
  return WiFi.status() == WL_CONNECTED;
}

bool JBImprovDefaultWiFi::connect(const char* ssid, const char* password, const char* hostname) {
  bool hasHostname = hostname != nullptr && hostname[0] != '\0';
  if (hasHostname) {
    // Before the station interface starts, so the first DHCP request uses it.
    setHostname(hostname);
  }
  ensureStationMode();
  if (hasHostname) {
    setHostname(hostname);
  }
  WiFi.disconnect();
  WiFi.begin(ssid, password);
  return true;
}

JBImprovWiFi::ConnectStatus JBImprovDefaultWiFi::connectStatus() {
  switch (WiFi.status()) {
    case WL_CONNECTED:
      return ConnectStatus::Connected;
#if defined(ESP8266)
    // The ESP32 core keeps retrying after these, so there they end in a timeout.
    case WL_CONNECT_FAILED:
    case WL_WRONG_PASSWORD:
      return ConnectStatus::Failed;
#endif
    default:
      return ConnectStatus::Connecting;
  }
}

void JBImprovDefaultWiFi::cancelConnect() {
  WiFi.disconnect();
}

bool JBImprovDefaultWiFi::localIP(char* buffer, size_t size) {
  if (!isConnected()) {
    return false;
  }
  copyString(buffer, size, WiFi.localIP().toString().c_str());
  return buffer[0] != '\0';
}

bool JBImprovDefaultWiFi::startScan() {
  ensureStationMode();
  int result = WiFi.scanNetworks(true);
  return result != WIFI_SCAN_FAILED;
}

int JBImprovDefaultWiFi::scanStatus() {
  int result = WiFi.scanComplete();
  if (result == WIFI_SCAN_RUNNING) {
    return JBIMPROV_SCAN_RUNNING;
  }
  if (result < 0) {
    return JBIMPROV_SCAN_FAILED;
  }
  return result;
}

#if defined(ESP32)
static const char* authName(wifi_auth_mode_t mode) {
  switch (mode) {
    case WIFI_AUTH_OPEN:
      return "NO";
    case WIFI_AUTH_WEP:
      return "WEP";
    case WIFI_AUTH_WPA_PSK:
      return "WPA";
    case WIFI_AUTH_WPA2_PSK:
      return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:
      return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE:
      return "WPA2 EAP";
    case WIFI_AUTH_WPA3_PSK:
      return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
      return "WPA2/WPA3";
    case WIFI_AUTH_WAPI_PSK:
      return "WAPI";
    default:
      return "WPA2";
  }
}
#else
static const char* authName(uint8_t type) {
  switch (type) {
    case ENC_TYPE_NONE:
      return "NO";
    case ENC_TYPE_WEP:
      return "WEP";
    case ENC_TYPE_TKIP:
      return "WPA";
    case ENC_TYPE_CCMP:
      return "WPA2";
    case ENC_TYPE_AUTO:
      return "WPA/WPA2";
    default:
      return "WPA2";
  }
}
#endif

bool JBImprovDefaultWiFi::scanResult(int index, JBImprovNetwork& network) {
  if (index < 0 || index >= WiFi.scanComplete()) {
    return false;
  }
  copyString(network.ssid, sizeof(network.ssid), WiFi.SSID(index).c_str());
  network.rssi = WiFi.RSSI(index);
  network.auth = authName(WiFi.encryptionType(index));
  return true;
}

void JBImprovDefaultWiFi::scanDelete() {
  WiFi.scanDelete();
}

bool JBImprovDefaultWiFi::setHostname(const char* hostname) {
#if defined(ESP32)
  // The ESP32 core keeps the hostname in a 32 byte buffer.
  if (strlen(hostname) > 31) {
    return false;
  }
  WiFi.setHostname(hostname);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  // Also update an interface that is already running; fails harmlessly if not.
  WiFi.STA.setHostname(hostname);
#endif
  return true;
#else
  if (strlen(hostname) > 32) {
    return false;
  }
  return WiFi.hostname(hostname);
#endif
}

bool JBImprovDefaultWiFi::defaultHostname(char* buffer, size_t size) {
#if defined(ESP32)
  const char* hostname = WiFi.getHostname();
  if (hostname != nullptr && hostname[0] != '\0') {
    copyString(buffer, size, hostname);
  } else {
    uint64_t mac = ESP.getEfuseMac();
    snprintf(buffer, size, "esp32-%02x%02x%02x", (uint8_t)(mac >> 24), (uint8_t)(mac >> 32),
             (uint8_t)(mac >> 40));
  }
#else
  copyString(buffer, size, WiFi.hostname().c_str());
#endif
  return buffer[0] != '\0';
}

const char* JBImprovDefaultWiFi::hardware() {
  if (_hardware[0] == '\0') {
#if defined(ESP32)
    copyString(_hardware, sizeof(_hardware), ESP.getChipModel());
#else
    copyString(_hardware, sizeof(_hardware), "ESP8266");
#endif
  }
  return _hardware;
}

const char* JBImprovDefaultWiFi::osName() {
#if defined(ESP32)
  return "Arduino ESP32";
#else
  return "Arduino ESP8266";
#endif
}

const char* JBImprovDefaultWiFi::osVersion() {
  if (_osVersion[0] == '\0') {
#if defined(ESP32)
    snprintf(_osVersion, sizeof(_osVersion), "%d.%d.%d", ESP_ARDUINO_VERSION_MAJOR,
             ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);
#else
    copyString(_osVersion, sizeof(_osVersion), ESP.getCoreVersion().c_str());
#endif
  }
  return _osVersion;
}

#endif  // JBIMPROV_HAS_DEFAULT_WIFI
