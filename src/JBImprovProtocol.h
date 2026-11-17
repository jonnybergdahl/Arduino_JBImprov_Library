/**
 * @file JBImprovProtocol.h
 * @brief Transport independent parts of the Improv Wi-Fi protocol.
 *
 * Constants, checksums, the RPC string encoding and the serial frame codec. Nothing
 * in here depends on Arduino, so it can be unit tested on the host.
 *
 * Specifications:
 * - Serial: https://www.improv-wifi.com/serial/
 * - BLE: https://www.improv-wifi.com/ble/
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/// Serial protocol version this library speaks.
#define JBIMPROV_SERIAL_VERSION 1

/// Maximum length of the data part of an RPC command or result.
#define JBIMPROV_MAX_DATA_LENGTH 255

/// Maximum length of the strings in an RPC result. A serial frame carries at most 255
/// data bytes, two of which are the command and length of the result.
#define JBIMPROV_MAX_RESULT_LENGTH 253

/// Maximum SSID length in bytes (IEEE 802.11).
#define JBIMPROV_MAX_SSID_LENGTH 32

/// Maximum Wi-Fi password length in bytes (63 character passphrase or 64 hex digits).
#define JBIMPROV_MAX_PASSWORD_LENGTH 64

/// Maximum hostname length accepted by this library (one RFC 1123 label).
#define JBIMPROV_MAX_HOSTNAME_LENGTH 63

/// Maximum device name length accepted by this library.
#define JBIMPROV_MAX_DEVICE_NAME_LENGTH 64

/// Size of a serial frame with the largest possible payload: header, version, type,
/// length, data and checksum.
#define JBIMPROV_MAX_SERIAL_FRAME_LENGTH (6 + 3 + JBIMPROV_MAX_DATA_LENGTH + 1)

/// Provisioning state of the device.
enum class JBImprovState : uint8_t {
  Stopped = 0x00,                ///< Provisioning is unavailable.
  AuthorizationRequired = 0x01,  ///< Waiting for physical authorization (BLE only).
  Authorized = 0x02,             ///< Ready to accept credentials.
  Provisioning = 0x03,           ///< Credentials received, trying to connect.
  Provisioned = 0x04,            ///< Connected to the network.
  Ready = Authorized             ///< Name used by the serial specification.
};

/// Error state reported to the client.
enum class JBImprovError : uint8_t {
  None = 0x00,
  InvalidRpcPacket = 0x01,
  UnknownRpcCommand = 0x02,
  UnableToConnect = 0x03,
  NotAuthorized = 0x04,  ///< Command needs authorization first (BLE).
  BadHostname = 0x05,
  Unknown = 0xFF
};

/// RPC command identifiers.
enum class JBImprovCommand : uint8_t {
  WifiSettings = 0x01,
  RequestCurrentState = 0x02,  ///< Serial meaning of 0x02.
  Identify = 0x02,             ///< BLE meaning of 0x02.
  DeviceInfo = 0x03,
  ScanWifi = 0x04,
  Hostname = 0x05,
  DeviceName = 0x06,
  NetworkState = 0x07  ///< Serial only.
};

/// Serial packet types.
enum class JBImprovPacketType : uint8_t {
  CurrentState = 0x01,
  ErrorState = 0x02,
  RpcCommand = 0x03,
  RpcResult = 0x04
};

/// BLE capability flags.
enum JBImprovCapability : uint8_t {
  JBIMPROV_CAPABILITY_IDENTIFY = 0x01,
  JBIMPROV_CAPABILITY_DEVICE_INFO = 0x02,
  JBIMPROV_CAPABILITY_SCAN_WIFI = 0x04,
  JBIMPROV_CAPABILITY_HOSTNAME = 0x08
};

/// Flags returned by the network state command (0x07).
enum JBImprovNetworkFlag : uint8_t {
  JBIMPROV_NETWORK_ONLINE = 0x01,
  JBIMPROV_NETWORK_WIFI = 0x02,
  JBIMPROV_NETWORK_ETHERNET = 0x04,
  JBIMPROV_NETWORK_THREAD = 0x08,
  JBIMPROV_NETWORK_CELLULAR = 0x10
};

/**
 * @brief Return the Improv checksum: the sum of all bytes, keeping the low byte.
 */
uint8_t jbImprovChecksum(const uint8_t* data, size_t length, uint8_t initial = 0);

/**
 * @brief Check a hostname against RFC 1123: 1-63 letters, digits and hyphens, not
 * starting or ending with a hyphen.
 */
bool jbImprovIsValidHostname(const char* hostname, size_t length);

/**
 * @brief Builds the data part of an RPC result: a list of length prefixed strings.
 */
class JBImprovResultBuilder {
public:
  JBImprovResultBuilder() : _length(0) {}

  /// Remove all strings.
  void clear() { _length = 0; }

  /**
   * @brief Append a string.
   * @return false if it doesn't fit; the result is unchanged then.
   */
  bool add(const char* value);

  /// Append a string of a given length. @return false if it doesn't fit.
  bool add(const char* value, size_t length);

  /// Append an integer as a decimal string. @return false if it doesn't fit.
  bool addNumber(long value);

  /// Bytes left for more strings, including their length prefixes.
  size_t remaining() const { return JBIMPROV_MAX_RESULT_LENGTH - _length; }

  const uint8_t* data() const { return _data; }
  uint8_t length() const { return (uint8_t)_length; }

private:
  uint8_t _data[JBIMPROV_MAX_RESULT_LENGTH];
  size_t _length;
};

/**
 * @brief Reads length prefixed strings from RPC command data.
 */
class JBImprovStringReader {
public:
  JBImprovStringReader(const uint8_t* data, size_t length)
      : _data(data), _length(length), _position(0) {}

  /**
   * @brief Read the next string. The string is not null terminated.
   * @return false if there is no more data or the string is truncated.
   */
  bool next(const char*& value, size_t& length);

  /**
   * @brief Read the next string into a null terminated buffer.
   * @return false if there is no more data, the string is truncated or it doesn't
   * fit in @p size - 1 bytes.
   */
  bool next(char* buffer, size_t size);

  /// true when all data has been read.
  bool atEnd() const { return _position >= _length; }

private:
  const uint8_t* _data;
  size_t _length;
  size_t _position;
};

/**
 * @brief Encode a serial frame: "IMPROV", version, type, length, data, checksum.
 * @param out Buffer of at least @p length + 10 bytes.
 * @return Number of bytes written.
 */
size_t jbImprovEncodeSerialFrame(JBImprovPacketType type, const uint8_t* data, uint8_t length,
                                 uint8_t* out);

/**
 * @brief Encode an RPC result: command, data length, data, optionally a checksum (BLE).
 * @param out Buffer of at least @p length + 3 bytes.
 * @return Number of bytes written.
 */
size_t jbImprovEncodeRpcResult(uint8_t command, const uint8_t* data, uint8_t length,
                               bool addChecksum, uint8_t* out);

/**
 * @brief Incremental parser for Improv frames in a serial byte stream.
 *
 * Bytes that aren't part of a valid frame are skipped, so the same port can carry
 * log output and other data.
 */
class JBImprovSerialParser {
public:
  JBImprovSerialParser() { reset(); }

  /// Forget any partial frame.
  void reset();

  /**
   * @brief Process one received byte.
   * @return true when the byte completed a valid frame. Read it with type(),
   * data() and length() before the next call.
   */
  bool feed(uint8_t byte);

  /// true while a frame has been started but not finished.
  bool inFrame() const { return _position > 0; }

  JBImprovPacketType type() const { return (JBImprovPacketType)_type; }
  const uint8_t* data() const { return _data; }
  uint8_t length() const { return _length; }

private:
  bool restart(uint8_t byte);

  size_t _position;
  uint8_t _type;
  uint8_t _length;
  uint8_t _checksum;
  uint8_t _data[JBIMPROV_MAX_DATA_LENGTH];
};
