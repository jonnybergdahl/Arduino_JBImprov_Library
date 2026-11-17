#include <string.h>
#include <unity.h>

#include "JBImprovProtocol.h"

// JBImprov.cpp is part of the build and needs a clock.
unsigned long millis() { return 0; }

void setUp() {}
void tearDown() {}

static const uint8_t DEVICE_INFO_FRAME[] = {'I', 'M', 'P', 'R', 'O', 'V', 0x01, 0x03,
                                            0x02, 0x03, 0x00, 0xE6};

static void test_checksum() {
  const uint8_t a[] = {0x03, 0x00};
  const uint8_t b[] = {0xFF, 0x02};
  TEST_ASSERT_EQUAL_UINT8(3, jbImprovChecksum(a, sizeof(a)));
  TEST_ASSERT_EQUAL_UINT8(1, jbImprovChecksum(b, sizeof(b)));
}

static void test_encode_serial_frame() {
  // Same vector as the Python client: request device info.
  const uint8_t rpc[] = {0x03, 0x00};
  uint8_t out[16];
  size_t length = jbImprovEncodeSerialFrame(JBImprovPacketType::RpcCommand, rpc, 2, out);
  TEST_ASSERT_EQUAL(sizeof(DEVICE_INFO_FRAME), length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(DEVICE_INFO_FRAME, out, length);
}

static void test_encode_rpc_result() {
  const uint8_t strings[] = {3, 'a', 'b', 'c'};
  uint8_t out[16];
  size_t length = jbImprovEncodeRpcResult(0x05, strings, 4, false, out);
  const uint8_t expected[] = {0x05, 0x04, 3, 'a', 'b', 'c'};
  TEST_ASSERT_EQUAL(sizeof(expected), length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, length);

  length = jbImprovEncodeRpcResult(0x05, strings, 4, true, out);
  TEST_ASSERT_EQUAL(sizeof(expected) + 1, length);
  TEST_ASSERT_EQUAL_UINT8(jbImprovChecksum(expected, sizeof(expected)), out[length - 1]);
}

static int feedAll(JBImprovSerialParser& parser, const uint8_t* data, size_t length) {
  int frames = 0;
  for (size_t i = 0; i < length; i++) {
    if (parser.feed(data[i])) {
      frames++;
    }
  }
  return frames;
}

static void test_parser_finds_frame_in_log_output() {
  JBImprovSerialParser parser;
  const char* log = "[I][main]: IMPROVISING hello\nIMP";
  TEST_ASSERT_EQUAL(0, feedAll(parser, (const uint8_t*)log, strlen(log)));
  TEST_ASSERT_EQUAL(1, feedAll(parser, DEVICE_INFO_FRAME, sizeof(DEVICE_INFO_FRAME)));
  TEST_ASSERT_EQUAL(JBImprovPacketType::RpcCommand, parser.type());
  TEST_ASSERT_EQUAL(2, parser.length());
  TEST_ASSERT_EQUAL_UINT8(0x03, parser.data()[0]);
  TEST_ASSERT_EQUAL_UINT8(0x00, parser.data()[1]);
}

static void test_parser_frames_back_to_back() {
  JBImprovSerialParser parser;
  uint8_t data[sizeof(DEVICE_INFO_FRAME) * 2 + 1];
  memcpy(data, DEVICE_INFO_FRAME, sizeof(DEVICE_INFO_FRAME));
  data[sizeof(DEVICE_INFO_FRAME)] = '\n';
  memcpy(&data[sizeof(DEVICE_INFO_FRAME) + 1], DEVICE_INFO_FRAME, sizeof(DEVICE_INFO_FRAME));
  TEST_ASSERT_EQUAL(2, feedAll(parser, data, sizeof(data)));
}

static void test_parser_rejects_bad_checksum_and_version() {
  JBImprovSerialParser parser;
  uint8_t bad[sizeof(DEVICE_INFO_FRAME)];
  memcpy(bad, DEVICE_INFO_FRAME, sizeof(bad));
  bad[sizeof(bad) - 1] ^= 0x01;
  TEST_ASSERT_EQUAL(0, feedAll(parser, bad, sizeof(bad)));
  TEST_ASSERT_FALSE(parser.inFrame());

  memcpy(bad, DEVICE_INFO_FRAME, sizeof(bad));
  bad[6] = 2;
  TEST_ASSERT_EQUAL(0, feedAll(parser, bad, sizeof(bad)));

  // Still in sync afterwards.
  TEST_ASSERT_EQUAL(1, feedAll(parser, DEVICE_INFO_FRAME, sizeof(DEVICE_INFO_FRAME)));
}

static void test_parser_max_length_frame() {
  uint8_t data[JBIMPROV_MAX_DATA_LENGTH];
  for (size_t i = 0; i < sizeof(data); i++) {
    data[i] = (uint8_t)i;
  }
  uint8_t frame[JBIMPROV_MAX_SERIAL_FRAME_LENGTH];
  size_t length =
      jbImprovEncodeSerialFrame(JBImprovPacketType::RpcCommand, data, sizeof(data), frame);
  TEST_ASSERT_EQUAL(JBIMPROV_MAX_SERIAL_FRAME_LENGTH, length);
  JBImprovSerialParser parser;
  TEST_ASSERT_EQUAL(1, feedAll(parser, frame, length));
  TEST_ASSERT_EQUAL(255, parser.length());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(data, parser.data(), sizeof(data));
}

static void test_result_builder() {
  JBImprovResultBuilder result;
  TEST_ASSERT_TRUE(result.add("abc"));
  TEST_ASSERT_TRUE(result.add(""));
  TEST_ASSERT_TRUE(result.addNumber(-60));
  const uint8_t expected[] = {3, 'a', 'b', 'c', 0, 3, '-', '6', '0'};
  TEST_ASSERT_EQUAL(sizeof(expected), result.length());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, result.data(), sizeof(expected));
}

static void test_result_builder_overflow() {
  JBImprovResultBuilder result;
  char big[200];
  memset(big, 'x', sizeof(big));
  TEST_ASSERT_TRUE(result.add(big, 200));
  TEST_ASSERT_EQUAL(201, result.length());
  TEST_ASSERT_FALSE(result.add(big, 52));  // 53 bytes, only 52 left
  TEST_ASSERT_EQUAL(201, result.length());
  TEST_ASSERT_TRUE(result.add(big, 51));
  TEST_ASSERT_EQUAL(JBIMPROV_MAX_RESULT_LENGTH, result.length());
  TEST_ASSERT_EQUAL(0, result.remaining());
}

static void test_string_reader() {
  const uint8_t data[] = {2, 'a', 'b', 0, 1, 'c'};
  JBImprovStringReader reader(data, sizeof(data));
  char buffer[4];
  TEST_ASSERT_TRUE(reader.next(buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_STRING("ab", buffer);
  TEST_ASSERT_TRUE(reader.next(buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_STRING("", buffer);
  TEST_ASSERT_TRUE(reader.next(buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_STRING("c", buffer);
  TEST_ASSERT_TRUE(reader.atEnd());
  TEST_ASSERT_FALSE(reader.next(buffer, sizeof(buffer)));
}

static void test_string_reader_rejects_truncated_and_too_long() {
  const uint8_t truncated[] = {5, 'a', 'b'};
  JBImprovStringReader reader(truncated, sizeof(truncated));
  char buffer[8];
  TEST_ASSERT_FALSE(reader.next(buffer, sizeof(buffer)));

  const uint8_t tooLong[] = {4, 'a', 'b', 'c', 'd'};
  JBImprovStringReader reader2(tooLong, sizeof(tooLong));
  TEST_ASSERT_FALSE(reader2.next(buffer, 4));
}

static void test_hostname_validation() {
  TEST_ASSERT_TRUE(jbImprovIsValidHostname("my-device-01", 12));
  TEST_ASSERT_FALSE(jbImprovIsValidHostname("", 0));
  TEST_ASSERT_FALSE(jbImprovIsValidHostname("-abc", 4));
  TEST_ASSERT_FALSE(jbImprovIsValidHostname("abc-", 4));
  TEST_ASSERT_FALSE(jbImprovIsValidHostname("my_device", 9));
  TEST_ASSERT_FALSE(jbImprovIsValidHostname("my device", 9));
  char longName[65];
  memset(longName, 'a', 64);
  longName[64] = '\0';
  TEST_ASSERT_TRUE(jbImprovIsValidHostname(longName, 63));
  TEST_ASSERT_FALSE(jbImprovIsValidHostname(longName, 64));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_checksum);
  RUN_TEST(test_encode_serial_frame);
  RUN_TEST(test_encode_rpc_result);
  RUN_TEST(test_parser_finds_frame_in_log_output);
  RUN_TEST(test_parser_frames_back_to_back);
  RUN_TEST(test_parser_rejects_bad_checksum_and_version);
  RUN_TEST(test_parser_max_length_frame);
  RUN_TEST(test_result_builder);
  RUN_TEST(test_result_builder_overflow);
  RUN_TEST(test_string_reader);
  RUN_TEST(test_string_reader_rejects_truncated_and_too_long);
  RUN_TEST(test_hostname_validation);
  return UNITY_END();
}
