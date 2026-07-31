#ifndef RS485_MANAGER_H
#define RS485_MANAGER_H

#include <Arduino.h>

struct RS485Status {
  bool enabled = false;
  bool ready = false;
  bool error = false;
  uint32_t baudRate = 9600;
  String lastError;
  size_t lastRxBytes = 0;
};

void rs485Begin(bool enabled, uint32_t baudRate);
void rs485Loop();
bool rs485SetEnabled(bool enabled);
bool rs485SetBaudRate(uint32_t baudRate);
RS485Status rs485GetStatus();
bool rs485Send(const uint8_t *data, size_t length);
bool rs485SendText(const String &text);
size_t rs485Receive(uint8_t *buffer, size_t maxLength);

#endif
