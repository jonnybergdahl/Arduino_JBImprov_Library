#include "JBImprovProtocol.h"

#include <stdio.h>
#include <string.h>

static const uint8_t SERIAL_HEADER[] = {'I', 'M', 'P', 'R', 'O', 'V'};
static const size_t SERIAL_HEADER_LENGTH = sizeof(SERIAL_HEADER);

uint8_t jbImprovChecksum(const uint8_t* data, size_t length, uint8_t initial) {
  uint8_t sum = initial;
  for (size_t i = 0; i < length; i++) {
    sum += data[i];
  }
  return sum;
}

bool jbImprovIsValidHostname(const char* hostname, size_t length) {
  if (hostname == nullptr || length == 0 || length > JBIMPROV_MAX_HOSTNAME_LENGTH) {
    return false;
  }
  if (hostname[0] == '-' || hostname[length - 1] == '-') {
    return false;
  }
  for (size_t i = 0; i < length; i++) {
    char c = hostname[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '-';
    if (!ok) {
      return false;
    }
  }
  return true;
}

// --- JBImprovResultBuilder ---------------------------------------------------------

bool JBImprovResultBuilder::add(const char* value) {
  return add(value, value == nullptr ? 0 : strlen(value));
}

bool JBImprovResultBuilder::add(const char* value, size_t length) {
  if (length > 255 || length + 1 > remaining()) {
    return false;
  }
  _data[_length++] = (uint8_t)length;
  if (length > 0) {
    memcpy(&_data[_length], value, length);
    _length += length;
  }
  return true;
}

bool JBImprovResultBuilder::addNumber(long value) {
  char buffer[24];
  int length = snprintf(buffer, sizeof(buffer), "%ld", value);
  return length > 0 && add(buffer, (size_t)length);
}

// --- JBImprovStringReader ----------------------------------------------------------

bool JBImprovStringReader::next(const char*& value, size_t& length) {
  if (_position >= _length) {
    return false;
  }
  size_t stringLength = _data[_position];
  if (_position + 1 + stringLength > _length) {
    return false;
  }
  value = (const char*)&_data[_position + 1];
  length = stringLength;
  _position += 1 + stringLength;
  return true;
}

bool JBImprovStringReader::next(char* buffer, size_t size) {
  const char* value;
  size_t length;
  if (size == 0 || !next(value, length) || length >= size) {
    return false;
  }
  memcpy(buffer, value, length);
  buffer[length] = '\0';
  return true;
}

// --- Encoding ----------------------------------------------------------------------

size_t jbImprovEncodeSerialFrame(JBImprovPacketType type, const uint8_t* data, uint8_t length,
                                 uint8_t* out) {
  size_t position = 0;
  memcpy(out, SERIAL_HEADER, SERIAL_HEADER_LENGTH);
  position += SERIAL_HEADER_LENGTH;
  out[position++] = JBIMPROV_SERIAL_VERSION;
  out[position++] = (uint8_t)type;
  out[position++] = length;
  if (length > 0) {
    memcpy(&out[position], data, length);
    position += length;
  }
  out[position] = jbImprovChecksum(out, position);
  return position + 1;
}

size_t jbImprovEncodeRpcResult(uint8_t command, const uint8_t* data, uint8_t length,
                               bool addChecksum, uint8_t* out) {
  out[0] = command;
  out[1] = length;
  if (length > 0) {
    memcpy(&out[2], data, length);
  }
  size_t position = 2 + (size_t)length;
  if (addChecksum) {
    out[position] = jbImprovChecksum(out, position);
    position++;
  }
  return position;
}

// --- JBImprovSerialParser ----------------------------------------------------------

void JBImprovSerialParser::reset() {
  _position = 0;
  _checksum = 0;
}

bool JBImprovSerialParser::restart(uint8_t byte) {
  // The byte that broke the frame may be the start of the next one.
  reset();
  if (byte == SERIAL_HEADER[0]) {
    _position = 1;
    _checksum = byte;
  }
  return false;
}

bool JBImprovSerialParser::feed(uint8_t byte) {
  if (_position < SERIAL_HEADER_LENGTH) {
    if (byte != SERIAL_HEADER[_position]) {
      return restart(byte);
    }
  } else if (_position == SERIAL_HEADER_LENGTH) {
    if (byte != JBIMPROV_SERIAL_VERSION) {
      return restart(byte);
    }
  } else if (_position == SERIAL_HEADER_LENGTH + 1) {
    _type = byte;
  } else if (_position == SERIAL_HEADER_LENGTH + 2) {
    _length = byte;
  } else if (_position < SERIAL_HEADER_LENGTH + 3 + (size_t)_length) {
    _data[_position - SERIAL_HEADER_LENGTH - 3] = byte;
  } else {
    // Checksum byte.
    if (byte != _checksum) {
      return restart(byte);
    }
    reset();
    return true;
  }
  _checksum += byte;
  _position++;
  return false;
}
