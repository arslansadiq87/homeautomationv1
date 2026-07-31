#ifndef RELAY_AUTOMATION_H
#define RELAY_AUTOMATION_H

#include <Arduino.h>

enum class RelayAutomationMode : uint8_t {
  Manual = 0,
  Automatic = 1,
};

enum class RelayAutomationDevice : uint8_t {
  OutdoorLight = 0,
  ExhaustFan = 1,
  MotionLight1 = 2,
  MotionLight2 = 3,
};

enum class RelayChannelMode : uint8_t {
  Manual = 0,
  Pulse = 1,
  Automatic = 2,
};

enum class RelayAutomaticControlType : uint8_t {
  Parametric = 0,
  Schedule = 1,
};

enum class RelaySensorType : uint8_t {
  IndoorTemperature = 0,
  OutdoorTemperature = 1,
  Humidity = 2,
  Lux = 3,
  Mq135 = 4,
  PirMotion = 5,
};

enum class RelayComparisonCondition : uint8_t {
  GreaterThan = 0,
  LessThan = 1,
  MotionDetected = 2,
  NoMotionDetected = 3,
};

enum class RelayPulseRole : uint8_t {
  None = 0,
  GarageDoor = 1,
  GarageGate = 2,
};

struct RelayChannelConfig {
  char name[32] = {};
  RelayChannelMode mode = RelayChannelMode::Manual;
  bool showInDashboard = false;
  bool currentState = false;
  uint32_t pulseDurationMs = 1000;
  RelayPulseRole pulseRole = RelayPulseRole::None;
  RelayAutomaticControlType automaticControlType = RelayAutomaticControlType::Parametric;
  RelaySensorType sensor = RelaySensorType::IndoorTemperature;
  RelayComparisonCondition comparison = RelayComparisonCondition::GreaterThan;
  float onThreshold = 30.0f;
  float offThreshold = 28.0f;
  uint16_t scheduleOnMinutes = 480;
  uint16_t scheduleOffMinutes = 1020;
  uint8_t enabledWeekdays = 0x7F;
};

struct RelayAutomationSettings {
  uint32_t doorLockPulseMs = 1000;
  uint32_t garageLockPulseMs = 1000;
  float outdoorLightOnBelowLux = 30.0f;
  float outdoorLightOffAboveLux = 80.0f;
  float exhaustFanOnAboveTemperature = 30.0f;
  float exhaustFanOffBelowTemperature = 28.0f;
  uint32_t motionLight1DurationMs = 60000;
  uint32_t motionLight2DurationMs = 60000;
  RelayAutomationMode outdoorLightMode = RelayAutomationMode::Manual;
  RelayAutomationMode exhaustFanMode = RelayAutomationMode::Manual;
  RelayAutomationMode motionLight1Mode = RelayAutomationMode::Manual;
  RelayAutomationMode motionLight2Mode = RelayAutomationMode::Manual;
  bool outdoorLightManualState = false;
  bool exhaustFanManualState = false;
  bool motionLight1ManualState = false;
  bool motionLight2ManualState = false;
  RelayChannelConfig channels[16];
};

struct RelayAutomationSnapshot {
  RelayAutomationSettings settings;
  bool settingsLoadedFromStorage = false;
  bool storageAvailable = false;
  bool lastSaveOk = false;
  bool doorLockActive = false;
  bool garageLockActive = false;
  bool outdoorLightOn = false;
  bool exhaustFanOn = false;
  bool motionLight1On = false;
  bool motionLight2On = false;
  uint32_t doorLockRemainingMs = 0;
  uint32_t garageLockRemainingMs = 0;
  uint32_t motionLight1RemainingMs = 0;
  uint32_t motionLight2RemainingMs = 0;
};

void relayAutomationBegin();
void relayAutomationLoop();

RelayAutomationSettings relayAutomationGetSettings();
RelayAutomationSnapshot relayAutomationGetSnapshot();
bool relayAutomationUpdateSettings(const RelayAutomationSettings &settings);
bool relayAutomationSaveSettings();
bool relayAutomationLoadSettings();

bool relayAutomationSetMode(RelayAutomationDevice device, RelayAutomationMode mode);
bool relayAutomationSetManualState(RelayAutomationDevice device, bool enabled);
bool relayAutomationToggleManualState(RelayAutomationDevice device);
bool relayAutomationPulseDoorLock();
bool relayAutomationPulseGarageLock();

const char *relayAutomationModeName(RelayAutomationMode mode);
const char *relayChannelModeName(RelayChannelMode mode);
const char *relayAutomaticControlTypeName(RelayAutomaticControlType type);
const char *relaySensorTypeName(RelaySensorType sensor);
const char *relayComparisonConditionName(RelayComparisonCondition condition);
const char *relayPulseRoleName(RelayPulseRole role);
bool relayAutomationParseDevice(const String &value, RelayAutomationDevice &device);
bool relayAutomationParseMode(const String &value, RelayAutomationMode &mode);
bool relayAutomationParseChannelMode(const String &value, RelayChannelMode &mode);
bool relayAutomationParseAutomaticControlType(const String &value, RelayAutomaticControlType &type);
bool relayAutomationParseSensorType(const String &value, RelaySensorType &sensor);
bool relayAutomationParseComparisonCondition(const String &value, RelayComparisonCondition &condition);
bool relayAutomationParsePulseRole(const String &value, RelayPulseRole &role);
bool relayAutomationChannelEnabled(uint8_t channel);
bool relayAutomationChannelAllowsPulse(uint8_t channel);
bool relayAutomationSetChannelManualState(uint8_t channel, bool enabled);
bool relayAutomationPulseChannel(uint8_t channel);

#endif
