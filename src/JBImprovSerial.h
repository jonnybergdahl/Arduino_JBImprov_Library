/**
 * @file JBImprovSerial.h
 * @brief Improv Wi-Fi over a serial port (Improv Serial).
 *
 * Works with ESP Web Tools and the https://www.improv-wifi.com/ web page. The port
 * can still be used for log output; clients skip anything that isn't an Improv
 * frame.
 */
#pragma once

#include <Arduino.h>

#include "JBImprov.h"

/**
 * @brief Improv Serial transport on any Arduino Stream (Serial, Serial1, USB CDC...).
 *
 * The stream must be started (e.g. Serial.begin(115200)) by the application.
 */
class JBImprovSerial : public JBImprovTransport {
public:
  /// Use @p stream, which must outlive this object.
  explicit JBImprovSerial(Stream& stream) : _stream(stream) {}

  /**
   * @brief Read the stream from JBImprov::loop() (default true).
   *
   * Turn it off if the application reads the stream itself and passes every byte
   * to handleByte().
   */
  void setAutoRead(bool autoRead) { _autoRead = autoRead; }

  /**
   * @brief Process one byte received from the client.
   * @return true if the byte is part of an Improv frame, false if it is other data
   * the application can use.
   */
  bool handleByte(uint8_t byte);

protected:
  void loop() override;
  void sendState(JBImprovState state) override;
  void sendError(JBImprovError error) override;
  void sendResult(uint8_t command, const uint8_t* data, uint8_t length) override;
  bool handleTransportCommand(uint8_t command, const uint8_t* data, uint8_t length) override;
  bool scanResultsPerNetwork() override { return true; }

private:
  void writeFrame(JBImprovPacketType type, const uint8_t* data, uint8_t length);
  void handleFrame();

  Stream& _stream;
  JBImprovSerialParser _parser;
  bool _autoRead = true;
};
