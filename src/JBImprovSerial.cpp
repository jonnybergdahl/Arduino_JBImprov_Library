#if defined(ARDUINO)

#include "JBImprovSerial.h"

/// Bytes read per loop() call, so a busy port can't starve the application.
static const int MAX_BYTES_PER_LOOP = 512;

bool JBImprovSerial::handleByte(uint8_t byte) {
  bool wasInFrame = _parser.inFrame();
  if (_parser.feed(byte)) {
    handleFrame();
    return true;
  }
  return wasInFrame || _parser.inFrame();
}

void JBImprovSerial::loop() {
  if (!_autoRead) {
    return;
  }
  for (int i = 0; i < MAX_BYTES_PER_LOOP && _stream.available() > 0; i++) {
    int value = _stream.read();
    if (value < 0) {
      break;
    }
    handleByte((uint8_t)value);
  }
}

void JBImprovSerial::handleFrame() {
  if (_parser.type() != JBImprovPacketType::RpcCommand) {
    return;  // State, error and result packets only travel to the client.
  }
  const uint8_t* data = _parser.data();
  uint8_t length = _parser.length();
  if (length < 2 || data[1] != length - 2) {
    setError(JBImprovError::InvalidRpcPacket);
    return;
  }
  handleCommand(data[0], &data[2], data[1]);
}

bool JBImprovSerial::handleTransportCommand(uint8_t command, const uint8_t* data,
                                            uint8_t length) {
  (void)data;
  (void)length;
  if (command != (uint8_t)JBImprovCommand::RequestCurrentState) {
    return false;
  }
  JBImprovState state = _improv->state();
  sendState(state);
  if (state == JBImprovState::Provisioned) {
    // Same result as a successful provisioning, so the client can offer the URL.
    JBImprovResultBuilder result;
    _improv->addRedirectUrl(result);
    sendResult(command, result.data(), result.length());
  }
  return true;
}

void JBImprovSerial::sendState(JBImprovState state) {
  uint8_t value = (uint8_t)state;
  writeFrame(JBImprovPacketType::CurrentState, &value, 1);
}

void JBImprovSerial::sendError(JBImprovError error) {
  uint8_t value = (uint8_t)error;
  writeFrame(JBImprovPacketType::ErrorState, &value, 1);
}

void JBImprovSerial::sendResult(uint8_t command, const uint8_t* data, uint8_t length) {
  uint8_t payload[JBIMPROV_MAX_DATA_LENGTH];
  if (length > JBIMPROV_MAX_RESULT_LENGTH) {
    length = JBIMPROV_MAX_RESULT_LENGTH;  // Can't happen with JBImprovResultBuilder.
  }
  size_t size = jbImprovEncodeRpcResult(command, data, length, false, payload);
  writeFrame(JBImprovPacketType::RpcResult, payload, (uint8_t)size);
}

void JBImprovSerial::writeFrame(JBImprovPacketType type, const uint8_t* data, uint8_t length) {
  uint8_t frame[JBIMPROV_MAX_SERIAL_FRAME_LENGTH];
  size_t size = jbImprovEncodeSerialFrame(type, data, length, frame);
  _stream.write(frame, size);
  // Ends the line, so frames don't run into log output on a terminal.
  _stream.write('\n');
  _stream.flush();
}

#endif  // ARDUINO
