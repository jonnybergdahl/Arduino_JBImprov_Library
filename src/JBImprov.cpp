#include "JBImprov.h"

#include <stdio.h>
#include <string.h>

#if defined(ARDUINO)
#include <Arduino.h>
#else
// Host builds (unit tests) provide this.
unsigned long millis();
#endif

/// How often the connection is checked outside of provisioning.
static const uint32_t CONNECTION_CHECK_INTERVAL = 1000;
/// How long the connection must be lost before the state goes back to Ready.
static const uint32_t DISCONNECT_GRACE_PERIOD = 10000;

static void copyString(char* buffer, size_t size, const char* value, size_t length) {
  if (size == 0) {
    return;
  }
  if (length >= size) {
    length = size - 1;
  }
  memcpy(buffer, value, length);
  buffer[length] = '\0';
}

static void copyString(char* buffer, size_t size, const char* value) {
  copyString(buffer, size, value == nullptr ? "" : value, value == nullptr ? 0 : strlen(value));
}

// --- JBImprovTransport -------------------------------------------------------------

void JBImprovTransport::handleCommand(uint8_t command, const uint8_t* data, uint8_t length) {
  if (_improv != nullptr) {
    _improv->handleCommand(*this, command, data, length);
  }
}

void JBImprovTransport::setError(JBImprovError error) {
  if (error != _lastError) {
    _lastError = error;
    sendError(error);
  }
}

// --- JBImprov ----------------------------------------------------------------------

#if defined(JBIMPROV_HAS_DEFAULT_WIFI)
static JBImprovDefaultWiFi defaultWiFi;

JBImprov::JBImprov() : JBImprov(defaultWiFi) {}
#endif

JBImprov::JBImprov(JBImprovWiFi& wifi) : _wifi(&wifi) {
  for (uint8_t i = 0; i < JBIMPROV_MAX_TRANSPORTS; i++) {
    _transports[i] = nullptr;
  }
}

void JBImprov::setFirmware(const char* name, const char* version) {
  _firmwareName = name == nullptr ? "" : name;
  _firmwareVersion = version == nullptr ? "" : version;
}

void JBImprov::setHardware(const char* hardware) {
  _hardware = hardware;
}

void JBImprov::setDeviceName(const char* name) {
  copyString(_deviceName, sizeof(_deviceName), name);
}

bool JBImprov::setHostname(const char* hostname) {
  if (hostname == nullptr || !jbImprovIsValidHostname(hostname, strlen(hostname))) {
    return false;
  }
  if (!_wifi->setHostname(hostname)) {
    return false;
  }
  copyString(_hostname, sizeof(_hostname), hostname);
  return true;
}

void JBImprov::setRedirectUrl(const char* url) {
  _redirectUrl = url;
}

void JBImprov::setConnectTimeout(uint32_t milliseconds) {
  _connectTimeout = milliseconds;
}

bool JBImprov::addTransport(JBImprovTransport& transport) {
  if (transportIndex(transport) >= 0) {
    return true;
  }
  if (_transportCount >= JBIMPROV_MAX_TRANSPORTS) {
    return false;
  }
  transport._improv = this;
  _transports[_transportCount++] = &transport;
  if (_started) {
    transport.begin();
  }
  return true;
}

void JBImprov::onProvisioned(JBImprovProvisionedCallback callback) {
  _onProvisioned = callback;
}

void JBImprov::onStateChanged(JBImprovStateCallback callback) {
  _onStateChanged = callback;
}

void JBImprov::onIdentify(JBImprovIdentifyCallback callback) {
  _onIdentify = callback;
}

void JBImprov::onHostnameChanged(JBImprovNameCallback callback) {
  _onHostnameChanged = callback;
}

void JBImprov::onDeviceNameChanged(JBImprovNameCallback callback) {
  _onDeviceNameChanged = callback;
}

uint8_t JBImprov::capabilities() const {
  uint8_t result =
      JBIMPROV_CAPABILITY_DEVICE_INFO | JBIMPROV_CAPABILITY_SCAN_WIFI | JBIMPROV_CAPABILITY_HOSTNAME;
  if (_onIdentify) {
    result |= JBIMPROV_CAPABILITY_IDENTIFY;
  }
  return result;
}

void JBImprov::begin() {
  if (_started) {
    return;
  }
  _wifi->begin();
  if (_hostname[0] == '\0') {
    _wifi->defaultHostname(_hostname, sizeof(_hostname));
  }
  if (_deviceName[0] == '\0') {
    copyString(_deviceName, sizeof(_deviceName), _hostname);
  }
  _state = _wifi->isConnected() ? JBImprovState::Provisioned : JBImprovState::Ready;
  _lastConnectionCheck = millis();
  _started = true;
  for (uint8_t i = 0; i < _transportCount; i++) {
    _transports[i]->begin();
  }
}

void JBImprov::loop() {
  if (!_started) {
    return;
  }
  for (uint8_t i = 0; i < _transportCount; i++) {
    _transports[i]->loop();
  }
  if (_state == JBImprovState::Provisioning) {
    loopProvisioning();
  } else {
    loopConnection();
  }
  if (_scanRequesters != 0) {
    loopScan();
  }
}

void JBImprov::setState(JBImprovState state) {
  if (state == _state) {
    return;
  }
  _state = state;
  for (uint8_t i = 0; i < _transportCount; i++) {
    _transports[i]->sendState(state);
  }
  if (_onStateChanged) {
    _onStateChanged(state);
  }
}

int JBImprov::transportIndex(JBImprovTransport& transport) const {
  for (uint8_t i = 0; i < _transportCount; i++) {
    if (_transports[i] == &transport) {
      return i;
    }
  }
  return -1;
}

void JBImprov::identify() {
  if (_onIdentify) {
    _onIdentify();
  }
}

// --- Commands ----------------------------------------------------------------------

void JBImprov::handleCommand(JBImprovTransport& from, uint8_t command, const uint8_t* data,
                             uint8_t length) {
  // The specification wants the error cleared for every command received.
  from.setError(JBImprovError::None);
  if (from.handleTransportCommand(command, data, length)) {
    return;
  }
  switch ((JBImprovCommand)command) {
    case JBImprovCommand::WifiSettings:
      handleWifiSettings(from, data, length);
      break;
    case JBImprovCommand::DeviceInfo:
      handleDeviceInfo(from);
      break;
    case JBImprovCommand::ScanWifi:
      handleScan(from);
      break;
    case JBImprovCommand::Hostname:
      handleHostname(from, data, length);
      break;
    case JBImprovCommand::DeviceName:
      handleDeviceName(from, data, length);
      break;
    case JBImprovCommand::NetworkState:
      handleNetworkState(from);
      break;
    default:
      from.setError(JBImprovError::UnknownRpcCommand);
      break;
  }
}

void JBImprov::handleWifiSettings(JBImprovTransport& from, const uint8_t* data, uint8_t length) {
  if (!from.isAuthorized()) {
    from.setError(JBImprovError::NotAuthorized);
    return;
  }
  JBImprovStringReader reader(data, length);
  char ssid[JBIMPROV_MAX_SSID_LENGTH + 1];
  char password[JBIMPROV_MAX_PASSWORD_LENGTH + 1];
  if (!reader.next(ssid, sizeof(ssid)) || !reader.next(password, sizeof(password)) ||
      ssid[0] == '\0') {
    from.setError(JBImprovError::InvalidRpcPacket);
    return;
  }

  if (_state == JBImprovState::Provisioning) {
    // A new attempt replaces the running one.
    _wifi->cancelConnect();
  }
  copyString(_ssid, sizeof(_ssid), ssid);
  copyString(_password, sizeof(_password), password);
  memset(password, 0, sizeof(password));
  _provisionRequester = &from;
  _provisionStarted = millis();
  from.authorizationUsed();
  setState(JBImprovState::Provisioning);
  if (!_wifi->connect(_ssid, _password, _hostname)) {
    // Report the failure from loop(), the same way as a timeout.
    _provisionStarted = millis() - _connectTimeout - 1;
  }
}

void JBImprov::handleDeviceInfo(JBImprovTransport& from) {
  JBImprovResultBuilder result;
  result.add(_firmwareName);
  result.add(_firmwareVersion);
  result.add(_hardware != nullptr ? _hardware : _wifi->hardware());
  result.add(_deviceName);
  const char* osName = _wifi->osName();
  if (osName != nullptr) {
    const char* osVersion = _wifi->osVersion();
    result.add(osName);
    result.add(osVersion != nullptr ? osVersion : "");
  }
  from.sendResult((uint8_t)JBImprovCommand::DeviceInfo, result.data(), result.length());
}

void JBImprov::handleScan(JBImprovTransport& from) {
  int index = transportIndex(from);
  if (index < 0) {
    return;
  }
  if (_scanRequesters == 0 && !_wifi->startScan()) {
    // Report no networks rather than leaving the client waiting.
    sendScanResults(1 << index);
    return;
  }
  _scanRequesters |= (uint8_t)(1 << index);
}

void JBImprov::handleHostname(JBImprovTransport& from, const uint8_t* data, uint8_t length) {
  if (length > 0) {
    if (!from.isAuthorized()) {
      from.setError(JBImprovError::NotAuthorized);
      return;
    }
    const char* hostname = (const char*)data;
    if (!jbImprovIsValidHostname(hostname, length)) {
      from.setError(JBImprovError::BadHostname);
      return;
    }
    char buffer[JBIMPROV_MAX_HOSTNAME_LENGTH + 1];
    copyString(buffer, sizeof(buffer), hostname, length);
    if (!_wifi->setHostname(buffer)) {
      from.setError(JBImprovError::BadHostname);
      return;
    }
    copyString(_hostname, sizeof(_hostname), buffer);
    from.authorizationUsed();
    if (_onHostnameChanged) {
      _onHostnameChanged(_hostname);
    }
  }
  JBImprovResultBuilder result;
  result.add(_hostname);
  from.sendResult((uint8_t)JBImprovCommand::Hostname, result.data(), result.length());
}

void JBImprov::handleDeviceName(JBImprovTransport& from, const uint8_t* data, uint8_t length) {
  if (length > 0) {
    if (!from.isAuthorized()) {
      from.setError(JBImprovError::NotAuthorized);
      return;
    }
    if (length > JBIMPROV_MAX_DEVICE_NAME_LENGTH || memchr(data, 0, length) != nullptr) {
      from.setError(JBImprovError::InvalidRpcPacket);
      return;
    }
    copyString(_deviceName, sizeof(_deviceName), (const char*)data, length);
    from.authorizationUsed();
    if (_onDeviceNameChanged) {
      _onDeviceNameChanged(_deviceName);
    }
  }
  JBImprovResultBuilder result;
  result.add(_deviceName);
  from.sendResult((uint8_t)JBImprovCommand::DeviceName, result.data(), result.length());
}

void JBImprov::handleNetworkState(JBImprovTransport& from) {
  bool online = _wifi->isConnected();
  JBImprovResultBuilder result;
  result.addNumber(JBIMPROV_NETWORK_WIFI | (online ? JBIMPROV_NETWORK_ONLINE : 0));
  if (online) {
    addRedirectUrl(result);
  }
  from.sendResult((uint8_t)JBImprovCommand::NetworkState, result.data(), result.length());
}

// --- Redirect URL ------------------------------------------------------------------

bool JBImprov::redirectUrl(char* buffer, size_t size) {
  if (size == 0) {
    return false;
  }
  buffer[0] = '\0';
  if (_redirectUrl == nullptr || _redirectUrl[0] == '\0') {
    return false;
  }
  char ip[16] = {0};
  if (!_wifi->localIP(ip, sizeof(ip))) {
    return false;
  }
  struct Placeholder {
    const char* name;
    const char* value;
  };
  const Placeholder placeholders[] = {
      {"{{ip_address}}", ip}, {"{{hostname}}", _hostname}, {"{{device_name}}", _deviceName}};

  size_t out = 0;
  const char* in = _redirectUrl;
  while (*in != '\0' && out < size - 1) {
    bool replaced = false;
    for (const Placeholder& placeholder : placeholders) {
      size_t nameLength = strlen(placeholder.name);
      if (strncmp(in, placeholder.name, nameLength) == 0) {
        size_t valueLength = strlen(placeholder.value);
        if (valueLength > size - 1 - out) {
          valueLength = size - 1 - out;
        }
        memcpy(&buffer[out], placeholder.value, valueLength);
        out += valueLength;
        in += nameLength;
        replaced = true;
        break;
      }
    }
    if (!replaced) {
      buffer[out++] = *in++;
    }
  }
  buffer[out] = '\0';
  return out > 0;
}

void JBImprov::addRedirectUrl(JBImprovResultBuilder& result) {
  char url[JBIMPROV_MAX_DATA_LENGTH];
  if (redirectUrl(url, sizeof(url))) {
    result.add(url);
  }
}

// --- loop() helpers ----------------------------------------------------------------

void JBImprov::loopProvisioning() {
  JBImprovWiFi::ConnectStatus status = _wifi->connectStatus();
  JBImprovTransport* requester = _provisionRequester;

  if (status == JBImprovWiFi::ConnectStatus::Connected) {
    _provisionRequester = nullptr;
    _disconnectedSince = 0;
    setState(JBImprovState::Provisioned);
    if (requester != nullptr) {
      JBImprovResultBuilder result;
      addRedirectUrl(result);
      requester->sendResult((uint8_t)JBImprovCommand::WifiSettings, result.data(),
                            result.length());
    }
    if (_onProvisioned) {
      _onProvisioned(_ssid, _password);
    }
    memset(_password, 0, sizeof(_password));
    return;
  }

  bool timedOut = (uint32_t)(millis() - _provisionStarted) > _connectTimeout;
  if (status == JBImprovWiFi::ConnectStatus::Failed || timedOut) {
    _wifi->cancelConnect();
    _provisionRequester = nullptr;
    memset(_password, 0, sizeof(_password));
    if (requester != nullptr) {
      requester->setError(JBImprovError::UnableToConnect);
      requester->authorizationUsed();
    }
    setState(JBImprovState::Ready);
  }
}

void JBImprov::loopConnection() {
  uint32_t now = millis();
  if ((uint32_t)(now - _lastConnectionCheck) < CONNECTION_CHECK_INTERVAL) {
    return;
  }
  _lastConnectionCheck = now;

  if (_wifi->isConnected()) {
    _disconnectedSince = 0;
    setState(JBImprovState::Provisioned);
  } else if (_state == JBImprovState::Provisioned) {
    // Ride out short drops before offering provisioning again.
    if (_disconnectedSince == 0) {
      _disconnectedSince = now == 0 ? 1 : now;
    } else if ((uint32_t)(now - _disconnectedSince) > DISCONNECT_GRACE_PERIOD) {
      _disconnectedSince = 0;
      setState(JBImprovState::Ready);
    }
  }
}

void JBImprov::loopScan() {
  int status = _wifi->scanStatus();
  if (status == JBIMPROV_SCAN_RUNNING) {
    return;
  }
  uint8_t requesters = _scanRequesters;
  _scanRequesters = 0;
  sendScanResults(requesters);
}

void JBImprov::sendScanResults(uint8_t transports) {
  // Collect unique, named networks, strongest first.
  JBImprovNetwork networks[JBIMPROV_MAX_SCAN_RESULTS];
  int count = 0;
  int found = _wifi->scanStatus();
  for (int i = 0; i < found; i++) {
    JBImprovNetwork network;
    if (!_wifi->scanResult(i, network) || network.ssid[0] == '\0') {
      continue;
    }
    if (network.auth == nullptr) {
      network.auth = "WPA2";
    }
    int existing = -1;
    for (int j = 0; j < count; j++) {
      if (strcmp(networks[j].ssid, network.ssid) == 0) {
        existing = j;
        break;
      }
    }
    if (existing >= 0) {
      if (network.rssi > networks[existing].rssi) {
        networks[existing] = network;
      }
      continue;
    }
    if (count < JBIMPROV_MAX_SCAN_RESULTS) {
      networks[count++] = network;
    } else {
      int weakest = 0;
      for (int j = 1; j < count; j++) {
        if (networks[j].rssi < networks[weakest].rssi) {
          weakest = j;
        }
      }
      if (network.rssi > networks[weakest].rssi) {
        networks[weakest] = network;
      }
    }
  }
  if (found > 0) {
    _wifi->scanDelete();
  }
  for (int i = 1; i < count; i++) {
    JBImprovNetwork network = networks[i];
    int j = i - 1;
    while (j >= 0 && networks[j].rssi < network.rssi) {
      networks[j + 1] = networks[j];
      j--;
    }
    networks[j + 1] = network;
  }

  const uint8_t command = (uint8_t)JBImprovCommand::ScanWifi;
  for (uint8_t t = 0; t < _transportCount; t++) {
    if ((transports & (1 << t)) == 0) {
      continue;
    }
    JBImprovTransport* transport = _transports[t];
    JBImprovResultBuilder result;
    if (transport->scanResultsPerNetwork()) {
      // Serial: one result per network, then an empty one.
      for (int i = 0; i < count; i++) {
        result.clear();
        result.add(networks[i].ssid);
        result.addNumber(networks[i].rssi);
        result.add(strcmp(networks[i].auth, "NO") == 0 ? "NO" : "YES");
        transport->sendResult(command, result.data(), result.length());
      }
      result.clear();
      transport->sendResult(command, result.data(), result.length());
    } else {
      // BLE: every network in a single result, as many as fit.
      for (int i = 0; i < count; i++) {
        char rssi[8];
        snprintf(rssi, sizeof(rssi), "%d", networks[i].rssi);
        size_t needed = 3 + strlen(networks[i].ssid) + strlen(rssi) + strlen(networks[i].auth);
        if (needed > result.remaining()) {
          break;
        }
        result.add(networks[i].ssid);
        result.add(rssi);
        result.add(networks[i].auth);
      }
      transport->sendResult(command, result.data(), result.length());
    }
  }
}
