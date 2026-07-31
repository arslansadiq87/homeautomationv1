#include <Arduino.h>
#include <Adafruit_MCP23X17.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <SPI.h>
#include <Wire.h>

#include "modules.h"
#include "pinout.h"

namespace {
constexpr uint8_t BH1750_POWER_ON = 0x01;
constexpr uint8_t BH1750_RESET = 0x07;
constexpr uint8_t BH1750_CONTINUOUS_HIGH_RES = 0x10;

ModuleSnapshot snapshot;
Adafruit_MCP23X17 relayExpander;
uint16_t relayLogicalState = 0;
bool relayExpanderDriverStarted = false;
bool inchingActive[RELAY_CHANNEL_COUNT] = {};
uint32_t inchingEndsAt[RELAY_CHANNEL_COUNT] = {};
uint32_t lastI2cResetMs = 0;
SPIClass storageSpi(FSPI);
OneWire oneWire(PIN_DS18B20);
DallasTemperature ds18b20(&oneWire);
bool ds18b20ConversionPending = false;
uint32_t ds18b20ConversionStartedAt = 0;
bool sht3xConversionPending = false;
uint32_t sht3xConversionStartedAt = 0;
uint32_t sht3xLastRequestMs = 0;
bool buzzerPwmAttached = false;
uint32_t buzzerEndsAt = 0;

SPISettings storageSpiSettings(W25Q128_SPI_CLOCK_HZ, MSBFIRST, SPI_MODE0);

void beginI2cBus() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(I2C_CLOCK_HZ);
  Wire.setTimeOut(I2C_TIMEOUT_MS);
}

void recoverI2cBus() {
  const uint32_t now = millis();
  if (now - lastI2cResetMs < 1000) {
    return;
  }

  lastI2cResetMs = now;
  snapshot.i2cErrorCount++;
  Wire.end();
  yield();
  beginI2cBus();
}

bool writeCommand(uint8_t address, uint8_t command) {
  Wire.beginTransmission(address);
  Wire.write(command);
  const uint8_t result = Wire.endTransmission();
  if (result != 0 && result != 2) {
    recoverI2cBus();
  }
  return result == 0;
}

bool writeCommand16(uint8_t address, uint16_t command) {
  Wire.beginTransmission(address);
  Wire.write(static_cast<uint8_t>(command >> 8));
  Wire.write(static_cast<uint8_t>(command & 0xFF));
  const uint8_t result = Wire.endTransmission();
  if (result != 0 && result != 2) {
    recoverI2cBus();
  }
  return result == 0;
}

bool probeAddress(uint8_t address) {
  Wire.beginTransmission(address);
  const uint8_t result = Wire.endTransmission();
  if (result != 0 && result != 2) {
    recoverI2cBus();
  }
  return result == 0;
}

bool writeRelayState() {
  snapshot.relayState = relayLogicalState;
  if (!snapshot.mcp23017Online) {
    return false;
  }

  if (!probeAddress(I2C_ADDRESS_MCP23017)) {
    snapshot.mcp23017Online = false;
    snapshot.mcp23017Address = 0;
    return false;
  }

  relayExpander.writeGPIOAB(relayLogicalState);
  return true;
}

bool initializeMcp23017() {
  snapshot.mcp23017Online = false;
  snapshot.mcp23017Address = 0;

  if (!relayExpanderDriverStarted) {
    relayExpanderDriverStarted = true;
    if (!relayExpander.begin_I2C(I2C_ADDRESS_MCP23017, &Wire)) {
      return false;
    }
  } else if (!probeAddress(I2C_ADDRESS_MCP23017)) {
    return false;
  }

  // Clear both output latches before changing any pin to an output. With the
  // external pull-downs this keeps every ULN2803A input LOW during startup.
  relayLogicalState = 0;
  relayExpander.writeGPIOAB(0x0000);
  for (uint8_t pin = 0; pin < RELAY_CHANNEL_COUNT; pin++) {
    relayExpander.pinMode(pin, OUTPUT);
    relayExpander.digitalWrite(pin, LOW);
  }

  snapshot.relayState = 0;
  snapshot.mcp23017Online = true;
  snapshot.mcp23017Address = I2C_ADDRESS_MCP23017;
  return true;
}

uint8_t sht3xCrc8(const uint8_t *data, size_t length) {
  uint8_t crc = 0xFF;

  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31) : static_cast<uint8_t>(crc << 1);
    }
  }

  return crc;
}

bool initBh1750() {
  if (!probeAddress(I2C_ADDRESS_BH1750)) {
    snapshot.bh1750Online = false;
    snapshot.lightValid = false;
    return false;
  }

  snapshot.bh1750Online = writeCommand(I2C_ADDRESS_BH1750, BH1750_POWER_ON);
  snapshot.bh1750Online = snapshot.bh1750Online && writeCommand(I2C_ADDRESS_BH1750, BH1750_RESET);
  snapshot.bh1750Online = snapshot.bh1750Online && writeCommand(I2C_ADDRESS_BH1750, BH1750_CONTINUOUS_HIGH_RES);
  return snapshot.bh1750Online;
}

bool readBh1750() {
  if (!snapshot.bh1750Online && !initBh1750()) {
    return false;
  }

  const uint8_t bytesRead = Wire.requestFrom(I2C_ADDRESS_BH1750, static_cast<uint8_t>(2));
  if (bytesRead != 2) {
    snapshot.bh1750Online = false;
    snapshot.lightValid = false;
    recoverI2cBus();
    return false;
  }

  const uint16_t raw = static_cast<uint16_t>(Wire.read() << 8) | Wire.read();
  snapshot.lux = raw / 1.2f;
  snapshot.bh1750Online = true;
  snapshot.lightValid = true;
  return true;
}

bool readSht3x() {
  const uint32_t now = millis();
  if (!sht3xConversionPending) {
    if (sht3xLastRequestMs != 0 && now - sht3xLastRequestMs < 2000) {
      return snapshot.climateValid;
    }
    sht3xLastRequestMs = now;
    if (!probeAddress(I2C_ADDRESS_SHT3X)) {
      snapshot.sht3xOnline = false;
      snapshot.climateValid = false;
      return false;
    }

    if (!writeCommand16(I2C_ADDRESS_SHT3X, 0x2400)) {
      snapshot.sht3xOnline = false;
      snapshot.climateValid = false;
      return false;
    }

    sht3xConversionPending = true;
    sht3xConversionStartedAt = now;
    return snapshot.climateValid;
  }

  if (now - sht3xConversionStartedAt < 20) {
    return snapshot.climateValid;
  }

  sht3xConversionPending = false;

  const uint8_t bytesRead = Wire.requestFrom(I2C_ADDRESS_SHT3X, static_cast<uint8_t>(6));
  if (bytesRead != 6) {
    snapshot.sht3xOnline = false;
    snapshot.climateValid = false;
    recoverI2cBus();
    return false;
  }

  uint8_t data[6];
  for (uint8_t i = 0; i < sizeof(data); i++) {
    data[i] = Wire.read();
  }

  if (sht3xCrc8(data, 2) != data[2] || sht3xCrc8(data + 3, 2) != data[5]) {
    snapshot.sht3xOnline = false;
    snapshot.climateValid = false;
    return false;
  }

  const uint16_t rawTemperature = static_cast<uint16_t>(data[0] << 8) | data[1];
  const uint16_t rawHumidity = static_cast<uint16_t>(data[3] << 8) | data[4];

  snapshot.temperatureC = -45.0f + 175.0f * (static_cast<float>(rawTemperature) / 65535.0f);
  snapshot.humidityPercent = 100.0f * (static_cast<float>(rawHumidity) / 65535.0f);
  snapshot.sht3xOnline = true;
  snapshot.climateValid = true;
  return true;
}

void readSensors() {
  readBh1750();
  snapshot.mq135DigitalActive = digitalRead(PIN_MQ135_DO) == MQ135_DO_ACTIVE_STATE;
  snapshot.mq135AnalogRaw = analogRead(PIN_MQ135_AO);
  snapshot.mq135AnalogVoltage = (static_cast<float>(snapshot.mq135AnalogRaw) * ADC_REFERENCE_VOLTAGE) / ADC_MAX_READING;
  snapshot.motion1Active = digitalRead(PIN_MOTION_1) == MOTION_ACTIVE_STATE;
  snapshot.motion2Active = digitalRead(PIN_MOTION_2) == MOTION_ACTIVE_STATE;
  snapshot.doorReedClosed = digitalRead(PIN_DOOR_REED_SWITCH) == REED_SWITCH_CLOSED_STATE;
  snapshot.garageReedClosed = digitalRead(PIN_GARAGE_REED_SWITCH) == REED_SWITCH_CLOSED_STATE;
  snapshot.lastSensorReadMs = millis();
}

void serviceInchingRelays() {
  const uint32_t now = millis();

  for (uint8_t channel = 0; channel < RELAY_CHANNEL_COUNT; channel++) {
    if (inchingActive[channel] && static_cast<int32_t>(now - inchingEndsAt[channel]) >= 0) {
      inchingActive[channel] = false;
      relaySet(channel, false);
    }
  }
}

void refreshMcp23017() {
  static uint32_t lastCheck = 0;
  if (snapshot.mcp23017Online) {
    return;
  }

  if (millis() - lastCheck < 5000) {
    return;
  }

  lastCheck = millis();
  if (initializeMcp23017()) {
    Serial.print("MCP23017 relay expander detected at 0x");
    Serial.println(I2C_ADDRESS_MCP23017, HEX);
    Serial.println("All 16 relays forced OFF");
  }
}

void serviceDs18b20() {
  const uint32_t now = millis();

  if (ds18b20ConversionPending) {
    if (now - ds18b20ConversionStartedAt < DS18B20_CONVERSION_MS) {
      return;
    }

    const float temperature = ds18b20.getTempCByIndex(0);
    if (temperature == DEVICE_DISCONNECTED_C || isnan(temperature)) {
      snapshot.ds18b20Online = false;
      snapshot.ds18b20TemperatureC = NAN;
    } else {
      snapshot.ds18b20Online = true;
      snapshot.ds18b20TemperatureC = temperature;
    }

    ds18b20ConversionPending = false;
  }

  static uint32_t lastRequest = 0;
  if (now - lastRequest >= 2000) {
    lastRequest = now;
    snapshot.ds18b20DeviceCount = ds18b20.getDeviceCount();
    snapshot.ds18b20Online = snapshot.ds18b20DeviceCount > 0;
    if (snapshot.ds18b20Online) {
      ds18b20.requestTemperatures();
      ds18b20ConversionStartedAt = now;
      ds18b20ConversionPending = true;
    }
  }
}

void serviceBuzzer() {
  if (snapshot.buzzerActive && buzzerEndsAt > 0 && static_cast<int32_t>(millis() - buzzerEndsAt) >= 0) {
    ::buzzerStop();
  }
}

bool applyRelaySet(uint8_t channel, bool enabled, bool clearInchingTimer) {
  if (channel >= RELAY_CHANNEL_COUNT) {
    return false;
  }

  const uint16_t previousState = relayLogicalState;
  if (enabled) {
    relayLogicalState |= static_cast<uint16_t>(1U << channel);
  } else {
    relayLogicalState &= static_cast<uint16_t>(~(1U << channel));
  }

  if (clearInchingTimer) {
    inchingActive[channel] = false;
  }

  if (writeRelayState()) {
    return true;
  }

  relayLogicalState = previousState;
  snapshot.relayState = relayLogicalState;
  return false;
}

void storageSelect() {
  digitalWrite(PIN_W25Q128_CS, LOW);
}

void storageDeselect() {
  digitalWrite(PIN_W25Q128_CS, HIGH);
}

void storageCommand(uint8_t command) {
  storageSpi.beginTransaction(storageSpiSettings);
  storageSelect();
  storageSpi.transfer(command);
  storageDeselect();
  storageSpi.endTransaction();
}

uint32_t storageReadJedecId() {
  storageSpi.beginTransaction(storageSpiSettings);
  storageSelect();
  storageSpi.transfer(0x9F);
  const uint8_t manufacturer = storageSpi.transfer(0x00);
  const uint8_t memoryType = storageSpi.transfer(0x00);
  const uint8_t capacity = storageSpi.transfer(0x00);
  storageDeselect();
  storageSpi.endTransaction();

  return (static_cast<uint32_t>(manufacturer) << 16) | (static_cast<uint32_t>(memoryType) << 8) | capacity;
}

uint8_t storageReadStatusRegister() {
  storageSpi.beginTransaction(storageSpiSettings);
  storageSelect();
  storageSpi.transfer(0x05);
  const uint8_t status = storageSpi.transfer(0x00);
  storageDeselect();
  storageSpi.endTransaction();
  return status;
}

bool storageWaitUntilReady(uint32_t timeoutMs = 120) {
  const uint32_t startedAt = millis();
  while ((storageReadStatusRegister() & 0x01) != 0) {
    if (millis() - startedAt > timeoutMs) {
      return false;
    }
    yield();
  }
  return true;
}

void storageWriteEnable() {
  storageCommand(0x06);
}

void storageTransferAddress(uint32_t address) {
  storageSpi.transfer(static_cast<uint8_t>(address >> 16));
  storageSpi.transfer(static_cast<uint8_t>(address >> 8));
  storageSpi.transfer(static_cast<uint8_t>(address));
}

uint32_t storageBytesFromJedecId(uint32_t jedecId) {
  const uint8_t capacityCode = jedecId & 0xFF;
  if (capacityCode < 16 || capacityCode > 31) {
    return 0;
  }

  return 1UL << capacityCode;
}

bool refreshStorageStatus() {
  storageCommand(0xAB);
  delayMicroseconds(50);

  const uint32_t jedecId = storageReadJedecId();
  const bool validId = jedecId != 0 && jedecId != 0xFFFFFF;
  const uint32_t totalBytes = validId ? storageBytesFromJedecId(jedecId) : 0;

  snapshot.storageOnline = validId && totalBytes > 0;
  snapshot.storageJedecId = validId ? jedecId : 0;
  snapshot.storageStatusRegister = snapshot.storageOnline ? storageReadStatusRegister() : 0;
  snapshot.storageTotalBytes = snapshot.storageOnline ? totalBytes : 0;
  snapshot.storageAvailableBytes = snapshot.storageOnline ? totalBytes : 0;
  return snapshot.storageOnline;
}

void serviceStorage() {
  static uint32_t lastCheck = 0;
  if (millis() - lastCheck < 5000) {
    return;
  }

  lastCheck = millis();
  refreshStorageStatus();
}
}

void modulesBegin() {
  pinMode(PIN_MQ135_DO, INPUT);
  pinMode(PIN_MOTION_1, INPUT);
  pinMode(PIN_MOTION_2, INPUT);
  pinMode(PIN_DOOR_REED_SWITCH, INPUT_PULLUP);
  pinMode(PIN_GARAGE_REED_SWITCH, INPUT_PULLUP);
  pinMode(PIN_PASSIVE_BUZZER, OUTPUT);
  digitalWrite(PIN_PASSIVE_BUZZER, LOW);
  pinMode(PIN_W25Q128_CS, OUTPUT);
  digitalWrite(PIN_W25Q128_CS, HIGH);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_MQ135_AO, ADC_11db);

  storageSpi.begin(PIN_W25Q128_SCK, PIN_W25Q128_MISO, PIN_W25Q128_MOSI, PIN_W25Q128_CS);
  if (refreshStorageStatus()) {
    Serial.print("W25Q storage detected, JEDEC ID 0x");
    Serial.print(snapshot.storageJedecId, HEX);
    Serial.print(", capacity ");
    Serial.print(snapshot.storageTotalBytes / (1024UL * 1024UL));
    Serial.println(" MB");
  } else {
    Serial.println("W25Q storage not detected");
  }

  beginI2cBus();

  ds18b20.begin();
  ds18b20.setWaitForConversion(false);
  snapshot.ds18b20DeviceCount = ds18b20.getDeviceCount();
  snapshot.ds18b20Online = snapshot.ds18b20DeviceCount > 0;
  if (snapshot.ds18b20Online) {
    ds18b20.requestTemperatures();
    ds18b20ConversionStartedAt = millis();
    ds18b20ConversionPending = true;
  }

  relayLogicalState = 0;
  if (initializeMcp23017()) {
    Serial.print("MCP23017 relay expander detected at 0x");
    Serial.println(I2C_ADDRESS_MCP23017, HEX);
    Serial.println("All 16 relays forced OFF");
  } else {
    Serial.println("MCP23017 relay expander not detected at 0x20; all relays remain OFF via pull-downs");
  }

  initBh1750();
  snapshot.sht3xOnline = probeAddress(I2C_ADDRESS_SHT3X);
  readSensors();

  Serial.print("I2C initialized on SDA ");
  Serial.print(PIN_I2C_SDA);
  Serial.print(", SCL ");
  Serial.println(PIN_I2C_SCL);
  Serial.print("DS18B20 devices detected: ");
  Serial.println(snapshot.ds18b20DeviceCount);
}

void modulesLoop() {
  serviceInchingRelays();
  refreshMcp23017();
  readSht3x();
  serviceDs18b20();
  serviceBuzzer();
  serviceStorage();

  static uint32_t lastRead = 0;
  if (millis() - lastRead >= 2000) {
    lastRead = millis();
    readSensors();
  }
}

ModuleSnapshot modulesGetSnapshot() {
  snapshot.relayState = relayLogicalState;
  snapshot.mcp23017Address = snapshot.mcp23017Online ? I2C_ADDRESS_MCP23017 : 0;
  return snapshot;
}

bool relaySet(uint8_t channel, bool enabled) {
  return applyRelaySet(channel, enabled, true);
}

bool relayToggle(uint8_t channel) {
  if (channel >= RELAY_CHANNEL_COUNT) {
    return false;
  }

  return relaySet(channel, !relayGet(channel));
}

bool relayGet(uint8_t channel) {
  if (channel >= RELAY_CHANNEL_COUNT) {
    return false;
  }

  return (relayLogicalState & (1U << channel)) != 0;
}

uint16_t relayGetState() {
  return relayLogicalState;
}

bool relayInching(uint8_t channel, uint32_t durationMs) {
  if (channel >= RELAY_CHANNEL_COUNT || durationMs == 0) {
    return false;
  }

  const bool ok = applyRelaySet(channel, true, false);
  if (ok) {
    inchingActive[channel] = true;
    inchingEndsAt[channel] = millis() + durationMs;
  }

  return ok;
}

bool relayItching(uint8_t channel, uint32_t durationMs) {
  return relayInching(channel, durationMs);
}

bool buzzerStart(uint16_t frequencyHz) {
  const uint16_t frequency = frequencyHz > 0 ? frequencyHz : BUZZER_DEFAULT_FREQUENCY_HZ;

  if (!buzzerPwmAttached) {
    buzzerPwmAttached = ledcAttach(PIN_PASSIVE_BUZZER, frequency, BUZZER_PWM_RESOLUTION_BITS);
  }

  if (!buzzerPwmAttached) {
    snapshot.buzzerActive = false;
    return false;
  }

  ledcWriteTone(PIN_PASSIVE_BUZZER, frequency);
  buzzerEndsAt = 0;
  snapshot.buzzerActive = true;
  return true;
}

void buzzerStop() {
  if (buzzerPwmAttached) {
    ledcWriteTone(PIN_PASSIVE_BUZZER, 0);
  }

  digitalWrite(PIN_PASSIVE_BUZZER, LOW);
  buzzerEndsAt = 0;
  snapshot.buzzerActive = false;
}

bool buzzerBeep(uint16_t frequencyHz, uint32_t durationMs) {
  if (durationMs == 0 || !buzzerStart(frequencyHz)) {
    return false;
  }

  buzzerEndsAt = millis() + durationMs;
  return true;
}

bool storageReadBytes(uint32_t address, uint8_t *data, size_t length) {
  if (!snapshot.storageOnline || data == nullptr || length == 0 || address + length > snapshot.storageTotalBytes) {
    return false;
  }

  storageSpi.beginTransaction(storageSpiSettings);
  storageSelect();
  storageSpi.transfer(0x03);
  storageTransferAddress(address);
  for (size_t i = 0; i < length; i++) {
    data[i] = storageSpi.transfer(0x00);
  }
  storageDeselect();
  storageSpi.endTransaction();
  return true;
}

bool storageEraseSector(uint32_t address) {
  if (!snapshot.storageOnline || address >= snapshot.storageTotalBytes) {
    return false;
  }

  storageWriteEnable();
  storageSpi.beginTransaction(storageSpiSettings);
  storageSelect();
  storageSpi.transfer(0x20);
  storageTransferAddress(address);
  storageDeselect();
  storageSpi.endTransaction();
  return storageWaitUntilReady();
}

bool storageWriteBytes(uint32_t address, const uint8_t *data, size_t length) {
  if (!snapshot.storageOnline || data == nullptr || length == 0 || address + length > snapshot.storageTotalBytes) {
    return false;
  }

  size_t written = 0;
  while (written < length) {
    const uint32_t currentAddress = address + written;
    const size_t pageOffset = currentAddress & 0xFF;
    const size_t pageRoom = 256 - pageOffset;
    const size_t chunk = min(pageRoom, length - written);

    storageWriteEnable();
    storageSpi.beginTransaction(storageSpiSettings);
    storageSelect();
    storageSpi.transfer(0x02);
    storageTransferAddress(currentAddress);
    for (size_t i = 0; i < chunk; i++) {
      storageSpi.transfer(data[written + i]);
    }
    storageDeselect();
    storageSpi.endTransaction();

    if (!storageWaitUntilReady()) {
      return false;
    }
    written += chunk;
  }

  return true;
}
