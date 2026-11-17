/**
 * @file JBImprovWiFi.h
 * @brief The Wi-Fi interface JBImprov uses to connect and scan.
 *
 * ESP32 and ESP8266 get a ready made implementation, JBImprovDefaultWiFi. On other
 * boards, derive a class from JBImprovWiFi and pass it to the JBImprov constructor.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/// JBImprovWiFi::scanStatus() value while a scan is running.
#define JBIMPROV_SCAN_RUNNING (-1)
/// JBImprovWiFi::scanStatus() value when the scan failed or wasn't started.
#define JBIMPROV_SCAN_FAILED (-2)

/**
 * @brief A Wi-Fi network found by a scan.
 */
struct JBImprovNetwork {
  char ssid[33];     ///< Network name, null terminated.
  int rssi;          ///< Signal strength in dBm.
  const char* auth;  ///< Authentication type, e.g. "WPA2", "WPA/WPA2" or "NO" for open.
};

/**
 * @brief Wi-Fi operations needed by JBImprov.
 *
 * All methods are called from JBImprov::loop() and must not block for long.
 */
class JBImprovWiFi {
public:
  /// Result of connectStatus().
  enum class ConnectStatus : uint8_t {
    Connecting,  ///< Still trying.
    Connected,   ///< Connected and has an IP address.
    Failed       ///< Gave up, e.g. wrong password. A timeout is handled by JBImprov.
  };

  virtual ~JBImprovWiFi() {}

  /// Called from JBImprov::begin().
  virtual void begin() {}

  /// true when connected to a network and an IP address is assigned.
  virtual bool isConnected() = 0;

  /**
   * @brief Start connecting to a network. Must return without waiting.
   * @param ssid Network name.
   * @param password Empty string for an open network.
   * @param hostname Hostname to use, may be empty.
   * @return false if the attempt couldn't be started.
   */
  virtual bool connect(const char* ssid, const char* password, const char* hostname) = 0;

  /// Progress of the attempt started by connect().
  virtual ConnectStatus connectStatus() = 0;

  /// Called when JBImprov gives up on an attempt (timeout or failure).
  virtual void cancelConnect() {}

  /// Write the local IPv4 address as text. @return false if there is none.
  virtual bool localIP(char* buffer, size_t size) = 0;

  /// Start an asynchronous scan. @return false if it couldn't be started.
  virtual bool startScan() = 0;

  /**
   * @brief Scan progress: the number of networks found when done, or
   * JBIMPROV_SCAN_RUNNING / JBIMPROV_SCAN_FAILED.
   */
  virtual int scanStatus() = 0;

  /// Get network @p index (0 based) of the last scan. @return false if out of range.
  virtual bool scanResult(int index, JBImprovNetwork& network) = 0;

  /// Free the scan results.
  virtual void scanDelete() {}

  /**
   * @brief Use a new hostname. It may take effect on the next connection.
   * @return false if the hostname isn't accepted (e.g. too long).
   */
  virtual bool setHostname(const char* hostname) {
    (void)hostname;
    return true;
  }

  /// Write the hostname to use when none is set. @return false if there is none.
  virtual bool defaultHostname(char* buffer, size_t size) {
    (void)buffer;
    (void)size;
    return false;
  }

  /// Hardware chip or variant for device info, e.g. "ESP32-S3".
  virtual const char* hardware() { return "unknown"; }

  /// Name of the OS or framework for device info, or nullptr to leave it out.
  virtual const char* osName() { return nullptr; }

  /// Version of the OS or framework for device info, or nullptr to leave it out.
  virtual const char* osVersion() { return nullptr; }
};

#if defined(ESP32) || defined(ESP8266)
/// Defined when JBImprovDefaultWiFi is available (ESP32 and ESP8266).
#define JBIMPROV_HAS_DEFAULT_WIFI 1

/**
 * @brief JBImprovWiFi for the ESP32 and ESP8266 Arduino cores.
 *
 * Credentials are stored by the core (WiFi.persistent() is on by default), so after
 * a restart a plain WiFi.begin() reconnects to the provisioned network.
 */
class JBImprovDefaultWiFi : public JBImprovWiFi {
public:
  void begin() override;
  bool isConnected() override;
  bool connect(const char* ssid, const char* password, const char* hostname) override;
  ConnectStatus connectStatus() override;
  void cancelConnect() override;
  bool localIP(char* buffer, size_t size) override;
  bool startScan() override;
  int scanStatus() override;
  bool scanResult(int index, JBImprovNetwork& network) override;
  void scanDelete() override;
  bool setHostname(const char* hostname) override;
  bool defaultHostname(char* buffer, size_t size) override;
  const char* hardware() override;
  const char* osName() override;
  const char* osVersion() override;

private:
  void ensureStationMode();
  char _hardware[24] = {0};
  char _osVersion[16] = {0};
};
#endif
