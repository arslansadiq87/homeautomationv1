#include "RS485Manager.h"

#include "pinout.h"

namespace {
HardwareSerial rs485Serial(1);
RS485Status status;
bool serialStarted = false;

void setReceiveMode() {
  digitalWrite(PIN_RS485_DE_RE, LOW);
}

void setTransmitMode() {
  digitalWrite(PIN_RS485_DE_RE, HIGH);
}

bool startSerial() {
  if (serialStarted) {
    rs485Serial.end();
    serialStarted = false;
  }

  setReceiveMode();
  rs485Serial.begin(status.baudRate, SERIAL_8N1, PIN_RS485_RX, PIN_RS485_TX);
  serialStarted = true;
  status.ready = true;
  status.error = false;
  status.lastError = "";
  return true;
}
}

void rs485Begin(bool enabled, uint32_t baudRate) {
  pinMode(PIN_RS485_DE_RE, OUTPUT);
  setReceiveMode();
  status.baudRate = baudRate > 0 ? baudRate : 9600;
  status.enabled = enabled;
  status.ready = false;
  status.error = false;
  status.lastError = enabled ? "RS485 not started" : "RS485 disabled";
  if (enabled) {
    startSerial();
  }
}

void rs485Loop() {
  if (!status.enabled || !serialStarted) {
    return;
  }
  status.lastRxBytes = rs485Serial.available();
}

bool rs485SetEnabled(bool enabled) {
  status.enabled = enabled;
  setReceiveMode();
  if (!enabled) {
    status.ready = false;
    status.error = false;
    status.lastError = "RS485 disabled";
    if (serialStarted) {
      rs485Serial.end();
      serialStarted = false;
    }
    return true;
  }
  return startSerial();
}

bool rs485SetBaudRate(uint32_t baudRate) {
  if (baudRate == 0) {
    status.error = true;
    status.lastError = "Invalid RS485 baud rate";
    return false;
  }
  status.baudRate = baudRate;
  if (!status.enabled) {
    return true;
  }
  return startSerial();
}

RS485Status rs485GetStatus() {
  RS485Status copy = status;
  if (serialStarted) {
    copy.lastRxBytes = rs485Serial.available();
  }
  return copy;
}

bool rs485Send(const uint8_t *data, size_t length) {
  if (!status.enabled || !serialStarted || data == nullptr || length == 0) {
    return false;
  }

  setTransmitMode();
  const size_t written = rs485Serial.write(data, length);
  rs485Serial.flush();
  setReceiveMode();
  return written == length;
}

bool rs485SendText(const String &text) {
  return rs485Send(reinterpret_cast<const uint8_t *>(text.c_str()), text.length());
}

size_t rs485Receive(uint8_t *buffer, size_t maxLength) {
  if (!status.enabled || !serialStarted || buffer == nullptr || maxLength == 0) {
    return 0;
  }

  size_t count = 0;
  while (count < maxLength && rs485Serial.available() > 0) {
    buffer[count++] = static_cast<uint8_t>(rs485Serial.read());
  }
  status.lastRxBytes = rs485Serial.available();
  return count;
}
