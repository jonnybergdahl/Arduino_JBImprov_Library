/**
 * @file JBImprov.h
 * @brief Improv Wi-Fi provisioning for Arduino.
 *
 * JBImprov holds the provisioning state, the device information and the Wi-Fi
 * handling. Transports (JBImprovSerial, JBImprovBLE) carry the protocol to the
 * client and can be combined, just like ESPHome does.
 *
 * @code
 * #include <JBImprov.h>
 * #include <JBImprovSerial.h>
 *
 * JBImprov improv;
 * JBImprovSerial improvSerial(Serial);
 *
 * void setup() {
 *   Serial.begin(115200);
 *   improv.setFirmware("MyFirmware", "1.0.0");
 *   improv.addTransport(improvSerial);
 *   improv.begin();
 * }
 *
 * void loop() {
 *   improv.loop();
 * }
 * @endcode
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "JBImprovProtocol.h"
#include "JBImprovWiFi.h"

#if defined(__has_include)
#if __has_include(<functional>)
#define JBIMPROV_USE_STD_FUNCTION 1
#endif
#endif

#if defined(JBIMPROV_USE_STD_FUNCTION)
#include <functional>
/// Called after a successful provisioning with the credentials that worked.
using JBImprovProvisionedCallback = std::function<void(const char* ssid, const char* password)>;
/// Called with the new state whenever it changes.
using JBImprovStateCallback = std::function<void(JBImprovState state)>;
/// Called when the client asks the device to identify itself.
using JBImprovIdentifyCallback = std::function<void()>;
/// Called with a new hostname or device name set by the client.
using JBImprovNameCallback = std::function<void(const char* name)>;
#else
typedef void (*JBImprovProvisionedCallback)(const char* ssid, const char* password);
typedef void (*JBImprovStateCallback)(JBImprovState state);
typedef void (*JBImprovIdentifyCallback)();
typedef void (*JBImprovNameCallback)(const char* name);
#endif

/// Maximum number of transports one JBImprov instance can serve.
#define JBIMPROV_MAX_TRANSPORTS 3

/// Maximum number of networks reported from a scan.
#define JBIMPROV_MAX_SCAN_RESULTS 20

/// Default time to wait for a Wi-Fi connection, in milliseconds.
#define JBIMPROV_DEFAULT_CONNECT_TIMEOUT 30000

class JBImprov;

/**
 * @brief Base class for the ways a client can talk to the device.
 *
 * Applications don't call these methods; they are used by JBImprov.
 */
class JBImprovTransport {
public:
  virtual ~JBImprovTransport() {}

protected:
  friend class JBImprov;

  /// Called from JBImprov::begin().
  virtual void begin() {}

  /// Called from JBImprov::loop().
  virtual void loop() {}

  /// Report the provisioning state. Called on every change and when asked.
  virtual void sendState(JBImprovState state) = 0;

  /// Write an error state to the client. Use setError() instead, it skips repeats.
  virtual void sendError(JBImprovError error) = 0;

  /// Send an RPC result. @p data holds length prefixed strings.
  virtual void sendResult(uint8_t command, const uint8_t* data, uint8_t length) = 0;

  /**
   * @brief Handle a command whose meaning depends on the transport (0x02).
   * @return true if handled.
   */
  virtual bool handleTransportCommand(uint8_t command, const uint8_t* data, uint8_t length) {
    (void)command;
    (void)data;
    (void)length;
    return false;
  }

  /// true if commands that change the device are allowed now.
  virtual bool isAuthorized() { return true; }

  /// Called after an authorized command was used, to restart an authorization timeout.
  virtual void authorizationUsed() {}

  /// true to send one RPC result per scanned network (serial), false for one combined
  /// result (BLE).
  virtual bool scanResultsPerNetwork() { return false; }

  /// Pass a received RPC command to JBImprov.
  void handleCommand(uint8_t command, const uint8_t* data, uint8_t length);

  /// Report an error state if it differs from the last one sent.
  void setError(JBImprovError error);

  JBImprov* _improv = nullptr;
  JBImprovError _lastError = JBImprovError::None;
};

/**
 * @brief Improv Wi-Fi device: state, device information and Wi-Fi provisioning.
 */
class JBImprov {
public:
#if defined(JBIMPROV_HAS_DEFAULT_WIFI)
  /// Use the built-in ESP32 / ESP8266 Wi-Fi support.
  JBImprov();
#endif

  /// Use a custom Wi-Fi implementation. @p wifi must outlive this object.
  explicit JBImprov(JBImprovWiFi& wifi);

  JBImprov(const JBImprov&) = delete;
  JBImprov& operator=(const JBImprov&) = delete;

  // --- Configuration, call before begin() -----------------------------------------

  /**
   * @brief Firmware name and version reported in device info.
   *
   * The strings are not copied and must stay valid, string literals are fine.
   */
  void setFirmware(const char* name, const char* version);

  /// Override the hardware string in device info (default: the chip, e.g. "ESP32-S3").
  /// Not copied.
  void setHardware(const char* hardware);

  /// User facing device name (copied). Defaults to the hostname.
  void setDeviceName(const char* name);

  /**
   * @brief Hostname used when connecting (copied). Defaults to the core's hostname.
   * @return false if it isn't a valid RFC 1123 hostname or the Wi-Fi layer rejects it.
   */
  bool setHostname(const char* hostname);

  /**
   * @brief URL the client offers the user after provisioning, e.g. the device's web UI.
   *
   * Placeholders: `{{ip_address}}`, `{{hostname}}` and `{{device_name}}`. Not copied.
   * Without a URL the result of a provisioning is empty.
   */
  void setRedirectUrl(const char* url);

  /// Time to wait for a connection before reporting "unable to connect".
  void setConnectTimeout(uint32_t milliseconds);

  /**
   * @brief Add a transport such as JBImprovSerial or JBImprovBLE.
   * @return false if JBIMPROV_MAX_TRANSPORTS are already added.
   */
  bool addTransport(JBImprovTransport& transport);

  // --- Callbacks ------------------------------------------------------------------

  /// Called after the device connected with credentials sent by a client.
  void onProvisioned(JBImprovProvisionedCallback callback);

  /// Called whenever the state changes.
  void onStateChanged(JBImprovStateCallback callback);

  /// Called when a client asks the device to identify itself, e.g. blink a LED.
  /// Setting it adds the identify capability (BLE).
  void onIdentify(JBImprovIdentifyCallback callback);

  /// Called when a client sets a new hostname. Store it to keep it after a restart.
  void onHostnameChanged(JBImprovNameCallback callback);

  /// Called when a client sets a new device name. Store it to keep it after a restart.
  void onDeviceNameChanged(JBImprovNameCallback callback);

  // --- Running --------------------------------------------------------------------

  /// Start all transports. Call after configuring.
  void begin();

  /// Process the transports and Wi-Fi. Call often from loop().
  void loop();

  // --- Status ---------------------------------------------------------------------

  /// Current state: Ready, Provisioning or Provisioned.
  JBImprovState state() const { return _state; }

  /// true when connected to Wi-Fi.
  bool isProvisioned() const { return _state == JBImprovState::Provisioned; }

  const char* deviceName() const { return _deviceName; }
  const char* hostname() const { return _hostname; }
  const char* firmwareName() const { return _firmwareName; }
  const char* firmwareVersion() const { return _firmwareVersion; }

  /// Capability flags for BLE.
  uint8_t capabilities() const;

  /**
   * @brief Write the redirect URL with placeholders replaced.
   * @return false if there is no URL or the device isn't connected.
   */
  bool redirectUrl(char* buffer, size_t size);

private:
  friend class JBImprovTransport;
  friend class JBImprovSerial;
  friend class JBImprovBLE;

  void handleCommand(JBImprovTransport& from, uint8_t command, const uint8_t* data,
                     uint8_t length);
  void handleWifiSettings(JBImprovTransport& from, const uint8_t* data, uint8_t length);
  void handleDeviceInfo(JBImprovTransport& from);
  void handleScan(JBImprovTransport& from);
  void handleHostname(JBImprovTransport& from, const uint8_t* data, uint8_t length);
  void handleDeviceName(JBImprovTransport& from, const uint8_t* data, uint8_t length);
  void handleNetworkState(JBImprovTransport& from);

  /// Add the redirect URL (if any) to @p result.
  void addRedirectUrl(JBImprovResultBuilder& result);

  void identify();
  void setState(JBImprovState state);
  void loopProvisioning();
  void loopScan();
  void loopConnection();
  void sendScanResults(uint8_t transports);
  int transportIndex(JBImprovTransport& transport) const;

  JBImprovWiFi* _wifi;
  JBImprovTransport* _transports[JBIMPROV_MAX_TRANSPORTS];
  uint8_t _transportCount = 0;

  const char* _firmwareName = "Arduino";
  const char* _firmwareVersion = "";
  const char* _hardware = nullptr;
  const char* _redirectUrl = nullptr;
  char _deviceName[JBIMPROV_MAX_DEVICE_NAME_LENGTH + 1] = {0};
  char _hostname[JBIMPROV_MAX_HOSTNAME_LENGTH + 1] = {0};

  JBImprovState _state = JBImprovState::Ready;
  bool _started = false;

  // Provisioning in progress.
  JBImprovTransport* _provisionRequester = nullptr;
  char _ssid[JBIMPROV_MAX_SSID_LENGTH + 1] = {0};
  char _password[JBIMPROV_MAX_PASSWORD_LENGTH + 1] = {0};
  uint32_t _provisionStarted = 0;
  uint32_t _connectTimeout = JBIMPROV_DEFAULT_CONNECT_TIMEOUT;

  // Scan in progress: one bit per transport index.
  uint8_t _scanRequesters = 0;

  // Connection monitoring outside of provisioning.
  uint32_t _lastConnectionCheck = 0;
  uint32_t _disconnectedSince = 0;

  JBImprovProvisionedCallback _onProvisioned = nullptr;
  JBImprovStateCallback _onStateChanged = nullptr;
  JBImprovIdentifyCallback _onIdentify = nullptr;
  JBImprovNameCallback _onHostnameChanged = nullptr;
  JBImprovNameCallback _onDeviceNameChanged = nullptr;
};
