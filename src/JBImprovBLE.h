/**
 * @file JBImprovBLE.h
 * @brief Improv Wi-Fi over Bluetooth Low Energy (ESP32 only).
 *
 * Works with the https://www.improv-wifi.com/ web page (Chrome, Edge), the Home
 * Assistant Improv BLE integration and the Improv apps.
 *
 * This transport is header-only, so the BLE library is only compiled into sketches
 * that include this file.
 */
#pragma once

#include <Arduino.h>

#include "JBImprov.h"

#if defined(ESP32) && (defined(CONFIG_BLUEDROID_ENABLED) || defined(CONFIG_NIMBLE_ENABLED))
/// Defined when JBImprovBLE is available (ESP32 with Bluetooth).
#define JBIMPROV_HAS_BLE 1

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#if defined(CONFIG_BLUEDROID_ENABLED)
#include <BLE2902.h>
#endif
#include <string.h>

/// Improv GATT service.
#define JBIMPROV_BLE_SERVICE_UUID "00467768-6228-2272-4663-277478268000"
/// Current state characteristic (read, notify).
#define JBIMPROV_BLE_STATE_UUID "00467768-6228-2272-4663-277478268001"
/// Error state characteristic (read, notify).
#define JBIMPROV_BLE_ERROR_UUID "00467768-6228-2272-4663-277478268002"
/// RPC command characteristic (write).
#define JBIMPROV_BLE_RPC_COMMAND_UUID "00467768-6228-2272-4663-277478268003"
/// RPC result characteristic (read, notify).
#define JBIMPROV_BLE_RPC_RESULT_UUID "00467768-6228-2272-4663-277478268004"
/// Capabilities characteristic (read).
#define JBIMPROV_BLE_CAPABILITIES_UUID "00467768-6228-2272-4663-277478268005"

/// 16-bit UUID of the Improv service data in the advertisement.
#define JBIMPROV_BLE_SERVICE_DATA_UUID 0x4677

/// Default time an authorization lasts, in milliseconds (the specification suggests a minute).
#define JBIMPROV_DEFAULT_AUTHORIZATION_TIMEOUT 60000

/**
 * @brief Improv BLE transport using the ESP32 BLE library.
 *
 * Owns the BLE stack (BLEDevice::init()), so don't use it together with other BLE
 * server code. The advertised name is the device name.
 */
class JBImprovBLE : public JBImprovTransport {
public:
  JBImprovBLE() : _serverCallbacks(*this), _commandCallbacks(*this) {}

  /**
   * @brief Require physical authorization before credentials are accepted.
   *
   * When on, the device reports "authorization required" until authorize() is
   * called, for example from a button press. Off by default, which lets anyone in
   * range provision the device while it isn't connected.
   */
  void setAuthorizationRequired(bool required) {
    _authorizationRequired = required;
    refreshState();
  }

  /// How long an authorization lasts.
  void setAuthorizationTimeout(uint32_t milliseconds) { _authorizationTimeout = milliseconds; }

  /// Authorize the client, e.g. when a button is pressed. Lasts the authorization timeout.
  void authorize() {
    _authorized = true;
    _authorizedAt = millis();
    refreshState();
  }

  /// true if credentials are accepted right now.
  bool isAuthorized() override {
    return !_authorizationRequired || _authorized;
  }

  /**
   * @brief Keep advertising after the device is provisioned (default false).
   *
   * The specification stops the Improv service once provisioned. Turn this on to
   * allow changing the network over BLE later.
   */
  void setAdvertiseWhenProvisioned(bool advertise) {
    _advertiseWhenProvisioned = advertise;
    _advertisingDirty = true;
  }

  /// true while a client is connected.
  bool isClientConnected() const { return _clientConnected; }

protected:
  void begin() override {
    char name[30];
    // A complete local name of up to 29 bytes fits in the scan response.
    strncpy(name, _improv->deviceName(), sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    if (name[0] == '\0') {
      strcpy(name, "Improv");
    }
    strcpy(_name, name);

    BLEDevice::init(name);
    _server = BLEDevice::createServer();
    _server->setCallbacks(&_serverCallbacks);
    BLEService* service = _server->createService(JBIMPROV_BLE_SERVICE_UUID);

    _stateCharacteristic = createCharacteristic(
        service, JBIMPROV_BLE_STATE_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    _errorCharacteristic = createCharacteristic(
        service, JBIMPROV_BLE_ERROR_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    _commandCharacteristic = createCharacteristic(
        service, JBIMPROV_BLE_RPC_COMMAND_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
    _resultCharacteristic = createCharacteristic(
        service, JBIMPROV_BLE_RPC_RESULT_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
    _capabilitiesCharacteristic =
        createCharacteristic(service, JBIMPROV_BLE_CAPABILITIES_UUID, BLECharacteristic::PROPERTY_READ);
    _commandCharacteristic->setCallbacks(&_commandCallbacks);

    uint8_t capabilities = _improv->capabilities();
    _capabilitiesCharacteristic->setValue(&capabilities, 1);
    uint8_t error = (uint8_t)JBImprovError::None;
    _errorCharacteristic->setValue(&error, 1);
    _lastError = JBImprovError::None;
    _bleState = bleState(_improv->state());
    uint8_t state = (uint8_t)_bleState;
    _stateCharacteristic->setValue(&state, 1);

    service->start();
    _started = true;
    updateAdvertising();
  }

  void loop() override {
    if (!_started) {
      return;
    }
    if (_authorized && _authorizationRequired &&
        _improv->state() != JBImprovState::Provisioning &&
        (uint32_t)(millis() - _authorizedAt) > _authorizationTimeout) {
      _authorized = false;
      refreshState();
    }

    uint8_t command[JBIMPROV_MAX_DATA_LENGTH + 3];
    size_t length = 0;
    bool disconnected;
    portENTER_CRITICAL(&_lock);
    if (_commandLength > 0) {
      length = _commandLength;
      memcpy(command, _command, length);
      _commandLength = 0;
    }
    disconnected = _disconnected;
    _disconnected = false;
    portEXIT_CRITICAL(&_lock);

    if (length > 0) {
      handleBleCommand(command, length);
    }
    if (disconnected) {
      // Bluedroid stops advertising on connect; start again for the next client.
      _advertisingDirty = true;
    }
    if (_advertisingDirty) {
      updateAdvertising();
    }
  }

  void sendState(JBImprovState state) override {
    (void)state;
    refreshState();
  }

  void sendError(JBImprovError error) override {
    if (!_started) {
      return;
    }
    uint8_t value = (uint8_t)error;
    _errorCharacteristic->setValue(&value, 1);
    _errorCharacteristic->notify();
  }

  void sendResult(uint8_t command, const uint8_t* data, uint8_t length) override {
    if (!_started) {
      return;
    }
    uint8_t payload[JBIMPROV_MAX_DATA_LENGTH + 3];
    size_t size = jbImprovEncodeRpcResult(command, data, length, true, payload);
    _resultCharacteristic->setValue(payload, size);
    _resultCharacteristic->notify();
  }

  bool handleTransportCommand(uint8_t command, const uint8_t* data, uint8_t length) override {
    (void)data;
    (void)length;
    if (command != (uint8_t)JBImprovCommand::Identify ||
        (_improv->capabilities() & JBIMPROV_CAPABILITY_IDENTIFY) == 0) {
      return false;
    }
    _improv->identify();
    return true;
  }

  void authorizationUsed() override {
    if (_authorized) {
      _authorizedAt = millis();
    }
  }

private:
  class ServerCallbacks : public BLEServerCallbacks {
  public:
    explicit ServerCallbacks(JBImprovBLE& owner) : _owner(owner) {}
    void onConnect(BLEServer* server) override {
      (void)server;
      _owner._clientConnected = true;
    }
    void onDisconnect(BLEServer* server) override {
      (void)server;
      _owner._clientConnected = false;
      portENTER_CRITICAL(&_owner._lock);
      _owner._incomingLength = 0;
      _owner._disconnected = true;
      portEXIT_CRITICAL(&_owner._lock);
    }

  private:
    JBImprovBLE& _owner;
  };

  class CommandCallbacks : public BLECharacteristicCallbacks {
  public:
    explicit CommandCallbacks(JBImprovBLE& owner) : _owner(owner) {}
    void onWrite(BLECharacteristic* characteristic) override {
      auto value = characteristic->getValue();
      _owner.receive((const uint8_t*)value.c_str(), value.length());
    }

  private:
    JBImprovBLE& _owner;
  };

  static BLECharacteristic* createCharacteristic(BLEService* service, const char* uuid,
                                                 uint32_t properties) {
    BLECharacteristic* characteristic = service->createCharacteristic(uuid, properties);
#if defined(CONFIG_BLUEDROID_ENABLED)
    if (properties & BLECharacteristic::PROPERTY_NOTIFY) {
      characteristic->addDescriptor(new BLE2902());
    }
#endif
    return characteristic;
  }

  /// Called from the BLE task with each write. Clients split long commands over
  /// several writes, so collect bytes until the command is complete.
  void receive(const uint8_t* data, size_t length) {
    portENTER_CRITICAL(&_lock);
    for (size_t i = 0; i < length; i++) {
      if (_incomingLength >= sizeof(_incoming)) {
        _incomingLength = 0;
      }
      _incoming[_incomingLength++] = data[i];
      if (_incomingLength >= 2 && _incomingLength == (size_t)_incoming[1] + 3) {
        // Complete: command, length, data, checksum.
        memcpy(_command, _incoming, _incomingLength);
        _commandLength = _incomingLength;
        _incomingLength = 0;
      }
    }
    portEXIT_CRITICAL(&_lock);
  }

  void handleBleCommand(const uint8_t* command, size_t length) {
    size_t dataLength = command[1];
    if (jbImprovChecksum(command, length - 1) != command[length - 1] || length != dataLength + 3) {
      setError(JBImprovError::InvalidRpcPacket);
      return;
    }
    handleCommand(command[0], &command[2], (uint8_t)dataLength);
  }

  JBImprovState bleState(JBImprovState state) {
    if (state == JBImprovState::Ready) {
      return isAuthorized() ? JBImprovState::Authorized : JBImprovState::AuthorizationRequired;
    }
    return state;
  }

  /// Publish the state if it changed, and update the advertisement.
  void refreshState() {
    if (!_started) {
      return;
    }
    JBImprovState state = bleState(_improv->state());
    if (state == _bleState) {
      return;
    }
    _bleState = state;
    uint8_t value = (uint8_t)state;
    _stateCharacteristic->setValue(&value, 1);
    _stateCharacteristic->notify();
    _advertisingDirty = true;
  }

  void updateAdvertising() {
    _advertisingDirty = false;
    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->stop();
    if (_improv->state() == JBImprovState::Provisioned && !_advertiseWhenProvisioned) {
      return;
    }
    if (_clientConnected) {
      return;  // Restarted on disconnect.
    }

    // Flags (3) + service UUID (18) + service data (10) = the full 31 bytes, so the
    // name goes in the scan response.
    BLEAdvertisementData data;
    data.setFlags(0x06);  // General discoverable, BR/EDR not supported.
    data.setCompleteServices(BLEUUID(JBIMPROV_BLE_SERVICE_UUID));
    char serviceData[] = {
        9,     // Length
        0x16,  // Service data, 16-bit UUID
        (char)(JBIMPROV_BLE_SERVICE_DATA_UUID & 0xFF),
        (char)(JBIMPROV_BLE_SERVICE_DATA_UUID >> 8),
        (char)_bleState,
        (char)_improv->capabilities(),
        0,
        0,
        0,
        0};
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    data.addData(serviceData, sizeof(serviceData));
#else
    data.addData(std::string(serviceData, sizeof(serviceData)));
#endif
    advertising->setAdvertisementData(data);

    BLEAdvertisementData response;
    response.setName(_name);
    advertising->setScanResponseData(response);
    advertising->start();
  }

  ServerCallbacks _serverCallbacks;
  CommandCallbacks _commandCallbacks;
  BLEServer* _server = nullptr;
  BLECharacteristic* _stateCharacteristic = nullptr;
  BLECharacteristic* _errorCharacteristic = nullptr;
  BLECharacteristic* _commandCharacteristic = nullptr;
  BLECharacteristic* _resultCharacteristic = nullptr;
  BLECharacteristic* _capabilitiesCharacteristic = nullptr;
  char _name[30] = {0};

  bool _started = false;
  JBImprovState _bleState = JBImprovState::Stopped;
  bool _advertisingDirty = false;
  bool _advertiseWhenProvisioned = false;
  volatile bool _clientConnected = false;

  bool _authorizationRequired = false;
  bool _authorized = false;
  uint32_t _authorizedAt = 0;
  uint32_t _authorizationTimeout = JBIMPROV_DEFAULT_AUTHORIZATION_TIMEOUT;

  // Shared with the BLE task, guarded by _lock.
  portMUX_TYPE _lock = portMUX_INITIALIZER_UNLOCKED;
  uint8_t _incoming[JBIMPROV_MAX_DATA_LENGTH + 3];
  size_t _incomingLength = 0;
  uint8_t _command[JBIMPROV_MAX_DATA_LENGTH + 3];
  size_t _commandLength = 0;
  bool _disconnected = false;
};

#else
#warning "JBImprovBLE needs an ESP32 with Bluetooth; JBImprovBLE is not available on this board."
#endif
