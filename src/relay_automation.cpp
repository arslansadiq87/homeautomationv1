#include <Arduino.h>
#include <math.h>
#include <time.h>

#include "modules.h"
#include "pinout.h"
#include "relay_automation.h"

namespace {
constexpr uint32_t SETTINGS_MAGIC = 0x524C5941;
constexpr uint16_t SETTINGS_VERSION = 5;
constexpr uint32_t SETTINGS_ADDRESS = W25Q128_EXPECTED_BYTES - 4096UL;

constexpr uint8_t RELAY_DOOR_LOCK = 0;
constexpr uint8_t RELAY_GARAGE_LOCK = 1;
constexpr uint8_t RELAY_OUTDOOR_LIGHT = 2;
constexpr uint8_t RELAY_EXHAUST_FAN = 5;
constexpr uint8_t RELAY_MOTION_LIGHT_1 = 6;
constexpr uint8_t RELAY_MOTION_LIGHT_2 = 7;

constexpr uint32_t MIN_PULSE_MS = 100;
constexpr uint32_t MAX_PULSE_MS = 30000;
constexpr uint32_t MIN_MOTION_HOLD_MS = 1000;
constexpr uint32_t MAX_MOTION_HOLD_MS = 3600000;
constexpr uint32_t MAX_RELAY_TIMER_MS = 604800000UL;
constexpr uint32_t DEFERRED_SAVE_DELAY_MS = 2000;

struct StoredRelayAutomationSettings {
  uint32_t magic = SETTINGS_MAGIC;
  uint16_t version = SETTINGS_VERSION;
  uint16_t size = 0;
  RelayAutomationSettings settings;
  uint32_t crc = 0;
};

RelayAutomationSettings settings;
bool settingsLoadedFromStorage = false;
bool storageAvailable = false;
bool lastSaveOk = false;
bool doorLockPulseActive = false;
bool garageLockPulseActive = false;
uint32_t doorLockPulseEndsAt = 0;
uint32_t garageLockPulseEndsAt = 0;
uint32_t motionLight1HoldUntil = 0;
uint32_t motionLight2HoldUntil = 0;
bool channelPulseActive[RELAY_CHANNEL_COUNT] = {};
uint32_t channelPulseEndsAt[RELAY_CHANNEL_COUNT] = {};
bool deferredSettingsSavePending = false;
uint32_t deferredSettingsSaveAt = 0;

uint32_t fnv1a(const uint8_t *data, size_t length) {
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < length; i++) {
    hash ^= data[i];
    hash *= 16777619UL;
  }
  return hash;
}

uint32_t settingsCrc(const StoredRelayAutomationSettings &stored) {
  return fnv1a(reinterpret_cast<const uint8_t *>(&stored),
               sizeof(StoredRelayAutomationSettings) - sizeof(stored.crc));
}

RelayAutomationMode sanitizeMode(RelayAutomationMode mode) {
  return mode == RelayAutomationMode::Automatic ? RelayAutomationMode::Automatic : RelayAutomationMode::Manual;
}

RelayChannelMode sanitizeChannelMode(RelayChannelMode mode) {
  switch (mode) {
    case RelayChannelMode::Manual:
    case RelayChannelMode::Automatic:
    case RelayChannelMode::Pulse:
      return mode;
  }
  return RelayChannelMode::Manual;
}

uint32_t boundedDuration(uint32_t value, uint32_t minimum, uint32_t maximum, uint32_t fallback) {
  if (value < minimum || value > maximum) {
    return fallback;
  }
  return value;
}

void setDefaultChannelConfig(RelayChannelConfig &config, uint8_t channel) {
  memset(&config, 0, sizeof(config));
  snprintf(config.name, sizeof(config.name), "Relay %u", static_cast<unsigned>(channel + 1));
  config.mode = RelayChannelMode::Manual;
  config.showInDashboard = false;
  config.pulseDurationMs = 1000;
  config.enabledWeekdays = 0x7F;
}

void ensureChannelDefaults(RelayAutomationSettings &value) {
  bool garageDoorRoleUsed = false;
  bool garageGateRoleUsed = false;
  for (uint8_t channel = 0; channel < RELAY_CHANNEL_COUNT; channel++) {
    RelayChannelConfig &config = value.channels[channel];
    if (config.name[0] == '\0') {
      snprintf(config.name, sizeof(config.name), "Relay %u", static_cast<unsigned>(channel + 1));
    }
    config.name[sizeof(config.name) - 1] = '\0';
    config.mode = sanitizeChannelMode(config.mode);
    config.pulseDurationMs = boundedDuration(config.pulseDurationMs, MIN_PULSE_MS, MAX_RELAY_TIMER_MS, 1000);
    if (config.mode != RelayChannelMode::Pulse ||
        static_cast<uint8_t>(config.pulseRole) > static_cast<uint8_t>(RelayPulseRole::GarageGate)) {
      config.pulseRole = RelayPulseRole::None;
    }
    if (config.pulseRole == RelayPulseRole::GarageDoor) {
      if (garageDoorRoleUsed) {
        config.pulseRole = RelayPulseRole::None;
      } else {
        garageDoorRoleUsed = true;
      }
    } else if (config.pulseRole == RelayPulseRole::GarageGate) {
      if (garageGateRoleUsed) {
        config.pulseRole = RelayPulseRole::None;
      } else {
        garageGateRoleUsed = true;
      }
    }
    if (config.automaticControlType != RelayAutomaticControlType::Schedule) {
      config.automaticControlType = RelayAutomaticControlType::Parametric;
    }
    if (static_cast<uint8_t>(config.sensor) > static_cast<uint8_t>(RelaySensorType::PirMotion)) {
      config.sensor = RelaySensorType::IndoorTemperature;
    }
    if (static_cast<uint8_t>(config.comparison) > static_cast<uint8_t>(RelayComparisonCondition::NoMotionDetected)) {
      config.comparison = RelayComparisonCondition::GreaterThan;
    }
    if (!isfinite(config.onThreshold)) {
      config.onThreshold = 30.0f;
    }
    if (!isfinite(config.offThreshold)) {
      config.offThreshold = config.onThreshold;
    }
    config.scheduleOnMinutes = config.scheduleOnMinutes > 1439 ? 1439 : config.scheduleOnMinutes;
    config.scheduleOffMinutes = config.scheduleOffMinutes > 1439 ? 1439 : config.scheduleOffMinutes;
    config.enabledWeekdays &= 0x7F;
  }
}

void sanitizeSettings(RelayAutomationSettings &value) {
  const RelayAutomationSettings defaults;

  value.doorLockPulseMs = boundedDuration(value.doorLockPulseMs, MIN_PULSE_MS, MAX_PULSE_MS, defaults.doorLockPulseMs);
  value.garageLockPulseMs = boundedDuration(value.garageLockPulseMs, MIN_PULSE_MS, MAX_PULSE_MS, defaults.garageLockPulseMs);
  value.motionLight1DurationMs =
    boundedDuration(value.motionLight1DurationMs, MIN_MOTION_HOLD_MS, MAX_MOTION_HOLD_MS, defaults.motionLight1DurationMs);
  value.motionLight2DurationMs =
    boundedDuration(value.motionLight2DurationMs, MIN_MOTION_HOLD_MS, MAX_MOTION_HOLD_MS, defaults.motionLight2DurationMs);

  if (!isfinite(value.outdoorLightOnBelowLux) || value.outdoorLightOnBelowLux < 0.0f) {
    value.outdoorLightOnBelowLux = defaults.outdoorLightOnBelowLux;
  }
  if (!isfinite(value.outdoorLightOffAboveLux) || value.outdoorLightOffAboveLux <= value.outdoorLightOnBelowLux) {
    value.outdoorLightOffAboveLux = max(value.outdoorLightOnBelowLux + 5.0f, defaults.outdoorLightOffAboveLux);
  }
  if (!isfinite(value.exhaustFanOnAboveTemperature)) {
    value.exhaustFanOnAboveTemperature = defaults.exhaustFanOnAboveTemperature;
  }
  if (!isfinite(value.exhaustFanOffBelowTemperature) ||
      value.exhaustFanOffBelowTemperature >= value.exhaustFanOnAboveTemperature) {
    value.exhaustFanOffBelowTemperature = min(value.exhaustFanOnAboveTemperature - 1.0f, defaults.exhaustFanOffBelowTemperature);
  }

  value.outdoorLightMode = sanitizeMode(value.outdoorLightMode);
  value.exhaustFanMode = sanitizeMode(value.exhaustFanMode);
  value.motionLight1Mode = sanitizeMode(value.motionLight1Mode);
  value.motionLight2Mode = sanitizeMode(value.motionLight2Mode);
  ensureChannelDefaults(value);
}

bool channelCanEnergize(uint8_t channel) {
  return channel < RELAY_CHANNEL_COUNT;
}

bool setRelayConfigured(uint8_t channel, bool enabled) {
  if (enabled && !channelCanEnergize(channel)) {
    return false;
  }
  return relaySet(channel, enabled);
}

void scheduleDeferredSettingsSave(uint32_t now = millis()) {
  deferredSettingsSavePending = true;
  deferredSettingsSaveAt = now + DEFERRED_SAVE_DELAY_MS;
}

RelayAutomationMode getMode(RelayAutomationDevice device) {
  switch (device) {
    case RelayAutomationDevice::OutdoorLight:
      return settings.outdoorLightMode;
    case RelayAutomationDevice::ExhaustFan:
      return settings.exhaustFanMode;
    case RelayAutomationDevice::MotionLight1:
      return settings.motionLight1Mode;
    case RelayAutomationDevice::MotionLight2:
      return settings.motionLight2Mode;
  }
  return RelayAutomationMode::Manual;
}

bool getManualState(RelayAutomationDevice device) {
  switch (device) {
    case RelayAutomationDevice::OutdoorLight:
      return settings.outdoorLightManualState;
    case RelayAutomationDevice::ExhaustFan:
      return settings.exhaustFanManualState;
    case RelayAutomationDevice::MotionLight1:
      return settings.motionLight1ManualState;
    case RelayAutomationDevice::MotionLight2:
      return settings.motionLight2ManualState;
  }
  return false;
}

uint8_t relayForDevice(RelayAutomationDevice device) {
  switch (device) {
    case RelayAutomationDevice::OutdoorLight:
      return RELAY_OUTDOOR_LIGHT;
    case RelayAutomationDevice::ExhaustFan:
      return RELAY_EXHAUST_FAN;
    case RelayAutomationDevice::MotionLight1:
      return RELAY_MOTION_LIGHT_1;
    case RelayAutomationDevice::MotionLight2:
      return RELAY_MOTION_LIGHT_2;
  }
  return RELAY_OUTDOOR_LIGHT;
}

void applyManualState(RelayAutomationDevice device) {
  if (getMode(device) == RelayAutomationMode::Manual) {
    setRelayConfigured(relayForDevice(device), getManualState(device));
  }
}

void applyManualStates() {
  applyManualState(RelayAutomationDevice::OutdoorLight);
  applyManualState(RelayAutomationDevice::ExhaustFan);
  applyManualState(RelayAutomationDevice::MotionLight1);
  applyManualState(RelayAutomationDevice::MotionLight2);
}

void setAllRelaysSafeOff(bool clearSavedState = true) {
  for (uint8_t channel = 0; channel < RELAY_CHANNEL_COUNT; channel++) {
    relaySet(channel, false);
    channelPulseActive[channel] = false;
    if (clearSavedState) {
      settings.channels[channel].currentState = false;
    }
  }
  doorLockPulseActive = false;
  garageLockPulseActive = false;
}

void serviceChannelPulses(uint32_t now) {
  for (uint8_t channel = 0; channel < RELAY_CHANNEL_COUNT; channel++) {
    if (channelPulseActive[channel] && static_cast<int32_t>(now - channelPulseEndsAt[channel]) >= 0) {
      channelPulseActive[channel] = false;
      settings.channels[channel].currentState = false;
      relaySet(channel, false);
      scheduleDeferredSettingsSave(now);
    }
  }
}

void serviceDeferredSettingsSave(uint32_t now) {
  if (deferredSettingsSavePending && static_cast<int32_t>(now - deferredSettingsSaveAt) >= 0) {
    deferredSettingsSavePending = false;
    relayAutomationSaveSettings();
  }
}

void serviceDoorPulses(uint32_t now) {
  if (doorLockPulseActive && static_cast<int32_t>(now - doorLockPulseEndsAt) >= 0) {
    doorLockPulseActive = false;
    relaySet(RELAY_DOOR_LOCK, false);
  }

  if (garageLockPulseActive && static_cast<int32_t>(now - garageLockPulseEndsAt) >= 0) {
    garageLockPulseActive = false;
    relaySet(RELAY_GARAGE_LOCK, false);
  }
}

void serviceOutdoorLight(const ModuleSnapshot &modules) {
  if (settings.outdoorLightMode != RelayAutomationMode::Automatic || !modules.lightValid || isnan(modules.lux)) {
    return;
  }

  const bool current = relayGet(RELAY_OUTDOOR_LIGHT);
  if (!current && modules.lux < settings.outdoorLightOnBelowLux) {
    setRelayConfigured(RELAY_OUTDOOR_LIGHT, true);
  } else if (current && modules.lux > settings.outdoorLightOffAboveLux) {
    relaySet(RELAY_OUTDOOR_LIGHT, false);
  }
}

void serviceExhaustFan(const ModuleSnapshot &modules) {
  if (settings.exhaustFanMode != RelayAutomationMode::Automatic || !modules.climateValid || isnan(modules.temperatureC)) {
    return;
  }

  const bool current = relayGet(RELAY_EXHAUST_FAN);
  if (!current && modules.temperatureC > settings.exhaustFanOnAboveTemperature) {
    setRelayConfigured(RELAY_EXHAUST_FAN, true);
  } else if (current && modules.temperatureC < settings.exhaustFanOffBelowTemperature) {
    relaySet(RELAY_EXHAUST_FAN, false);
  }
}

void serviceMotionLight(const ModuleSnapshot &modules,
                        RelayAutomationMode mode,
                        bool motionActive,
                        uint8_t relay,
                        uint32_t durationMs,
                        uint32_t &holdUntil) {
  if (mode != RelayAutomationMode::Automatic) {
    return;
  }

  const uint32_t now = millis();
  if (motionActive) {
    holdUntil = now + durationMs;
  }

  setRelayConfigured(relay, static_cast<int32_t>(now - holdUntil) < 0);
}

uint32_t remainingMs(bool active, uint32_t endsAt) {
  if (!active) {
    return 0;
  }

  const int32_t remaining = static_cast<int32_t>(endsAt - millis());
  return remaining > 0 ? static_cast<uint32_t>(remaining) : 0;
}

bool channelForPulseRole(RelayPulseRole role, uint8_t &channel) {
  for (uint8_t index = 0; index < RELAY_CHANNEL_COUNT; index++) {
    if (settings.channels[index].mode == RelayChannelMode::Pulse && settings.channels[index].pulseRole == role) {
      channel = index;
      return true;
    }
  }
  return false;
}

uint32_t channelPulseRemainingMs(uint8_t channel) {
  return channel < RELAY_CHANNEL_COUNT ? remainingMs(channelPulseActive[channel], channelPulseEndsAt[channel]) : 0;
}

bool sensorValue(const ModuleSnapshot &modules, RelaySensorType sensor, float &value) {
  switch (sensor) {
    case RelaySensorType::IndoorTemperature:
      value = modules.temperatureC;
      return modules.climateValid && isfinite(value);
    case RelaySensorType::OutdoorTemperature:
      value = modules.ds18b20TemperatureC;
      return modules.ds18b20Online && isfinite(value);
    case RelaySensorType::Humidity:
      value = modules.humidityPercent;
      return modules.climateValid && isfinite(value);
    case RelaySensorType::Lux:
      value = modules.lux;
      return modules.lightValid && isfinite(value);
    case RelaySensorType::Mq135:
      value = modules.mq135AnalogRaw;
      return true;
    case RelaySensorType::PirMotion:
      value = (modules.motion1Active || modules.motion2Active) ? 1.0f : 0.0f;
      return true;
  }
  return false;
}

bool automaticTargetState(const RelayChannelConfig &config, const ModuleSnapshot &modules, bool current) {
  float value = NAN;
  if (!sensorValue(modules, config.sensor, value)) {
    return current;
  }

  if (config.sensor == RelaySensorType::PirMotion) {
    const bool motion = value > 0.5f;
    return config.comparison == RelayComparisonCondition::NoMotionDetected ? !motion : motion;
  }

  if (config.comparison == RelayComparisonCondition::LessThan) {
    if (!current && value < config.onThreshold) {
      return true;
    }
    if (current && value > config.offThreshold) {
      return false;
    }
    return current;
  }

  if (!current && value > config.onThreshold) {
    return true;
  }
  if (current && value < config.offThreshold) {
    return false;
  }
  return current;
}

bool scheduleTargetState(const RelayChannelConfig &config, bool current) {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 0)) {
    return current;
  }

  const uint8_t weekdayBit = static_cast<uint8_t>(1U << timeInfo.tm_wday);
  if ((config.enabledWeekdays & weekdayBit) == 0) {
    return false;
  }

  const uint16_t nowMinutes = static_cast<uint16_t>((timeInfo.tm_hour * 60) + timeInfo.tm_min);
  if (config.scheduleOnMinutes == config.scheduleOffMinutes) {
    return false;
  }
  if (config.scheduleOnMinutes < config.scheduleOffMinutes) {
    return nowMinutes >= config.scheduleOnMinutes && nowMinutes < config.scheduleOffMinutes;
  }
  return nowMinutes >= config.scheduleOnMinutes || nowMinutes < config.scheduleOffMinutes;
}

void serviceChannelAutomation(const ModuleSnapshot &modules) {
  for (uint8_t channel = 0; channel < RELAY_CHANNEL_COUNT; channel++) {
    RelayChannelConfig &config = settings.channels[channel];
    if (config.mode != RelayChannelMode::Automatic || channelPulseActive[channel]) {
      continue;
    }
    const bool current = relayGet(channel);
    const bool target = config.automaticControlType == RelayAutomaticControlType::Schedule
                          ? scheduleTargetState(config, current)
                          : automaticTargetState(config, modules, current);
    if (target != current && setRelayConfigured(channel, target)) {
      config.currentState = target;
    }
  }
}
}

void relayAutomationBegin() {
  settings = RelayAutomationSettings();
  for (uint8_t channel = 0; channel < RELAY_CHANNEL_COUNT; channel++) {
    setDefaultChannelConfig(settings.channels[channel], channel);
  }
  relayAutomationLoadSettings();
  settings.outdoorLightManualState = false;
  settings.exhaustFanManualState = false;
  settings.motionLight1ManualState = false;
  settings.motionLight2ManualState = false;
  setAllRelaysSafeOff(false);
  for (uint8_t channel = 0; channel < RELAY_CHANNEL_COUNT; channel++) {
    if (settings.channels[channel].mode == RelayChannelMode::Manual && settings.channels[channel].currentState) {
      setRelayConfigured(channel, true);
    }
  }
}

void relayAutomationLoop() {
  const uint32_t now = millis();
  serviceDoorPulses(now);
  serviceChannelPulses(now);
  serviceDeferredSettingsSave(now);

  const ModuleSnapshot modules = modulesGetSnapshot();
  serviceOutdoorLight(modules);
  serviceExhaustFan(modules);
  serviceMotionLight(modules,
                     settings.motionLight1Mode,
                     modules.motion1Active,
                     RELAY_MOTION_LIGHT_1,
                     settings.motionLight1DurationMs,
                     motionLight1HoldUntil);
  serviceMotionLight(modules,
                     settings.motionLight2Mode,
                     modules.motion2Active,
                     RELAY_MOTION_LIGHT_2,
                     settings.motionLight2DurationMs,
                     motionLight2HoldUntil);
  serviceChannelAutomation(modules);
}

RelayAutomationSettings relayAutomationGetSettings() {
  return settings;
}

RelayAutomationSnapshot relayAutomationGetSnapshot() {
  RelayAutomationSnapshot snapshot;
  snapshot.settings = settings;
  snapshot.settingsLoadedFromStorage = settingsLoadedFromStorage;
  snapshot.storageAvailable = storageAvailable;
  snapshot.lastSaveOk = lastSaveOk;
  snapshot.doorLockActive = relayGet(RELAY_DOOR_LOCK);
  snapshot.garageLockActive = relayGet(RELAY_GARAGE_LOCK);
  snapshot.outdoorLightOn = relayGet(RELAY_OUTDOOR_LIGHT);
  snapshot.exhaustFanOn = relayGet(RELAY_EXHAUST_FAN);
  snapshot.motionLight1On = relayGet(RELAY_MOTION_LIGHT_1);
  snapshot.motionLight2On = relayGet(RELAY_MOTION_LIGHT_2);
  snapshot.doorLockRemainingMs = remainingMs(doorLockPulseActive, doorLockPulseEndsAt);
  snapshot.garageLockRemainingMs = remainingMs(garageLockPulseActive, garageLockPulseEndsAt);
  uint8_t roleChannel = 0;
  if (channelForPulseRole(RelayPulseRole::GarageDoor, roleChannel)) {
    snapshot.doorLockActive = relayGet(roleChannel);
    snapshot.doorLockRemainingMs = channelPulseRemainingMs(roleChannel);
  }
  if (channelForPulseRole(RelayPulseRole::GarageGate, roleChannel)) {
    snapshot.garageLockActive = relayGet(roleChannel);
    snapshot.garageLockRemainingMs = channelPulseRemainingMs(roleChannel);
  }
  snapshot.motionLight1RemainingMs = remainingMs(snapshot.motionLight1On, motionLight1HoldUntil);
  snapshot.motionLight2RemainingMs = remainingMs(snapshot.motionLight2On, motionLight2HoldUntil);
  return snapshot;
}

bool relayAutomationUpdateSettings(const RelayAutomationSettings &updatedSettings) {
  deferredSettingsSavePending = false;
  settings = updatedSettings;
  sanitizeSettings(settings);
  setAllRelaysSafeOff();
  applyManualStates();
  for (uint8_t channel = 0; channel < RELAY_CHANNEL_COUNT; channel++) {
    if (settings.channels[channel].mode == RelayChannelMode::Manual && settings.channels[channel].currentState) {
      setRelayConfigured(channel, true);
    }
  }
  return relayAutomationSaveSettings();
}

bool relayAutomationSaveSettings() {
  StoredRelayAutomationSettings stored;
  stored.size = sizeof(StoredRelayAutomationSettings);
  stored.settings = settings;
  sanitizeSettings(stored.settings);
  for (uint8_t channel = 0; channel < RELAY_CHANNEL_COUNT; channel++) {
    const bool current = relayGet(channel);
    settings.channels[channel].currentState = current;
    stored.settings.channels[channel].currentState = current;
  }
  stored.crc = settingsCrc(stored);

  StoredRelayAutomationSettings existing;
  if (storageReadBytes(SETTINGS_ADDRESS, reinterpret_cast<uint8_t *>(&existing), sizeof(existing)) &&
      existing.magic == stored.magic && existing.version == stored.version && existing.size == stored.size &&
      existing.crc == stored.crc) {
    lastSaveOk = true;
    storageAvailable = true;
    return true;
  }

  storageAvailable = storageEraseSector(SETTINGS_ADDRESS);
  if (!storageAvailable) {
    lastSaveOk = false;
    return false;
  }

  lastSaveOk = storageWriteBytes(SETTINGS_ADDRESS, reinterpret_cast<const uint8_t *>(&stored), sizeof(stored));
  storageAvailable = storageAvailable && lastSaveOk;
  return lastSaveOk;
}

bool relayAutomationLoadSettings() {
  StoredRelayAutomationSettings stored;
  storageAvailable = storageReadBytes(SETTINGS_ADDRESS, reinterpret_cast<uint8_t *>(&stored), sizeof(stored));
  if (!storageAvailable || stored.magic != SETTINGS_MAGIC || stored.version != SETTINGS_VERSION ||
      stored.size != sizeof(StoredRelayAutomationSettings) || stored.crc != settingsCrc(stored)) {
    settings = RelayAutomationSettings();
    for (uint8_t channel = 0; channel < RELAY_CHANNEL_COUNT; channel++) {
      setDefaultChannelConfig(settings.channels[channel], channel);
    }
    settingsLoadedFromStorage = false;
    return false;
  }

  settings = stored.settings;
  sanitizeSettings(settings);
  settingsLoadedFromStorage = true;
  return true;
}

bool relayAutomationSetMode(RelayAutomationDevice device, RelayAutomationMode mode) {
  mode = sanitizeMode(mode);
  switch (device) {
    case RelayAutomationDevice::OutdoorLight:
      settings.outdoorLightMode = mode;
      break;
    case RelayAutomationDevice::ExhaustFan:
      settings.exhaustFanMode = mode;
      break;
    case RelayAutomationDevice::MotionLight1:
      settings.motionLight1Mode = mode;
      motionLight1HoldUntil = 0;
      break;
    case RelayAutomationDevice::MotionLight2:
      settings.motionLight2Mode = mode;
      motionLight2HoldUntil = 0;
      break;
  }

  if (mode == RelayAutomationMode::Manual) {
    applyManualState(device);
  }

  return relayAutomationSaveSettings();
}

bool relayAutomationSetManualState(RelayAutomationDevice device, bool enabled) {
  switch (device) {
    case RelayAutomationDevice::OutdoorLight:
      settings.outdoorLightManualState = enabled;
      break;
    case RelayAutomationDevice::ExhaustFan:
      settings.exhaustFanManualState = enabled;
      break;
    case RelayAutomationDevice::MotionLight1:
      settings.motionLight1ManualState = enabled;
      break;
    case RelayAutomationDevice::MotionLight2:
      settings.motionLight2ManualState = enabled;
      break;
  }

  applyManualState(device);
  return relayAutomationSaveSettings();
}

bool relayAutomationToggleManualState(RelayAutomationDevice device) {
  return relayAutomationSetManualState(device, !getManualState(device));
}

bool relayAutomationPulseDoorLock() {
  uint8_t roleChannel = 0;
  if (channelForPulseRole(RelayPulseRole::GarageDoor, roleChannel)) {
    return relayAutomationPulseChannel(roleChannel);
  }
  const bool ok = setRelayConfigured(RELAY_DOOR_LOCK, true);
  if (ok) {
    doorLockPulseActive = true;
    doorLockPulseEndsAt = millis() + settings.doorLockPulseMs;
  }
  return ok;
}

bool relayAutomationPulseGarageLock() {
  uint8_t roleChannel = 0;
  if (channelForPulseRole(RelayPulseRole::GarageGate, roleChannel)) {
    return relayAutomationPulseChannel(roleChannel);
  }
  const bool ok = setRelayConfigured(RELAY_GARAGE_LOCK, true);
  if (ok) {
    garageLockPulseActive = true;
    garageLockPulseEndsAt = millis() + settings.garageLockPulseMs;
  }
  return ok;
}

const char *relayAutomationModeName(RelayAutomationMode mode) {
  return mode == RelayAutomationMode::Automatic ? "automatic" : "manual";
}

const char *relayChannelModeName(RelayChannelMode mode) {
  switch (sanitizeChannelMode(mode)) {
    case RelayChannelMode::Automatic:
      return "automatic";
    case RelayChannelMode::Pulse:
      return "pulse";
    case RelayChannelMode::Manual:
    default:
      return "manual";
  }
}

const char *relayAutomaticControlTypeName(RelayAutomaticControlType type) {
  return type == RelayAutomaticControlType::Schedule ? "schedule" : "parametric";
}

const char *relaySensorTypeName(RelaySensorType sensor) {
  switch (sensor) {
    case RelaySensorType::Lux:
      return "lux";
    case RelaySensorType::Mq135:
      return "mq135";
    case RelaySensorType::PirMotion:
      return "pir";
    case RelaySensorType::OutdoorTemperature:
      return "outdoorTemperature";
    case RelaySensorType::Humidity:
      return "humidity";
    case RelaySensorType::IndoorTemperature:
    default:
      return "indoorTemperature";
  }
}

const char *relayComparisonConditionName(RelayComparisonCondition condition) {
  switch (condition) {
    case RelayComparisonCondition::LessThan:
      return "lessThan";
    case RelayComparisonCondition::MotionDetected:
      return "motionDetected";
    case RelayComparisonCondition::NoMotionDetected:
      return "noMotionDetected";
    case RelayComparisonCondition::GreaterThan:
    default:
      return "greaterThan";
  }
}

const char *relayPulseRoleName(RelayPulseRole role) {
  switch (role) {
    case RelayPulseRole::GarageDoor:
      return "garageDoor";
    case RelayPulseRole::GarageGate:
      return "garageGate";
    case RelayPulseRole::None:
    default:
      return "none";
  }
}

bool relayAutomationParseDevice(const String &value, RelayAutomationDevice &device) {
  if (value == "outdoorLight") {
    device = RelayAutomationDevice::OutdoorLight;
    return true;
  }
  if (value == "exhaustFan") {
    device = RelayAutomationDevice::ExhaustFan;
    return true;
  }
  if (value == "motionLight1") {
    device = RelayAutomationDevice::MotionLight1;
    return true;
  }
  if (value == "motionLight2") {
    device = RelayAutomationDevice::MotionLight2;
    return true;
  }
  return false;
}

bool relayAutomationParseMode(const String &value, RelayAutomationMode &mode) {
  if (value == "automatic" || value == "auto") {
    mode = RelayAutomationMode::Automatic;
    return true;
  }
  if (value == "manual") {
    mode = RelayAutomationMode::Manual;
    return true;
  }
  return false;
}

bool relayAutomationParseChannelMode(const String &value, RelayChannelMode &mode) {
  if (value == "manual") {
    mode = RelayChannelMode::Manual;
    return true;
  }
  if (value == "automatic" || value == "auto") {
    mode = RelayChannelMode::Automatic;
    return true;
  }
  if (value == "pulse") {
    mode = RelayChannelMode::Pulse;
    return true;
  }
  return false;
}

bool relayAutomationParseAutomaticControlType(const String &value, RelayAutomaticControlType &type) {
  if (value == "schedule") {
    type = RelayAutomaticControlType::Schedule;
    return true;
  }
  if (value == "parametric") {
    type = RelayAutomaticControlType::Parametric;
    return true;
  }
  return false;
}

bool relayAutomationParseSensorType(const String &value, RelaySensorType &sensor) {
  if (value == "lux") {
    sensor = RelaySensorType::Lux;
    return true;
  }
  if (value == "mq135") {
    sensor = RelaySensorType::Mq135;
    return true;
  }
  if (value == "pir") {
    sensor = RelaySensorType::PirMotion;
    return true;
  }
  if (value == "outdoorTemperature" || value == "ds18b20") {
    sensor = RelaySensorType::OutdoorTemperature;
    return true;
  }
  if (value == "humidity") {
    sensor = RelaySensorType::Humidity;
    return true;
  }
  if (value == "indoorTemperature" || value == "temperature") {
    sensor = RelaySensorType::IndoorTemperature;
    return true;
  }
  return false;
}

bool relayAutomationParseComparisonCondition(const String &value, RelayComparisonCondition &condition) {
  if (value == "lessThan") {
    condition = RelayComparisonCondition::LessThan;
    return true;
  }
  if (value == "motionDetected") {
    condition = RelayComparisonCondition::MotionDetected;
    return true;
  }
  if (value == "noMotionDetected") {
    condition = RelayComparisonCondition::NoMotionDetected;
    return true;
  }
  if (value == "greaterThan") {
    condition = RelayComparisonCondition::GreaterThan;
    return true;
  }
  return false;
}

bool relayAutomationParsePulseRole(const String &value, RelayPulseRole &role) {
  if (value == "garageDoor") {
    role = RelayPulseRole::GarageDoor;
    return true;
  }
  if (value == "garageGate") {
    role = RelayPulseRole::GarageGate;
    return true;
  }
  if (value == "none" || value.length() == 0) {
    role = RelayPulseRole::None;
    return true;
  }
  return false;
}

bool relayAutomationChannelEnabled(uint8_t channel) {
  return channelCanEnergize(channel);
}

bool relayAutomationChannelAllowsPulse(uint8_t channel) {
  return channelCanEnergize(channel) && settings.channels[channel].mode == RelayChannelMode::Pulse;
}

bool relayAutomationSetChannelManualState(uint8_t channel, bool enabled) {
  if (channel >= RELAY_CHANNEL_COUNT || settings.channels[channel].mode != RelayChannelMode::Manual) {
    return false;
  }
  settings.channels[channel].currentState = enabled;
  const bool ok = setRelayConfigured(channel, enabled);
  if (ok) {
    scheduleDeferredSettingsSave();
  }
  return ok;
}

bool relayAutomationPulseChannel(uint8_t channel) {
  if (channel >= RELAY_CHANNEL_COUNT || settings.channels[channel].mode != RelayChannelMode::Pulse || channelPulseActive[channel]) {
    return false;
  }
  const bool ok = setRelayConfigured(channel, true);
  if (ok) {
    settings.channels[channel].currentState = true;
    channelPulseActive[channel] = true;
    channelPulseEndsAt[channel] = millis() + settings.channels[channel].pulseDurationMs;
    scheduleDeferredSettingsSave();
  }
  return ok;
}
