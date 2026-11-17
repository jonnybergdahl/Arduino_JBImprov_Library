#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "JBImprov.h"

// --- Test doubles --------------------------------------------------------------------

static unsigned long fakeNow = 1000;
unsigned long millis() { return fakeNow; }

class FakeWiFi : public JBImprovWiFi {
public:
  bool connected = false;
  ConnectStatus status = ConnectStatus::Connecting;
  bool connectResult = true;
  std::string ssid, password, connectHostname;
  int connectCalls = 0;
  int cancelCalls = 0;

  bool scanStartResult = true;
  int scan = JBIMPROV_SCAN_FAILED;
  int scanStarts = 0;
  std::vector<JBImprovNetwork> networks;

  bool acceptHostname = true;
  std::string hostname;

  bool isConnected() override { return connected; }
  bool connect(const char* s, const char* p, const char* h) override {
    ssid = s;
    password = p;
    connectHostname = h;
    connectCalls++;
    return connectResult;
  }
  ConnectStatus connectStatus() override { return status; }
  void cancelConnect() override { cancelCalls++; }
  bool localIP(char* buffer, size_t size) override {
    if (!connected) {
      return false;
    }
    snprintf(buffer, size, "192.168.1.10");
    return true;
  }
  bool startScan() override {
    scanStarts++;
    if (scanStartResult) {
      scan = JBIMPROV_SCAN_RUNNING;
    }
    return scanStartResult;
  }
  int scanStatus() override { return scan; }
  bool scanResult(int index, JBImprovNetwork& network) override {
    if (index < 0 || index >= (int)networks.size()) {
      return false;
    }
    network = networks[index];
    return true;
  }
  void scanDelete() override { scan = JBIMPROV_SCAN_FAILED; }
  bool setHostname(const char* h) override {
    if (!acceptHostname) {
      return false;
    }
    hostname = h;
    return true;
  }
  bool defaultHostname(char* buffer, size_t size) override {
    snprintf(buffer, size, "esp-default");
    return true;
  }
  const char* hardware() override { return "TestChip"; }
  const char* osName() override { return "TestOS"; }
  const char* osVersion() override { return "1.2"; }

  void addNetwork(const char* name, int rssi, const char* auth) {
    JBImprovNetwork network;
    snprintf(network.ssid, sizeof(network.ssid), "%s", name);
    network.rssi = rssi;
    network.auth = auth;
    networks.push_back(network);
  }
};

struct Sent {
  char kind;  // 'S' state, 'E' error, 'R' result
  uint8_t value;
  std::vector<std::string> strings;
};

class FakeTransport : public JBImprovTransport {
public:
  std::vector<Sent> sent;
  bool authorized = true;
  bool perNetwork = false;
  int authorizationUses = 0;
  int begins = 0;

  void receive(uint8_t command, const std::vector<uint8_t>& data = {}) {
    handleCommand(command, data.data(), (uint8_t)data.size());
  }

  const Sent* last(char kind) const {
    for (auto it = sent.rbegin(); it != sent.rend(); ++it) {
      if (it->kind == kind) {
        return &*it;
      }
    }
    return nullptr;
  }

  size_t count(char kind) const {
    size_t n = 0;
    for (const Sent& s : sent) {
      n += s.kind == kind;
    }
    return n;
  }

protected:
  void begin() override { begins++; }
  void sendState(JBImprovState state) override { sent.push_back({'S', (uint8_t)state, {}}); }
  void sendError(JBImprovError error) override { sent.push_back({'E', (uint8_t)error, {}}); }
  void sendResult(uint8_t command, const uint8_t* data, uint8_t length) override {
    Sent s{'R', command, {}};
    JBImprovStringReader reader(data, length);
    const char* value;
    size_t valueLength;
    while (reader.next(value, valueLength)) {
      s.strings.push_back(std::string(value, valueLength));
    }
    TEST_ASSERT_TRUE_MESSAGE(reader.atEnd(), "Malformed result");
    sent.push_back(s);
  }
  bool isAuthorized() override { return authorized; }
  void authorizationUsed() override { authorizationUses++; }
  bool scanResultsPerNetwork() override { return perNetwork; }
};

static std::vector<uint8_t> strings(std::initializer_list<const char*> values) {
  std::vector<uint8_t> data;
  for (const char* value : values) {
    data.push_back((uint8_t)strlen(value));
    data.insert(data.end(), value, value + strlen(value));
  }
  return data;
}

static std::vector<uint8_t> raw(const char* value) {
  return std::vector<uint8_t>(value, value + strlen(value));
}

static FakeWiFi* wifi;
static FakeTransport* transport;
static JBImprov* improv;

void setUp() {
  fakeNow = 1000;
  wifi = new FakeWiFi();
  transport = new FakeTransport();
  improv = new JBImprov(*wifi);
  improv->setFirmware("TestFirmware", "0.1.0");
  improv->addTransport(*transport);
}

void tearDown() {
  delete improv;
  delete transport;
  delete wifi;
}

static void advance(unsigned long milliseconds) {
  fakeNow += milliseconds;
  improv->loop();
}

static void assertStrings(const Sent* sent, std::initializer_list<const char*> expected) {
  TEST_ASSERT_NOT_NULL(sent);
  TEST_ASSERT_EQUAL(expected.size(), sent->strings.size());
  size_t i = 0;
  for (const char* value : expected) {
    TEST_ASSERT_EQUAL_STRING(value, sent->strings[i++].c_str());
  }
}

// --- Tests ---------------------------------------------------------------------------

static void test_begin_defaults() {
  improv->begin();
  TEST_ASSERT_EQUAL(1, transport->begins);
  TEST_ASSERT_EQUAL_STRING("esp-default", improv->hostname());
  TEST_ASSERT_EQUAL_STRING("esp-default", improv->deviceName());
  TEST_ASSERT_EQUAL(JBImprovState::Ready, improv->state());
}

static void test_begin_when_already_connected() {
  wifi->connected = true;
  improv->begin();
  TEST_ASSERT_EQUAL(JBImprovState::Provisioned, improv->state());
}

static void test_device_info() {
  improv->setDeviceName("Kitchen");
  improv->begin();
  transport->receive(0x03);
  const Sent* result = transport->last('R');
  TEST_ASSERT_EQUAL_UINT8(0x03, result->value);
  assertStrings(result, {"TestFirmware", "0.1.0", "TestChip", "Kitchen", "TestOS", "1.2"});

  improv->setHardware("CustomBoard");
  transport->receive(0x03);
  TEST_ASSERT_EQUAL_STRING("CustomBoard", transport->last('R')->strings[2].c_str());
}

static void test_provisioning_success() {
  std::string provisionedSsid, provisionedPassword;
  improv->setRedirectUrl("http://{{ip_address}}/?name={{hostname}}");
  improv->onProvisioned([&](const char* ssid, const char* password) {
    provisionedSsid = ssid;
    provisionedPassword = password;
  });
  improv->begin();

  transport->receive(0x01, strings({"MyWirelessAP", "mysecurepassword"}));
  TEST_ASSERT_EQUAL(JBImprovState::Provisioning, improv->state());
  TEST_ASSERT_EQUAL_UINT8((uint8_t)JBImprovState::Provisioning, transport->last('S')->value);
  TEST_ASSERT_EQUAL(1, wifi->connectCalls);
  TEST_ASSERT_EQUAL_STRING("MyWirelessAP", wifi->ssid.c_str());
  TEST_ASSERT_EQUAL_STRING("mysecurepassword", wifi->password.c_str());
  TEST_ASSERT_EQUAL_STRING("esp-default", wifi->connectHostname.c_str());

  advance(100);
  TEST_ASSERT_EQUAL(JBImprovState::Provisioning, improv->state());
  TEST_ASSERT_NULL(transport->last('R'));

  wifi->connected = true;
  wifi->status = JBImprovWiFi::ConnectStatus::Connected;
  advance(100);
  TEST_ASSERT_EQUAL(JBImprovState::Provisioned, improv->state());
  TEST_ASSERT_EQUAL_UINT8((uint8_t)JBImprovState::Provisioned, transport->last('S')->value);
  const Sent* result = transport->last('R');
  TEST_ASSERT_EQUAL_UINT8(0x01, result->value);
  assertStrings(result, {"http://192.168.1.10/?name=esp-default"});
  TEST_ASSERT_EQUAL_STRING("MyWirelessAP", provisionedSsid.c_str());
  TEST_ASSERT_EQUAL_STRING("mysecurepassword", provisionedPassword.c_str());
  TEST_ASSERT_EQUAL(0, transport->count('E'));  // Error was already None
}

static void test_provisioning_without_redirect_url_sends_empty_result() {
  improv->begin();
  transport->receive(0x01, strings({"ap", ""}));
  TEST_ASSERT_EQUAL_STRING("", wifi->password.c_str());
  wifi->connected = true;
  wifi->status = JBImprovWiFi::ConnectStatus::Connected;
  advance(10);
  assertStrings(transport->last('R'), {});
}

static void test_provisioning_timeout() {
  improv->setConnectTimeout(5000);
  improv->begin();
  transport->receive(0x01, strings({"ap", "wrong"}));
  advance(4000);
  TEST_ASSERT_EQUAL(JBImprovState::Provisioning, improv->state());
  advance(2000);
  TEST_ASSERT_EQUAL(JBImprovState::Ready, improv->state());
  TEST_ASSERT_EQUAL(1, wifi->cancelCalls);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)JBImprovError::UnableToConnect, transport->last('E')->value);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)JBImprovState::Ready, transport->last('S')->value);
  TEST_ASSERT_NULL(transport->last('R'));

  // The next command clears the error first.
  transport->receive(0x03);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)JBImprovError::None, transport->last('E')->value);
}

static void test_provisioning_failure_reported_by_wifi() {
  improv->begin();
  transport->receive(0x01, strings({"ap", "wrong"}));
  wifi->status = JBImprovWiFi::ConnectStatus::Failed;
  advance(10);
  TEST_ASSERT_EQUAL(JBImprovState::Ready, improv->state());
  TEST_ASSERT_EQUAL_UINT8((uint8_t)JBImprovError::UnableToConnect, transport->last('E')->value);
}

static void test_provisioning_connect_refused() {
  wifi->connectResult = false;
  improv->begin();
  transport->receive(0x01, strings({"ap", "pw"}));
  advance(1);
  TEST_ASSERT_EQUAL(JBImprovState::Ready, improv->state());
  TEST_ASSERT_EQUAL_UINT8((uint8_t)JBImprovError::UnableToConnect, transport->last('E')->value);
}

static void test_wifi_settings_not_authorized() {
  transport->authorized = false;
  improv->begin();
  transport->receive(0x01, strings({"ap", "pw"}));
  TEST_ASSERT_EQUAL(0, wifi->connectCalls);
  TEST_ASSERT_EQUAL(JBImprovState::Ready, improv->state());
  TEST_ASSERT_EQUAL_UINT8((uint8_t)JBImprovError::NotAuthorized, transport->last('E')->value);
}

static void test_wifi_settings_invalid() {
  improv->begin();
  std::vector<uint8_t> truncated = strings({"ap"});
  truncated.push_back(10);  // Password length without the password.
  transport->receive(0x01, truncated);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)JBImprovError::InvalidRpcPacket, transport->last('E')->value);

  transport->receive(0x01, strings({"", "pw"}));  // Empty SSID
  TEST_ASSERT_EQUAL_UINT8((uint8_t)JBImprovError::InvalidRpcPacket, transport->last('E')->value);

  char longSsid[34];
  memset(longSsid, 's', 33);
  longSsid[33] = '\0';
  transport->receive(0x01, strings({longSsid, "pw"}));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)JBImprovError::InvalidRpcPacket, transport->last('E')->value);
  TEST_ASSERT_EQUAL(0, wifi->connectCalls);
}

static void test_unknown_command() {
  improv->begin();
  transport->receive(0x42);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)JBImprovError::UnknownRpcCommand, transport->last('E')->value);
  // 0x02 means different things per transport; the fake doesn't handle it.
  transport->receive(0x02);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)JBImprovError::UnknownRpcCommand, transport->last('E')->value);
}

static void test_scan_combined_result() {
  wifi->addNetwork("Weak", -90, "WPA2");
  wifi->addNetwork("Strong", -40, "WPA/WPA2");
  wifi->addNetwork("", -30, "NO");        // Hidden network, skipped
  wifi->addNetwork("Weak", -70, "WPA2");  // Same SSID, stronger
  wifi->addNetwork("Open", -60, "NO");
  improv->begin();

  transport->receive(0x04);
  TEST_ASSERT_EQUAL(1, wifi->scanStarts);
  advance(10);
  TEST_ASSERT_NULL(transport->last('R'));

  wifi->scan = (int)wifi->networks.size();
  advance(10);
  const Sent* result = transport->last('R');
  TEST_ASSERT_EQUAL_UINT8(0x04, result->value);
  assertStrings(result, {"Strong", "-40", "WPA/WPA2", "Open", "-60", "NO", "Weak", "-70", "WPA2"});
  TEST_ASSERT_EQUAL(1, transport->count('R'));
}

static void test_scan_per_network_results() {
  transport->perNetwork = true;
  wifi->addNetwork("Secure", -50, "WPA2");
  wifi->addNetwork("Open", -60, "NO");
  improv->begin();
  transport->receive(0x04);
  wifi->scan = 2;
  advance(10);
  TEST_ASSERT_EQUAL(3, transport->count('R'));
  assertStrings(&transport->sent[transport->sent.size() - 3], {"Secure", "-50", "YES"});
  assertStrings(&transport->sent[transport->sent.size() - 2], {"Open", "-60", "NO"});
  assertStrings(&transport->sent[transport->sent.size() - 1], {});
}

static void test_scan_combined_result_is_limited_to_one_result() {
  char name[33];
  for (int i = 0; i < 20; i++) {
    snprintf(name, sizeof(name), "Network-with-a-long-name-%02d", i);
    wifi->addNetwork(name, -40 - i, "WPA2");
  }
  improv->begin();
  transport->receive(0x04);
  wifi->scan = 20;
  advance(10);
  const Sent* result = transport->last('R');
  TEST_ASSERT_EQUAL(0, result->strings.size() % 3);
  TEST_ASSERT_TRUE(result->strings.size() >= 3);
  TEST_ASSERT_TRUE(result->strings.size() < 60);
  TEST_ASSERT_EQUAL_STRING("Network-with-a-long-name-00", result->strings[0].c_str());
}

static void test_scan_start_failure_reports_no_networks() {
  wifi->scanStartResult = false;
  improv->begin();
  transport->receive(0x04);
  assertStrings(transport->last('R'), {});
}

static void test_hostname() {
  std::string changed;
  improv->onHostnameChanged([&](const char* name) { changed = name; });
  improv->begin();

  transport->receive(0x05);
  assertStrings(transport->last('R'), {"esp-default"});

  transport->receive(0x05, raw("kitchen-sensor"));
  assertStrings(transport->last('R'), {"kitchen-sensor"});
  TEST_ASSERT_EQUAL_STRING("kitchen-sensor", improv->hostname());
  TEST_ASSERT_EQUAL_STRING("kitchen-sensor", wifi->hostname.c_str());
  TEST_ASSERT_EQUAL_STRING("kitchen-sensor", changed.c_str());
  TEST_ASSERT_EQUAL(1, transport->authorizationUses);

  size_t results = transport->count('R');
  transport->receive(0x05, raw("bad_name"));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)JBImprovError::BadHostname, transport->last('E')->value);
  TEST_ASSERT_EQUAL(results, transport->count('R'));

  wifi->acceptHostname = false;
  transport->receive(0x05, raw("valid-but-refused"));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)JBImprovError::BadHostname, transport->last('E')->value);
  TEST_ASSERT_EQUAL_STRING("kitchen-sensor", improv->hostname());

  transport->authorized = false;
  transport->receive(0x05, raw("other"));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)JBImprovError::NotAuthorized, transport->last('E')->value);
  transport->receive(0x05);  // Getting is allowed
  assertStrings(transport->last('R'), {"kitchen-sensor"});
}

static void test_set_hostname_api() {
  TEST_ASSERT_TRUE(improv->setHostname("my-host"));
  TEST_ASSERT_FALSE(improv->setHostname("not valid"));
  improv->begin();
  TEST_ASSERT_EQUAL_STRING("my-host", improv->hostname());
  TEST_ASSERT_EQUAL_STRING("my-host", improv->deviceName());
}

static void test_device_name() {
  std::string changed;
  improv->onDeviceNameChanged([&](const char* name) { changed = name; });
  improv->begin();

  transport->receive(0x06, raw("Living Room Lamp"));
  assertStrings(transport->last('R'), {"Living Room Lamp"});
  TEST_ASSERT_EQUAL_STRING("Living Room Lamp", improv->deviceName());
  TEST_ASSERT_EQUAL_STRING("Living Room Lamp", changed.c_str());

  transport->receive(0x06);
  assertStrings(transport->last('R'), {"Living Room Lamp"});

  transport->authorized = false;
  transport->receive(0x06, raw("Other"));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)JBImprovError::NotAuthorized, transport->last('E')->value);
}

static void test_network_state() {
  improv->setRedirectUrl("http://{{ip_address}}");
  improv->begin();
  transport->receive(0x07);
  assertStrings(transport->last('R'), {"2"});

  wifi->connected = true;
  transport->receive(0x07);
  assertStrings(transport->last('R'), {"3", "http://192.168.1.10"});
}

static void test_connection_monitoring() {
  std::vector<JBImprovState> states;
  improv->onStateChanged([&](JBImprovState state) { states.push_back(state); });
  improv->begin();

  // Connected by the application, e.g. WiFi.begin() with stored credentials.
  wifi->connected = true;
  advance(1500);
  TEST_ASSERT_EQUAL(JBImprovState::Provisioned, improv->state());

  // Short drops are ignored.
  wifi->connected = false;
  advance(1500);
  advance(5000);
  TEST_ASSERT_EQUAL(JBImprovState::Provisioned, improv->state());
  wifi->connected = true;
  advance(1500);

  wifi->connected = false;
  advance(1500);
  advance(6000);
  advance(6000);
  TEST_ASSERT_EQUAL(JBImprovState::Ready, improv->state());

  TEST_ASSERT_EQUAL(2, states.size());
  TEST_ASSERT_EQUAL(JBImprovState::Provisioned, states[0]);
  TEST_ASSERT_EQUAL(JBImprovState::Ready, states[1]);
}

static void test_state_goes_to_all_transports_results_to_requester() {
  FakeTransport other;
  improv->addTransport(other);
  improv->begin();
  TEST_ASSERT_EQUAL(1, other.begins);

  transport->receive(0x01, strings({"ap", "pw"}));
  TEST_ASSERT_EQUAL_UINT8((uint8_t)JBImprovState::Provisioning, other.last('S')->value);
  wifi->connected = true;
  wifi->status = JBImprovWiFi::ConnectStatus::Connected;
  advance(10);
  TEST_ASSERT_EQUAL_UINT8((uint8_t)JBImprovState::Provisioned, other.last('S')->value);
  TEST_ASSERT_NOT_NULL(transport->last('R'));
  TEST_ASSERT_NULL(other.last('R'));

  // Both asking for a scan share one scan.
  transport->receive(0x04);
  other.receive(0x04);
  TEST_ASSERT_EQUAL(1, wifi->scanStarts);
  wifi->scan = 0;
  advance(10);
  TEST_ASSERT_EQUAL_UINT8(0x04, transport->last('R')->value);
  TEST_ASSERT_EQUAL_UINT8(0x04, other.last('R')->value);
}

static void test_capabilities() {
  TEST_ASSERT_EQUAL_HEX8(0x0E, improv->capabilities());
  improv->onIdentify([]() {});
  TEST_ASSERT_EQUAL_HEX8(0x0F, improv->capabilities());
}

static void test_max_transports() {
  FakeTransport a, b, c;
  TEST_ASSERT_TRUE(improv->addTransport(a));
  TEST_ASSERT_TRUE(improv->addTransport(b));
  TEST_ASSERT_FALSE(improv->addTransport(c));
  TEST_ASSERT_TRUE(improv->addTransport(*transport));  // Already added
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_begin_defaults);
  RUN_TEST(test_begin_when_already_connected);
  RUN_TEST(test_device_info);
  RUN_TEST(test_provisioning_success);
  RUN_TEST(test_provisioning_without_redirect_url_sends_empty_result);
  RUN_TEST(test_provisioning_timeout);
  RUN_TEST(test_provisioning_failure_reported_by_wifi);
  RUN_TEST(test_provisioning_connect_refused);
  RUN_TEST(test_wifi_settings_not_authorized);
  RUN_TEST(test_wifi_settings_invalid);
  RUN_TEST(test_unknown_command);
  RUN_TEST(test_scan_combined_result);
  RUN_TEST(test_scan_per_network_results);
  RUN_TEST(test_scan_combined_result_is_limited_to_one_result);
  RUN_TEST(test_scan_start_failure_reports_no_networks);
  RUN_TEST(test_hostname);
  RUN_TEST(test_set_hostname_api);
  RUN_TEST(test_device_name);
  RUN_TEST(test_network_state);
  RUN_TEST(test_connection_monitoring);
  RUN_TEST(test_state_goes_to_all_transports_results_to_requester);
  RUN_TEST(test_capabilities);
  RUN_TEST(test_max_transports);
  return UNITY_END();
}
