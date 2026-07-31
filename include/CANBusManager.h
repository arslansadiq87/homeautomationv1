#ifndef CAN_BUS_MANAGER_H
#define CAN_BUS_MANAGER_H

#include <Arduino.h>

struct CANBusFrame {
  uint32_t id = 0;
  bool extended = false;
  uint8_t length = 0;
  uint8_t data[8] = {};
};

struct CANBusStatus {
  bool enabled = false;
  bool ready = false;
  bool error = false;
  uint32_t bitrate = 500000;
  String lastError;
};

void canBusBegin(bool enabled, uint32_t bitrate);
void canBusLoop();
bool canBusSetEnabled(bool enabled);
bool canBusSetBitrate(uint32_t bitrate);
CANBusStatus canBusGetStatus();
bool canBusSend(const CANBusFrame &frame);
bool canBusReceive(CANBusFrame &frame);

#endif
