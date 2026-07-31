#ifndef MODULES_H
#define MODULES_H

#include <Arduino.h>

struct ModuleSnapshot {
  bool bh1750Online = false;
  bool sht3xOnline = false;
  bool mcp23017Online = false;
  bool lightValid = false;
  bool climateValid = false;
  float lux = NAN;
  float temperatureC = NAN;
  float humidityPercent = NAN;
  uint16_t relayState = 0;
  uint8_t mcp23017Address = 0;
  uint32_t i2cErrorCount = 0;
  bool mq135DigitalActive = false;
  uint16_t mq135AnalogRaw = 0;
  float mq135AnalogVoltage = NAN;
  bool ds18b20Online = false;
  uint8_t ds18b20DeviceCount = 0;
  float ds18b20TemperatureC = NAN;
  bool motion1Active = false;
  bool motion2Active = false;
  bool doorReedClosed = false;
  bool garageReedClosed = false;
  bool buzzerActive = false;
  bool storageOnline = false;
  uint32_t storageJedecId = 0;
  uint8_t storageStatusRegister = 0;
  uint32_t storageTotalBytes = 0;
  uint32_t storageAvailableBytes = 0;
  uint32_t lastSensorReadMs = 0;
};

void modulesBegin();
void modulesLoop();
ModuleSnapshot modulesGetSnapshot();

bool relaySet(uint8_t channel, bool enabled);
bool relayToggle(uint8_t channel);
bool relayGet(uint8_t channel);
uint16_t relayGetState();

bool relayInching(uint8_t channel, uint32_t durationMs);
bool relayItching(uint8_t channel, uint32_t durationMs);

bool buzzerStart(uint16_t frequencyHz = 0);
void buzzerStop();
bool buzzerBeep(uint16_t frequencyHz, uint32_t durationMs);

bool storageReadBytes(uint32_t address, uint8_t *data, size_t length);
bool storageEraseSector(uint32_t address);
bool storageWriteBytes(uint32_t address, const uint8_t *data, size_t length);

#endif
