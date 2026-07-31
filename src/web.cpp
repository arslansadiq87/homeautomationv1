#include <Arduino.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <math.h>
#include <string.h>
#include <time.h>

#include "CANBusManager.h"
#include "RS485Manager.h"
#include "fm225.h"
#include "mp3.h"
#include "modules.h"
#include "pinout.h"
#include "radar.h"
#include "rdm.h"
#include "relay_automation.h"
#include "secrets.h"
#include "web.h"
#include "web_assets_generated.h"

namespace {
WebServer server(80);

constexpr uint32_t WEB_STORAGE_MAGIC = 0x57454231;
constexpr uint16_t WEB_STORAGE_VERSION = 1;
constexpr uint8_t WEB_STORAGE_FILE_COUNT = 4;
constexpr uint32_t WEB_STORAGE_ADDRESS = W25Q128_EXPECTED_BYTES - (2UL * 1024UL * 1024UL);
constexpr uint32_t WEB_STORAGE_BYTES = 1024UL * 1024UL;
constexpr uint32_t WEB_STORAGE_HEADER_BYTES = 4096UL;
constexpr uint32_t WEB_STORAGE_SECTOR_BYTES = 4096UL;
constexpr uint32_t FM225_RADAR_SETTINGS_MAGIC = 0x46525231;
constexpr uint16_t FM225_RADAR_SETTINGS_VERSION = 2;
constexpr uint32_t FM225_RADAR_SETTINGS_ADDRESS = W25Q128_EXPECTED_BYTES - 12288UL;
constexpr uint32_t FM225_RADAR_UNLOCK_COOLDOWN_MS = 5000;
constexpr uint32_t FM225_RADAR_ATTEMPT_COOLDOWN_MS = 10000;
constexpr uint8_t FM225_RADAR_VERIFY_TIMEOUT_SEC = 5;
constexpr uint32_t TDS_SETTINGS_MAGIC = 0x54445331;
constexpr uint16_t TDS_SETTINGS_VERSION = 1;
constexpr uint32_t TDS_SETTINGS_ADDRESS = W25Q128_EXPECTED_BYTES - 16384UL;
constexpr uint32_t TDS_POLL_INTERVAL_MS = 5000;
constexpr uint32_t TDS_HTTP_TIMEOUT_MS = 150;
constexpr size_t TDS_ADDRESS_SIZE = 96;
constexpr uint32_t AIR_PURIFIER_SETTINGS_MAGIC = 0x41505231;
constexpr uint16_t AIR_PURIFIER_SETTINGS_VERSION = 1;
constexpr uint32_t AIR_PURIFIER_SETTINGS_ADDRESS = W25Q128_EXPECTED_BYTES - 73728UL;
constexpr uint32_t AIR_PURIFIER_POLL_INTERVAL_MS = 5000;
constexpr uint32_t AIR_PURIFIER_HTTP_TIMEOUT_MS = 150;
constexpr size_t AIR_PURIFIER_ADDRESS_SIZE = 96;
constexpr uint32_t INVERTER_SETTINGS_MAGIC = 0x494E5631;
constexpr uint16_t INVERTER_SETTINGS_VERSION = 1;
constexpr uint32_t INVERTER_SETTINGS_ADDRESS = W25Q128_EXPECTED_BYTES - 20480UL;
constexpr uint32_t INVERTER_MIN_INTERVAL_MS = 5000;
constexpr uint32_t INVERTER_MAX_INTERVAL_MS = 300000;
constexpr size_t INVERTER_ADDRESS_SIZE = 96;
constexpr size_t INVERTER_PASSWORD_SIZE = 32;
constexpr size_t INVERTER_HOST_SIZE = 64;
constexpr size_t GROWATT_TOKEN_SIZE = 72;
constexpr uint32_t MP3_SOUND_SETTINGS_MAGIC = 0x4D503331;
constexpr uint16_t MP3_SOUND_SETTINGS_VERSION = 2;
constexpr uint32_t MP3_SOUND_SETTINGS_ADDRESS = W25Q128_EXPECTED_BYTES - 24576UL;
constexpr uint32_t NETWORK_SETTINGS_MAGIC = 0x4E455431;
constexpr uint16_t NETWORK_SETTINGS_VERSION = 2;
constexpr uint32_t NETWORK_SETTINGS_ADDRESS = W25Q128_EXPECTED_BYTES - 28672UL;
constexpr uint32_t SECURITY_SETTINGS_MAGIC = 0x53454331;
constexpr uint16_t SECURITY_SETTINGS_VERSION = 1;
constexpr uint32_t SECURITY_SETTINGS_ADDRESS = W25Q128_EXPECTED_BYTES - 32768UL;
constexpr uint32_t LOG_SETTINGS_MAGIC = 0x4C4F4731;
constexpr uint16_t LOG_SETTINGS_VERSION = 1;
constexpr uint32_t LOG_SETTINGS_ADDRESS = W25Q128_EXPECTED_BYTES - 36864UL;
constexpr uint32_t EVENT_LOG_MAGIC = 0x45564C31;
constexpr uint16_t EVENT_LOG_VERSION = 1;
constexpr uint32_t EVENT_LOG_ADDRESS = W25Q128_EXPECTED_BYTES - 69632UL;
constexpr uint32_t EVENT_LOG_BYTES = 32768UL;
constexpr uint32_t COMMUNICATION_SETTINGS_MAGIC = 0x434F4D31;
constexpr uint16_t COMMUNICATION_SETTINGS_VERSION = 1;
constexpr uint32_t COMMUNICATION_SETTINGS_ADDRESS = W25Q128_EXPECTED_BYTES - 77824UL;
constexpr uint16_t EVENT_LOG_MAX_RECORDS = 240;
constexpr uint32_t LOG_RETENTION_SECONDS = 30UL * 24UL * 60UL * 60UL;
constexpr size_t WIFI_SSID_SIZE = 33;
constexpr size_t WIFI_PASSWORD_SIZE = 64;
constexpr size_t MDNS_HOSTNAME_SIZE = 32;
constexpr size_t SECURITY_USERNAME_SIZE = 32;
constexpr size_t SECURITY_PASSWORD_SIZE = 48;
constexpr uint32_t AUTH_COOKIE_MAX_AGE_SECONDS = 31536000UL;
constexpr uint8_t MP3_SOUND_FOLDER = 1;
constexpr uint8_t MP3_SOUND_MIN_TRACK = 1;
constexpr uint8_t MP3_SOUND_MAX_TRACK = 255;
constexpr uint8_t MP3_SOUND_MAX_VOLUME = 30;
constexpr uint16_t MQ135_ALARM_MAX_RAW = 4095;
constexpr uint32_t SOLAX_HTTP_TIMEOUT_MS = 150;
constexpr uint32_t NITROX_TCP_TIMEOUT_MS = 150;
constexpr uint32_t GROWATT_HTTP_TIMEOUT_MS = 250;
constexpr uint32_t API_FAILURE_BACKOFF_MS = 60000;
constexpr uint32_t WIFI_CONNECT_WINDOW_MS = 15000;
constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 30000;
constexpr uint32_t RELAY_COMMAND_PRIORITY_WINDOW_MS = 2500;

const char *WEB_STORAGE_PATHS[WEB_STORAGE_FILE_COUNT] = {
  "/index.html",
  "/styles.css",
  "/app.js",
  "/techpanda.png",
};

struct WebStorageEntry {
  char path[32] = {};
  uint32_t offset = 0;
  uint32_t length = 0;
  uint32_t crc = 0;
};

struct WebStorageHeader {
  uint32_t magic = WEB_STORAGE_MAGIC;
  uint16_t version = WEB_STORAGE_VERSION;
  uint16_t size = 0;
  uint8_t fileCount = 0;
  uint8_t reserved[3] = {};
  WebStorageEntry files[WEB_STORAGE_FILE_COUNT];
  uint32_t crc = 0;
};

struct DashboardSettings {
  String deviceName = "Home Automation Hub";
  bool automationEnabled = true;
  bool notificationsEnabled = true;
  int targetTemperature = 24;
  int brightness = 72;
  String mode = "auto";
};

struct Fm225RadarSettings {
  bool enabled = false;
  bool faceVerificationEnabled = true;
  uint16_t minDistanceCm = 0;
  uint8_t minEnergy = 0;
  uint8_t reserved[3] = {};
};

struct StoredFm225RadarSettings {
  uint32_t magic = FM225_RADAR_SETTINGS_MAGIC;
  uint16_t version = FM225_RADAR_SETTINGS_VERSION;
  uint16_t size = 0;
  Fm225RadarSettings settings;
  uint32_t crc = 0;
};

struct TdsMonitorSettings {
  bool enabled = false;
  char address[TDS_ADDRESS_SIZE] = "http://tds.local/api/tds";
};

struct StoredTdsMonitorSettings {
  uint32_t magic = TDS_SETTINGS_MAGIC;
  uint16_t version = TDS_SETTINGS_VERSION;
  uint16_t size = 0;
  TdsMonitorSettings settings;
  uint32_t crc = 0;
};

struct AirPurifierSettings {
  bool enabled = false;
  char address[AIR_PURIFIER_ADDRESS_SIZE] = "http://air-purifier.local/api/status";
};

struct CommunicationSettings {
  bool rs485Enabled = false;
  uint8_t reserved0[3] = {};
  uint32_t rs485BaudRate = 9600;
  bool canEnabled = false;
  uint8_t reserved1[3] = {};
  uint32_t canBitrate = 500000;
};

struct StoredCommunicationSettings {
  uint32_t magic = COMMUNICATION_SETTINGS_MAGIC;
  uint16_t version = COMMUNICATION_SETTINGS_VERSION;
  uint16_t size = 0;
  CommunicationSettings settings;
  uint32_t crc = 0;
};

struct StoredAirPurifierSettings {
  uint32_t magic = AIR_PURIFIER_SETTINGS_MAGIC;
  uint16_t version = AIR_PURIFIER_SETTINGS_VERSION;
  uint16_t size = 0;
  AirPurifierSettings settings;
  uint32_t crc = 0;
};

struct InverterMonitorSettings {
  bool solaxEnabled = HA_DEFAULT_SOLAX_ENABLED;
  char solaxAddress[INVERTER_ADDRESS_SIZE] = HA_DEFAULT_SOLAX_ADDRESS;
  char solaxPassword[INVERTER_PASSWORD_SIZE] = HA_DEFAULT_SOLAX_PASSWORD;
  uint32_t solaxIntervalMs = 10000;
  bool nitroxEnabled = HA_DEFAULT_NITROX_ENABLED;
  char nitroxHost[INVERTER_HOST_SIZE] = HA_DEFAULT_NITROX_HOST;
  uint16_t nitroxPort = 8899;
  uint32_t nitroxLoggerSerial = 1732083940UL;
  uint8_t nitroxSlaveId = 1;
  uint8_t reserved[3] = {};
  uint32_t nitroxIntervalMs = 10000;
  bool growattEnabled = HA_DEFAULT_GROWATT_ENABLED;
  char growattBaseUrl[INVERTER_ADDRESS_SIZE] = "https://openapi.growatt.com/v1/";
  char growattToken[GROWATT_TOKEN_SIZE] = HA_DEFAULT_GROWATT_TOKEN;
  uint32_t growattPlantId = 0;
  uint32_t growattIntervalMs = 300000;
};

struct StoredInverterMonitorSettings {
  uint32_t magic = INVERTER_SETTINGS_MAGIC;
  uint16_t version = INVERTER_SETTINGS_VERSION;
  uint16_t size = 0;
  InverterMonitorSettings settings;
  uint32_t crc = 0;
};

struct Mp3SoundSettings {
  uint8_t volume = 25;
  bool startupSoundEnabled = false;
  uint8_t startupTrack = 1;
  bool smokeAlarmEnabled = false;
  uint8_t smokeAlarmTrack = 2;
  uint16_t smokeAlarmThresholdRaw = 2000;
  uint8_t reserved[1] = {};
};

struct StoredMp3SoundSettings {
  uint32_t magic = MP3_SOUND_SETTINGS_MAGIC;
  uint16_t version = MP3_SOUND_SETTINGS_VERSION;
  uint16_t size = 0;
  Mp3SoundSettings settings;
  uint32_t crc = 0;
};

struct NetworkSettings {
  char ssid[WIFI_SSID_SIZE] = HA_DEFAULT_WIFI_SSID;
  char password[WIFI_PASSWORD_SIZE] = HA_DEFAULT_WIFI_PASSWORD;
  char mdnsHostname[MDNS_HOSTNAME_SIZE] = HA_DEFAULT_MDNS_HOSTNAME;
  bool otaEnabled = false;
  uint8_t reserved[3] = {};
};

struct StoredNetworkSettings {
  uint32_t magic = NETWORK_SETTINGS_MAGIC;
  uint16_t version = NETWORK_SETTINGS_VERSION;
  uint16_t size = 0;
  NetworkSettings settings;
  uint32_t crc = 0;
};

struct LegacyNetworkSettings {
  char ssid[WIFI_SSID_SIZE] = HA_DEFAULT_WIFI_SSID;
  char password[WIFI_PASSWORD_SIZE] = HA_DEFAULT_WIFI_PASSWORD;
  char mdnsHostname[MDNS_HOSTNAME_SIZE] = HA_DEFAULT_MDNS_HOSTNAME;
};

struct StoredLegacyNetworkSettings {
  uint32_t magic = NETWORK_SETTINGS_MAGIC;
  uint16_t version = 1;
  uint16_t size = 0;
  LegacyNetworkSettings settings;
  uint32_t crc = 0;
};

struct SecuritySettings {
  bool loginEnabled = false;
  char username[SECURITY_USERNAME_SIZE] = "user";
  char password[SECURITY_PASSWORD_SIZE] = "";
  uint8_t reserved[3] = {};
};

struct StoredSecuritySettings {
  uint32_t magic = SECURITY_SETTINGS_MAGIC;
  uint16_t version = SECURITY_SETTINGS_VERSION;
  uint16_t size = 0;
  SecuritySettings settings;
  uint32_t crc = 0;
};

struct LogSettings {
  bool rfid = false;
  bool fm225 = false;
  bool doorReed = false;
  bool garageReed = false;
  bool doorUnlock = false;
  bool garageUnlock = false;
  uint8_t reserved[2] = {};
};

struct StoredLogSettings {
  uint32_t magic = LOG_SETTINGS_MAGIC;
  uint16_t version = LOG_SETTINGS_VERSION;
  uint16_t size = 0;
  LogSettings settings;
  uint32_t crc = 0;
};

struct EventLogRecord {
  uint32_t uptimeSeconds = 0;
  char category[16] = {};
  char message[84] = {};
};

struct EventLogHeader {
  uint32_t magic = EVENT_LOG_MAGIC;
  uint16_t version = EVENT_LOG_VERSION;
  uint16_t size = 0;
  uint16_t count = 0;
  uint16_t reserved = 0;
  uint32_t crc = 0;
};

struct TdsMonitorSnapshot {
  bool enabled = false;
  bool online = false;
  String address = "http://tds.local/api/tds";
  String lastEvent = "TDS monitor disabled";
  float tdsPpm = NAN;
  float voltage = NAN;
  float tempC = NAN;
  float tempFallbackC = NAN;
  String waterLevelLabel;
  float waterLevelInches = NAN;
  float waterLevelPercent = NAN;
  float waterVolumeLiters = NAN;
  float tankCapacityLiters = NAN;
  uint32_t sampleAgeMs = 0;
  uint32_t lastPollMs = 0;
  uint32_t lastSuccessMs = 0;
  uint32_t nextPollMs = 0;
};

struct AirPurifierSnapshot {
  bool enabled = false;
  bool online = false;
  bool purifierOn = false;
  bool fanOn = false;
  bool autoMode = false;
  bool sensorOnline = false;
  bool pmsPowered = false;
  String address = "http://air-purifier.local/api/status";
  String lastEvent = "Air purifier disabled";
  float pm1 = NAN;
  float pm25 = NAN;
  float pm10 = NAN;
  uint8_t aq = 0;
  uint8_t fanSpeedPct = 0;
  uint32_t sampleAgeMs = 0;
  uint32_t lastPollMs = 0;
  uint32_t lastSuccessMs = 0;
  uint32_t nextPollMs = 0;
};

struct InverterFlowSnapshot {
  const char *name = "";
  bool online = false;
  String lastEvent = "Waiting for inverter data";
  uint32_t lastPollMs = 0;
  uint32_t lastSuccessMs = 0;
  uint32_t nextPollMs = 0;
  float pvPowerW = NAN;
  float gridPowerW = NAN;
  float gridVoltageV = NAN;
  float homePowerW = NAN;
  float batteryPowerW = NAN;
  float batterySoc = NAN;
  float todayYieldKwh = NAN;
  float totalEnergyKwh = NAN;
};

struct LegacyFm225RadarSettings {
  bool enabled = false;
  uint16_t minDistanceCm = 0;
  uint8_t minEnergy = 0;
  uint8_t reserved = 0;
};

struct StoredLegacyFm225RadarSettings {
  uint32_t magic = FM225_RADAR_SETTINGS_MAGIC;
  uint16_t version = 1;
  uint16_t size = 0;
  LegacyFm225RadarSettings settings;
  uint32_t crc = 0;
};

DashboardSettings settings;

String relayConfigsJson(const RelayAutomationSettings &relaySettings);
void updateRelayChannelSettingsFromForm(RelayAutomationSettings &relaySettings);
Fm225RadarSettings fm225RadarSettings;
TdsMonitorSettings tdsSettings;
AirPurifierSettings airPurifierSettings;
CommunicationSettings communicationSettings;
InverterMonitorSettings inverterSettings;
Mp3SoundSettings mp3SoundSettings;
NetworkSettings networkSettings;
SecuritySettings securitySettings;
LogSettings logSettings;
EventLogRecord eventLogScratch[EVENT_LOG_MAX_RECORDS];
TdsMonitorSnapshot tdsSnapshot;
AirPurifierSnapshot airPurifierSnapshot;
InverterFlowSnapshot solaxSnapshot{"Solax"};
InverterFlowSnapshot nitroxSnapshot{"Nitrox"};
InverterFlowSnapshot growattSnapshot{"Growatt"};
String fm225LastStatus = "UART ready";
String fm225LastEvent = "Waiting for FM225 response";
String fm225LastRecognizedName;
uint16_t fm225LastRecognizedUserId = 0;
uint8_t fm225LastEnrollResult = 0xFF;
uint8_t fm225LastVerifyResult = 0xFF;
uint32_t fm225LastEventMs = 0;
String fm225VerifySummary = "Verification not started";
bool fm225VerifyPending = false;
String fm225RadarStatus = "Radar presence waiting";
bool fm225RadarVerifyActive = false;
uint32_t fm225RadarVerifyStartedMs = 0;
uint32_t fm225RadarLastUnlockMs = 0;
uint32_t fm225RadarLastAttemptMs = 0;
bool eventLogDoorReedKnown = false;
bool eventLogGarageReedKnown = false;
bool eventLogDoorReedClosed = false;
bool eventLogGarageReedClosed = false;
bool eventLogDoorUnlockActive = false;
bool eventLogGarageUnlockActive = false;
bool eventLogRfidKnown = false;
uint32_t eventLogRfidReads = 0;
bool webStorageLastSeedOk = false;
String webStorageLastEvent = "Winbond web storage not seeded";
uint32_t relayCommandPriorityUntilMs = 0;
bool inverterSettingsLastSaveOk = true;
bool smokeAlarmWasActive = false;
bool otaServiceStarted = false;
bool mdnsServiceStarted = false;
bool wifiConnectActive = false;
bool fallbackApStarted = false;
uint32_t wifiConnectStartedMs = 0;
uint32_t wifiLastRetryMs = 0;

String currentIpString();
String formString(const char *name, const String &fallback = String());

uint32_t fnv1aUpdate(uint32_t hash, const uint8_t *data, size_t length) {
  for (size_t i = 0; i < length; i++) {
    hash ^= data[i];
    hash *= 16777619UL;
  }
  return hash;
}

uint32_t fnv1a(const uint8_t *data, size_t length) {
  return fnv1aUpdate(2166136261UL, data, length);
}

uint32_t webStorageHeaderCrc(const WebStorageHeader &header) {
  return fnv1a(reinterpret_cast<const uint8_t *>(&header), sizeof(WebStorageHeader) - sizeof(header.crc));
}

uint32_t fm225RadarSettingsCrc(const StoredFm225RadarSettings &stored) {
  return fnv1a(reinterpret_cast<const uint8_t *>(&stored), sizeof(StoredFm225RadarSettings) - sizeof(stored.crc));
}

uint32_t legacyFm225RadarSettingsCrc(const StoredLegacyFm225RadarSettings &stored) {
  return fnv1a(reinterpret_cast<const uint8_t *>(&stored), sizeof(StoredLegacyFm225RadarSettings) - sizeof(stored.crc));
}

uint32_t tdsSettingsCrc(const StoredTdsMonitorSettings &stored) {
  return fnv1a(reinterpret_cast<const uint8_t *>(&stored), sizeof(StoredTdsMonitorSettings) - sizeof(stored.crc));
}

uint32_t airPurifierSettingsCrc(const StoredAirPurifierSettings &stored) {
  return fnv1a(reinterpret_cast<const uint8_t *>(&stored), sizeof(StoredAirPurifierSettings) - sizeof(stored.crc));
}

uint32_t communicationSettingsCrc(const StoredCommunicationSettings &stored) {
  return fnv1a(reinterpret_cast<const uint8_t *>(&stored), sizeof(StoredCommunicationSettings) - sizeof(stored.crc));
}

uint32_t inverterSettingsCrc(const StoredInverterMonitorSettings &stored) {
  return fnv1a(reinterpret_cast<const uint8_t *>(&stored), sizeof(StoredInverterMonitorSettings) - sizeof(stored.crc));
}

uint32_t mp3SoundSettingsCrc(const StoredMp3SoundSettings &stored) {
  return fnv1a(reinterpret_cast<const uint8_t *>(&stored), sizeof(StoredMp3SoundSettings) - sizeof(stored.crc));
}

uint32_t networkSettingsCrc(const StoredNetworkSettings &stored) {
  return fnv1a(reinterpret_cast<const uint8_t *>(&stored), sizeof(StoredNetworkSettings) - sizeof(stored.crc));
}

uint32_t legacyNetworkSettingsCrc(const StoredLegacyNetworkSettings &stored) {
  return fnv1a(reinterpret_cast<const uint8_t *>(&stored), sizeof(StoredLegacyNetworkSettings) - sizeof(stored.crc));
}

uint32_t securitySettingsCrc(const StoredSecuritySettings &stored) {
  return fnv1a(reinterpret_cast<const uint8_t *>(&stored), sizeof(StoredSecuritySettings) - sizeof(stored.crc));
}

uint32_t logSettingsCrc(const StoredLogSettings &stored) {
  return fnv1a(reinterpret_cast<const uint8_t *>(&stored), sizeof(StoredLogSettings) - sizeof(stored.crc));
}

uint32_t eventLogCrc(const EventLogHeader &header, const EventLogRecord *records) {
  uint32_t hash = fnv1a(reinterpret_cast<const uint8_t *>(&header), sizeof(EventLogHeader) - sizeof(header.crc));
  return fnv1aUpdate(hash, reinterpret_cast<const uint8_t *>(records), sizeof(EventLogRecord) * header.count);
}

String sanitizeLoginText(String value, const char *fallback) {
  value.trim();
  if (value.length() == 0) {
    value = fallback;
  }
  return value;
}

void sanitizeSecuritySettings(SecuritySettings &value) {
  const bool requestedLogin = value.loginEnabled;
  String username = sanitizeLoginText(String(value.username), "user");
  memset(value.username, 0, sizeof(value.username));
  username.substring(0, SECURITY_USERNAME_SIZE - 1).toCharArray(value.username, SECURITY_USERNAME_SIZE);

  String password = String(value.password);
  password.trim();
  memset(value.password, 0, sizeof(value.password));
  password.substring(0, SECURITY_PASSWORD_SIZE - 1).toCharArray(value.password, SECURITY_PASSWORD_SIZE);

  value.loginEnabled = requestedLogin && strlen(value.username) > 0 && strlen(value.password) > 0;

  value.reserved[0] = 0;
  value.reserved[1] = 0;
  value.reserved[2] = 0;
}

void sanitizeLogSettings(LogSettings &value) {
  value.reserved[0] = 0;
  value.reserved[1] = 0;
}

String authToken() {
  uint32_t hash = 2166136261UL;
  hash = fnv1aUpdate(hash, reinterpret_cast<const uint8_t *>(securitySettings.username), strlen(securitySettings.username));
  hash = fnv1aUpdate(hash, reinterpret_cast<const uint8_t *>(":"), 1);
  hash = fnv1aUpdate(hash, reinterpret_cast<const uint8_t *>(securitySettings.password), strlen(securitySettings.password));
  hash = fnv1aUpdate(hash, reinterpret_cast<const uint8_t *>(":home-automation"), 16);
  return String(hash, HEX);
}

String sanitizeMdnsHostname(String hostname) {
  hostname.trim();
  hostname.toLowerCase();
  String clean;
  bool previousDash = false;
  for (size_t i = 0; i < hostname.length() && clean.length() < MDNS_HOSTNAME_SIZE - 1; i++) {
    const char c = hostname.charAt(i);
    const bool allowed = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    if (!allowed) {
      continue;
    }
    if (c == '-' && (clean.length() == 0 || previousDash)) {
      continue;
    }
    clean += c;
    previousDash = c == '-';
  }
  while (clean.endsWith("-")) {
    clean.remove(clean.length() - 1);
  }
  if (clean.length() == 0) {
    clean = HA_DEFAULT_MDNS_HOSTNAME;
    clean.trim();
    clean.toLowerCase();
    if (clean.length() == 0) {
      clean = "home-automation";
    }
  }
  return clean;
}

void sanitizeNetworkSettings(NetworkSettings &value) {
  String ssid = String(value.ssid);
  ssid.trim();
  memset(value.ssid, 0, sizeof(value.ssid));
  ssid.substring(0, WIFI_SSID_SIZE - 1).toCharArray(value.ssid, WIFI_SSID_SIZE);

  String password = String(value.password);
  password.trim();
  memset(value.password, 0, sizeof(value.password));
  password.substring(0, WIFI_PASSWORD_SIZE - 1).toCharArray(value.password, WIFI_PASSWORD_SIZE);

  const String hostname = sanitizeMdnsHostname(String(value.mdnsHostname));
  memset(value.mdnsHostname, 0, sizeof(value.mdnsHostname));
  hostname.substring(0, MDNS_HOSTNAME_SIZE - 1).toCharArray(value.mdnsHostname, MDNS_HOSTNAME_SIZE);
  value.reserved[0] = 0;
  value.reserved[1] = 0;
  value.reserved[2] = 0;
}

void sanitizeMp3SoundSettings(Mp3SoundSettings &value) {
  const Mp3SoundSettings defaults;
  value.volume = constrain(value.volume, static_cast<uint8_t>(0), MP3_SOUND_MAX_VOLUME);
  value.startupTrack = constrain(value.startupTrack == 0 ? defaults.startupTrack : value.startupTrack,
                                 MP3_SOUND_MIN_TRACK, MP3_SOUND_MAX_TRACK);
  value.smokeAlarmTrack = constrain(value.smokeAlarmTrack == 0 ? defaults.smokeAlarmTrack : value.smokeAlarmTrack,
                                    MP3_SOUND_MIN_TRACK, MP3_SOUND_MAX_TRACK);
  value.smokeAlarmThresholdRaw = constrain(value.smokeAlarmThresholdRaw, static_cast<uint16_t>(0), MQ135_ALARM_MAX_RAW);
  value.reserved[0] = 0;
}

void sanitizeCommunicationSettings(CommunicationSettings &value) {
  const uint32_t rs485BaudRates[] = {9600, 19200, 38400, 57600, 115200};
  bool validRs485 = false;
  for (const uint32_t baudRate : rs485BaudRates) {
    validRs485 = validRs485 || value.rs485BaudRate == baudRate;
  }
  if (!validRs485) {
    value.rs485BaudRate = 9600;
  }

  const uint32_t canBitrates[] = {125000, 250000, 500000, 1000000};
  bool validCan = false;
  for (const uint32_t bitrate : canBitrates) {
    validCan = validCan || value.canBitrate == bitrate;
  }
  if (!validCan) {
    value.canBitrate = 500000;
  }
  memset(value.reserved0, 0, sizeof(value.reserved0));
  memset(value.reserved1, 0, sizeof(value.reserved1));
}

void sanitizeFm225RadarSettings(Fm225RadarSettings &value) {
  value.faceVerificationEnabled = true;
  value.minDistanceCm = constrain(value.minDistanceCm, static_cast<uint16_t>(0), static_cast<uint16_t>(1000));
  value.minEnergy = constrain(value.minEnergy, static_cast<uint8_t>(0), static_cast<uint8_t>(100));
  value.reserved[0] = 0;
  value.reserved[1] = 0;
  value.reserved[2] = 0;
}

String normalizeTdsAddress(String address) {
  address.trim();
  if (address.length() == 0) {
    address = "http://tds.local/api/tds";
  }
  if (!address.startsWith("http://") && !address.startsWith("https://")) {
    address = "http://" + address;
  }
  if (address.endsWith("/")) {
    address.remove(address.length() - 1);
  }
  if (!address.endsWith("/api/tds")) {
    address += "/api/tds";
  }
  return address;
}

String normalizeAirPurifierAddress(String address) {
  address.trim();
  if (address.length() == 0) {
    address = "http://air-purifier.local/api/status";
  }
  if (!address.startsWith("http://") && !address.startsWith("https://")) {
    address = "http://" + address;
  }
  if (address.endsWith("/")) {
    address.remove(address.length() - 1);
  }
  if (!address.endsWith("/api/status")) {
    address += "/api/status";
  }
  return address;
}

String normalizeSolaxAddress(String address) {
  address.trim();
  if (address.length() == 0) {
    address = HA_DEFAULT_SOLAX_ADDRESS;
  }
  if (!address.startsWith("http://") && !address.startsWith("https://")) {
    address = "http://" + address;
  }
  if (!address.endsWith("/")) {
    address += "/";
  }
  return address;
}

String normalizeGrowattBaseUrl(String address) {
  address.trim();
  if (address.length() == 0) {
    address = "https://openapi.growatt.com/v1/";
  }
  if (!address.startsWith("http://") && !address.startsWith("https://")) {
    address = "https://" + address;
  }
  if (!address.endsWith("/")) {
    address += "/";
  }
  return address;
}

void sanitizeTdsSettings(TdsMonitorSettings &value) {
  const String normalized = normalizeTdsAddress(String(value.address));
  memset(value.address, 0, sizeof(value.address));
  normalized.substring(0, TDS_ADDRESS_SIZE - 1).toCharArray(value.address, TDS_ADDRESS_SIZE);
}

void sanitizeAirPurifierSettings(AirPurifierSettings &value) {
  const String normalized = normalizeAirPurifierAddress(String(value.address));
  memset(value.address, 0, sizeof(value.address));
  normalized.substring(0, AIR_PURIFIER_ADDRESS_SIZE - 1).toCharArray(value.address, AIR_PURIFIER_ADDRESS_SIZE);
}

void sanitizeInverterSettings(InverterMonitorSettings &value) {
  const String solaxAddress = normalizeSolaxAddress(String(value.solaxAddress));
  memset(value.solaxAddress, 0, sizeof(value.solaxAddress));
  solaxAddress.substring(0, INVERTER_ADDRESS_SIZE - 1).toCharArray(value.solaxAddress, INVERTER_ADDRESS_SIZE);

  String solaxPassword = String(value.solaxPassword);
  solaxPassword.trim();
  memset(value.solaxPassword, 0, sizeof(value.solaxPassword));
  solaxPassword.substring(0, INVERTER_PASSWORD_SIZE - 1).toCharArray(value.solaxPassword, INVERTER_PASSWORD_SIZE);
  value.solaxIntervalMs = constrain(value.solaxIntervalMs, INVERTER_MIN_INTERVAL_MS, INVERTER_MAX_INTERVAL_MS);

  String nitroxHost = String(value.nitroxHost);
  nitroxHost.trim();
  if (nitroxHost.length() == 0) {
    nitroxHost = HA_DEFAULT_NITROX_HOST;
  }
  if (nitroxHost.startsWith("http://")) {
    nitroxHost.remove(0, 7);
  } else if (nitroxHost.startsWith("https://")) {
    nitroxHost.remove(0, 8);
  }
  const int slash = nitroxHost.indexOf('/');
  if (slash >= 0) {
    nitroxHost.remove(slash);
  }
  memset(value.nitroxHost, 0, sizeof(value.nitroxHost));
  nitroxHost.substring(0, INVERTER_HOST_SIZE - 1).toCharArray(value.nitroxHost, INVERTER_HOST_SIZE);

  if (value.nitroxPort == 0) {
    value.nitroxPort = 8899;
  }
  if (value.nitroxLoggerSerial == 0) {
    value.nitroxLoggerSerial = 1732083940UL;
  }
  value.nitroxSlaveId = constrain(value.nitroxSlaveId, static_cast<uint8_t>(1), static_cast<uint8_t>(247));
  value.reserved[0] = 0;
  value.reserved[1] = 0;
  value.reserved[2] = 0;
  value.nitroxIntervalMs = constrain(value.nitroxIntervalMs, INVERTER_MIN_INTERVAL_MS, INVERTER_MAX_INTERVAL_MS);

  const String growattBaseUrl = normalizeGrowattBaseUrl(String(value.growattBaseUrl));
  memset(value.growattBaseUrl, 0, sizeof(value.growattBaseUrl));
  growattBaseUrl.substring(0, INVERTER_ADDRESS_SIZE - 1).toCharArray(value.growattBaseUrl, INVERTER_ADDRESS_SIZE);

  String growattToken = String(value.growattToken);
  growattToken.trim();
  memset(value.growattToken, 0, sizeof(value.growattToken));
  growattToken.substring(0, GROWATT_TOKEN_SIZE - 1).toCharArray(value.growattToken, GROWATT_TOKEN_SIZE);
  value.growattIntervalMs = constrain(value.growattIntervalMs, 60000UL, 3600000UL);
}

String jsonEscape(const String &value) {
  String escaped;
  escaped.reserve(value.length() + 8);

  for (size_t i = 0; i < value.length(); i++) {
    const char c = value[i];
    if (c == '"' || c == '\\') {
      escaped += '\\';
    }
    escaped += c;
  }

  return escaped;
}

bool jsonNumberField(const String &json, const char *field, float &out) {
  const String key = "\"" + String(field) + "\":";
  int pos = json.indexOf(key);
  if (pos < 0) {
    return false;
  }
  pos += key.length();
  while (pos < json.length() && isspace(static_cast<unsigned char>(json[pos]))) {
    pos++;
  }
  if (json.substring(pos, pos + 4) == "null") {
    return false;
  }
  int end = pos;
  while (end < json.length()) {
    const char c = json[end];
    if (!(isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E')) {
      break;
    }
    end++;
  }
  if (end == pos) {
    return false;
  }
  out = json.substring(pos, end).toFloat();
  return true;
}

bool jsonStringField(const String &json, const char *field, String &out) {
  const String key = "\"" + String(field) + "\":";
  int pos = json.indexOf(key);
  if (pos < 0) {
    return false;
  }
  pos += key.length();
  while (pos < json.length() && isspace(static_cast<unsigned char>(json[pos]))) {
    pos++;
  }
  if (pos >= json.length() || json[pos] != '"') {
    return false;
  }
  pos++;
  String value;
  while (pos < json.length()) {
    const char c = json[pos++];
    if (c == '"') {
      out = value;
      return true;
    }
    if (c == '\\' && pos < json.length()) {
      value += json[pos++];
    } else {
      value += c;
    }
  }
  return false;
}

int16_t signed16(uint16_t value) {
  return value > 32767 ? static_cast<int16_t>(static_cast<int32_t>(value) - 65536) : static_cast<int16_t>(value);
}

bool jsonArrayNumberAt(const String &json, const char *field, size_t index, float &out) {
  const String key = "\"" + String(field) + "\":";
  int pos = json.indexOf(key);
  if (pos < 0) return false;
  pos = json.indexOf('[', pos + key.length());
  if (pos < 0) return false;
  pos++;

  size_t current = 0;
  while (pos < json.length()) {
    while (pos < json.length() && (isspace(static_cast<unsigned char>(json[pos])) || json[pos] == ',')) pos++;
    if (pos >= json.length() || json[pos] == ']') return false;
    int end = pos;
    while (end < json.length()) {
      const char c = json[end];
      if (!(isdigit(static_cast<unsigned char>(c)) || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E')) break;
      end++;
    }
    if (end == pos) return false;
    if (current == index) {
      out = json.substring(pos, end).toFloat();
      return true;
    }
    current++;
    pos = end;
  }
  return false;
}

String jsonFloatValue(float value, uint8_t decimals) {
  return isfinite(value) ? String(static_cast<double>(value), static_cast<unsigned int>(decimals)) : "null";
}

String contentTypeFor(const String &path) {
  if (path.endsWith(".html")) {
    return "text/html";
  }
  if (path.endsWith(".css")) {
    return "text/css";
  }
  if (path.endsWith(".js")) {
    return "application/javascript";
  }
  if (path.endsWith(".json")) {
    return "application/json";
  }
  if (path.endsWith(".ico")) {
    return "image/x-icon";
  }
  if (path.endsWith(".png")) {
    return "image/png";
  }
  return "text/plain";
}

String jsonFloatOrNull(float value, uint8_t decimals) {
  if (isnan(value)) {
    return "null";
  }

  return String(static_cast<double>(value), static_cast<unsigned int>(decimals));
}

String jsonStringArray(const std::vector<uint16_t> &ids) {
  String json = "[";
  for (size_t i = 0; i < ids.size(); i++) {
    if (i > 0) {
      json += ",";
    }

    const uint16_t id = ids[i];
    json += "{\"id\":" + String(id) + ",\"name\":\"" + jsonEscape(FM225::getFetchedUserName(id)) + "\"}";
  }
  json += "]";
  return json;
}

String jsonRfidTagArray(const std::vector<String> &tags) {
  String json = "[";
  for (size_t i = 0; i < tags.size(); i++) {
    if (i > 0) {
      json += ",";
    }
    json += "\"" + jsonEscape(tags[i]) + "\"";
  }
  json += "]";
  return json;
}

void appendEventLog(const char *category, const String &message);

void setFm225Event(const String &event) {
  fm225LastEvent = event;
  fm225LastEventMs = millis();
  if (logSettings.fm225) {
    appendEventLog("fm225", event);
  }
}

void setFm225RadarStatus(const String &status) {
  if (fm225RadarStatus == status) {
    return;
  }
  fm225RadarStatus = status;
  setFm225Event(status);
}

bool saveFm225RadarSettings();

bool loadFm225RadarSettings() {
  StoredFm225RadarSettings stored;
  if (storageReadBytes(FM225_RADAR_SETTINGS_ADDRESS, reinterpret_cast<uint8_t *>(&stored), sizeof(stored)) &&
      stored.magic == FM225_RADAR_SETTINGS_MAGIC && stored.version == FM225_RADAR_SETTINGS_VERSION &&
      stored.size == sizeof(StoredFm225RadarSettings) && stored.crc == fm225RadarSettingsCrc(stored)) {
    fm225RadarSettings = stored.settings;
    sanitizeFm225RadarSettings(fm225RadarSettings);
    return true;
  }

  StoredLegacyFm225RadarSettings legacy;
  if (storageReadBytes(FM225_RADAR_SETTINGS_ADDRESS, reinterpret_cast<uint8_t *>(&legacy), sizeof(legacy)) &&
      legacy.magic == FM225_RADAR_SETTINGS_MAGIC && legacy.version == 1 &&
      legacy.size == sizeof(StoredLegacyFm225RadarSettings) && legacy.crc == legacyFm225RadarSettingsCrc(legacy)) {
    fm225RadarSettings = Fm225RadarSettings();
    fm225RadarSettings.enabled = legacy.settings.enabled;
    fm225RadarSettings.faceVerificationEnabled = true;
    fm225RadarSettings.minDistanceCm = legacy.settings.minDistanceCm;
    fm225RadarSettings.minEnergy = legacy.settings.minEnergy;
    sanitizeFm225RadarSettings(fm225RadarSettings);
    saveFm225RadarSettings();
    return true;
  }

  fm225RadarSettings = Fm225RadarSettings();
  sanitizeFm225RadarSettings(fm225RadarSettings);
  return false;
}

bool saveFm225RadarSettings() {
  sanitizeFm225RadarSettings(fm225RadarSettings);

  StoredFm225RadarSettings stored;
  stored.size = sizeof(StoredFm225RadarSettings);
  stored.settings = fm225RadarSettings;
  stored.crc = fm225RadarSettingsCrc(stored);

  return storageEraseSector(FM225_RADAR_SETTINGS_ADDRESS) &&
         storageWriteBytes(FM225_RADAR_SETTINGS_ADDRESS, reinterpret_cast<const uint8_t *>(&stored), sizeof(stored));
}

bool loadTdsMonitorSettings() {
  StoredTdsMonitorSettings stored;
  if (!storageReadBytes(TDS_SETTINGS_ADDRESS, reinterpret_cast<uint8_t *>(&stored), sizeof(stored)) ||
      stored.magic != TDS_SETTINGS_MAGIC || stored.version != TDS_SETTINGS_VERSION ||
      stored.size != sizeof(StoredTdsMonitorSettings) || stored.crc != tdsSettingsCrc(stored)) {
    tdsSettings = TdsMonitorSettings();
    sanitizeTdsSettings(tdsSettings);
    return false;
  }

  tdsSettings = stored.settings;
  sanitizeTdsSettings(tdsSettings);
  return true;
}

bool saveTdsMonitorSettings() {
  sanitizeTdsSettings(tdsSettings);

  StoredTdsMonitorSettings stored;
  stored.size = sizeof(StoredTdsMonitorSettings);
  stored.settings = tdsSettings;
  stored.crc = tdsSettingsCrc(stored);

  return storageEraseSector(TDS_SETTINGS_ADDRESS) &&
         storageWriteBytes(TDS_SETTINGS_ADDRESS, reinterpret_cast<const uint8_t *>(&stored), sizeof(stored));
}

bool loadAirPurifierSettings() {
  StoredAirPurifierSettings stored;
  if (!storageReadBytes(AIR_PURIFIER_SETTINGS_ADDRESS, reinterpret_cast<uint8_t *>(&stored), sizeof(stored)) ||
      stored.magic != AIR_PURIFIER_SETTINGS_MAGIC || stored.version != AIR_PURIFIER_SETTINGS_VERSION ||
      stored.size != sizeof(StoredAirPurifierSettings) || stored.crc != airPurifierSettingsCrc(stored)) {
    airPurifierSettings = AirPurifierSettings();
    sanitizeAirPurifierSettings(airPurifierSettings);
    return false;
  }

  airPurifierSettings = stored.settings;
  sanitizeAirPurifierSettings(airPurifierSettings);
  return true;
}

bool saveAirPurifierSettings() {
  sanitizeAirPurifierSettings(airPurifierSettings);

  StoredAirPurifierSettings stored;
  stored.size = sizeof(StoredAirPurifierSettings);
  stored.settings = airPurifierSettings;
  stored.crc = airPurifierSettingsCrc(stored);

  return storageEraseSector(AIR_PURIFIER_SETTINGS_ADDRESS) &&
         storageWriteBytes(AIR_PURIFIER_SETTINGS_ADDRESS, reinterpret_cast<const uint8_t *>(&stored), sizeof(stored));
}

bool loadCommunicationSettings() {
  StoredCommunicationSettings stored;
  if (!storageReadBytes(COMMUNICATION_SETTINGS_ADDRESS, reinterpret_cast<uint8_t *>(&stored), sizeof(stored)) ||
      stored.magic != COMMUNICATION_SETTINGS_MAGIC || stored.version != COMMUNICATION_SETTINGS_VERSION ||
      stored.size != sizeof(StoredCommunicationSettings) || stored.crc != communicationSettingsCrc(stored)) {
    communicationSettings = CommunicationSettings();
    sanitizeCommunicationSettings(communicationSettings);
    return false;
  }

  communicationSettings = stored.settings;
  sanitizeCommunicationSettings(communicationSettings);
  return true;
}

bool saveCommunicationSettings() {
  sanitizeCommunicationSettings(communicationSettings);

  StoredCommunicationSettings stored;
  stored.size = sizeof(StoredCommunicationSettings);
  stored.settings = communicationSettings;
  stored.crc = communicationSettingsCrc(stored);

  return storageEraseSector(COMMUNICATION_SETTINGS_ADDRESS) &&
         storageWriteBytes(COMMUNICATION_SETTINGS_ADDRESS, reinterpret_cast<const uint8_t *>(&stored), sizeof(stored));
}

void applyCommunicationSettings() {
  sanitizeCommunicationSettings(communicationSettings);
  rs485SetBaudRate(communicationSettings.rs485BaudRate);
  rs485SetEnabled(communicationSettings.rs485Enabled);
  canBusSetBitrate(communicationSettings.canBitrate);
  canBusSetEnabled(communicationSettings.canEnabled);
}

bool loadInverterSettings() {
  StoredInverterMonitorSettings stored;
  if (!storageReadBytes(INVERTER_SETTINGS_ADDRESS, reinterpret_cast<uint8_t *>(&stored), sizeof(stored)) ||
      stored.magic != INVERTER_SETTINGS_MAGIC || stored.version != INVERTER_SETTINGS_VERSION ||
      stored.size != sizeof(StoredInverterMonitorSettings) || stored.crc != inverterSettingsCrc(stored)) {
    inverterSettings = InverterMonitorSettings();
    sanitizeInverterSettings(inverterSettings);
    return false;
  }

  inverterSettings = stored.settings;
  sanitizeInverterSettings(inverterSettings);
  return true;
}

bool saveInverterSettings() {
  sanitizeInverterSettings(inverterSettings);

  StoredInverterMonitorSettings stored;
  stored.size = sizeof(StoredInverterMonitorSettings);
  stored.settings = inverterSettings;
  stored.crc = inverterSettingsCrc(stored);

  return storageEraseSector(INVERTER_SETTINGS_ADDRESS) &&
         storageWriteBytes(INVERTER_SETTINGS_ADDRESS, reinterpret_cast<const uint8_t *>(&stored), sizeof(stored));
}

bool loadMp3SoundSettings() {
  StoredMp3SoundSettings stored;
  if (!storageReadBytes(MP3_SOUND_SETTINGS_ADDRESS, reinterpret_cast<uint8_t *>(&stored), sizeof(stored)) ||
      stored.magic != MP3_SOUND_SETTINGS_MAGIC || stored.version != MP3_SOUND_SETTINGS_VERSION ||
      stored.size != sizeof(StoredMp3SoundSettings) || stored.crc != mp3SoundSettingsCrc(stored)) {
    mp3SoundSettings = Mp3SoundSettings();
    sanitizeMp3SoundSettings(mp3SoundSettings);
    return false;
  }

  mp3SoundSettings = stored.settings;
  sanitizeMp3SoundSettings(mp3SoundSettings);
  return true;
}

bool saveMp3SoundSettings() {
  sanitizeMp3SoundSettings(mp3SoundSettings);

  StoredMp3SoundSettings stored;
  stored.size = sizeof(StoredMp3SoundSettings);
  stored.settings = mp3SoundSettings;
  stored.crc = mp3SoundSettingsCrc(stored);

  return storageEraseSector(MP3_SOUND_SETTINGS_ADDRESS) &&
         storageWriteBytes(MP3_SOUND_SETTINGS_ADDRESS, reinterpret_cast<const uint8_t *>(&stored), sizeof(stored));
}

bool saveNetworkSettings();

bool loadNetworkSettings() {
  StoredNetworkSettings stored;
  if (storageReadBytes(NETWORK_SETTINGS_ADDRESS, reinterpret_cast<uint8_t *>(&stored), sizeof(stored)) &&
      stored.magic == NETWORK_SETTINGS_MAGIC && stored.version == NETWORK_SETTINGS_VERSION &&
      stored.size == sizeof(StoredNetworkSettings) && stored.crc == networkSettingsCrc(stored)) {
    networkSettings = stored.settings;
    sanitizeNetworkSettings(networkSettings);
    return true;
  }

  StoredLegacyNetworkSettings legacy;
  if (storageReadBytes(NETWORK_SETTINGS_ADDRESS, reinterpret_cast<uint8_t *>(&legacy), sizeof(legacy)) &&
      legacy.magic == NETWORK_SETTINGS_MAGIC && legacy.version == 1 &&
      legacy.size == sizeof(StoredLegacyNetworkSettings) && legacy.crc == legacyNetworkSettingsCrc(legacy)) {
    networkSettings = NetworkSettings();
    strlcpy(networkSettings.ssid, legacy.settings.ssid, sizeof(networkSettings.ssid));
    strlcpy(networkSettings.password, legacy.settings.password, sizeof(networkSettings.password));
    strlcpy(networkSettings.mdnsHostname, legacy.settings.mdnsHostname, sizeof(networkSettings.mdnsHostname));
    networkSettings.otaEnabled = false;
    sanitizeNetworkSettings(networkSettings);
    saveNetworkSettings();
    return true;
  }

  networkSettings = NetworkSettings();
  sanitizeNetworkSettings(networkSettings);
  return false;
}

bool saveNetworkSettings() {
  sanitizeNetworkSettings(networkSettings);

  StoredNetworkSettings stored;
  stored.size = sizeof(StoredNetworkSettings);
  stored.settings = networkSettings;
  stored.crc = networkSettingsCrc(stored);

  return storageEraseSector(NETWORK_SETTINGS_ADDRESS) &&
         storageWriteBytes(NETWORK_SETTINGS_ADDRESS, reinterpret_cast<const uint8_t *>(&stored), sizeof(stored));
}

bool loadSecuritySettings() {
  StoredSecuritySettings stored;
  if (!storageReadBytes(SECURITY_SETTINGS_ADDRESS, reinterpret_cast<uint8_t *>(&stored), sizeof(stored)) ||
      stored.magic != SECURITY_SETTINGS_MAGIC || stored.version != SECURITY_SETTINGS_VERSION ||
      stored.size != sizeof(StoredSecuritySettings) || stored.crc != securitySettingsCrc(stored)) {
    securitySettings = SecuritySettings();
    sanitizeSecuritySettings(securitySettings);
    return false;
  }

  securitySettings = stored.settings;
  sanitizeSecuritySettings(securitySettings);
  return true;
}

bool saveSecuritySettings() {
  sanitizeSecuritySettings(securitySettings);

  StoredSecuritySettings stored;
  stored.size = sizeof(StoredSecuritySettings);
  stored.settings = securitySettings;
  stored.crc = securitySettingsCrc(stored);

  return storageEraseSector(SECURITY_SETTINGS_ADDRESS) &&
         storageWriteBytes(SECURITY_SETTINGS_ADDRESS, reinterpret_cast<const uint8_t *>(&stored), sizeof(stored));
}

bool loadLogSettings() {
  StoredLogSettings stored;
  if (!storageReadBytes(LOG_SETTINGS_ADDRESS, reinterpret_cast<uint8_t *>(&stored), sizeof(stored)) ||
      stored.magic != LOG_SETTINGS_MAGIC || stored.version != LOG_SETTINGS_VERSION ||
      stored.size != sizeof(StoredLogSettings) || stored.crc != logSettingsCrc(stored)) {
    logSettings = LogSettings();
    sanitizeLogSettings(logSettings);
    return false;
  }

  logSettings = stored.settings;
  sanitizeLogSettings(logSettings);
  return true;
}

bool saveLogSettings() {
  sanitizeLogSettings(logSettings);

  StoredLogSettings stored;
  stored.size = sizeof(StoredLogSettings);
  stored.settings = logSettings;
  stored.crc = logSettingsCrc(stored);

  return storageEraseSector(LOG_SETTINGS_ADDRESS) &&
         storageWriteBytes(LOG_SETTINGS_ADDRESS, reinterpret_cast<const uint8_t *>(&stored), sizeof(stored));
}

bool loadEventLog(EventLogHeader &header, EventLogRecord *records) {
  if (!storageReadBytes(EVENT_LOG_ADDRESS, reinterpret_cast<uint8_t *>(&header), sizeof(header)) ||
      header.magic != EVENT_LOG_MAGIC || header.version != EVENT_LOG_VERSION ||
      header.size != sizeof(EventLogHeader) || header.count > EVENT_LOG_MAX_RECORDS) {
    header = EventLogHeader();
    header.size = sizeof(EventLogHeader);
    return false;
  }

  if (header.count > 0 &&
      !storageReadBytes(EVENT_LOG_ADDRESS + sizeof(EventLogHeader), reinterpret_cast<uint8_t *>(records),
                        sizeof(EventLogRecord) * header.count)) {
    header = EventLogHeader();
    header.size = sizeof(EventLogHeader);
    return false;
  }

  if (header.crc != eventLogCrc(header, records)) {
    header = EventLogHeader();
    header.size = sizeof(EventLogHeader);
    return false;
  }
  return true;
}

bool saveEventLog(EventLogHeader &header, EventLogRecord *records) {
  header.magic = EVENT_LOG_MAGIC;
  header.version = EVENT_LOG_VERSION;
  header.size = sizeof(EventLogHeader);
  header.reserved = 0;
  header.crc = eventLogCrc(header, records);

  for (uint32_t offset = 0; offset < EVENT_LOG_BYTES; offset += WEB_STORAGE_SECTOR_BYTES) {
    if (!storageEraseSector(EVENT_LOG_ADDRESS + offset)) {
      return false;
    }
  }
  return storageWriteBytes(EVENT_LOG_ADDRESS, reinterpret_cast<const uint8_t *>(&header), sizeof(header)) &&
         (header.count == 0 ||
          storageWriteBytes(EVENT_LOG_ADDRESS + sizeof(EventLogHeader), reinterpret_cast<const uint8_t *>(records),
                            sizeof(EventLogRecord) * header.count));
}

void appendEventLog(const char *category, const String &message) {
  EventLogHeader header;
  memset(eventLogScratch, 0, sizeof(eventLogScratch));
  loadEventLog(header, eventLogScratch);

  const uint32_t nowSeconds = millis() / 1000UL;
  uint16_t kept = 0;
  for (uint16_t i = 0; i < header.count; i++) {
    if (nowSeconds >= LOG_RETENTION_SECONDS && eventLogScratch[i].uptimeSeconds + LOG_RETENTION_SECONDS < nowSeconds) {
      continue;
    }
    if (kept != i) {
      eventLogScratch[kept] = eventLogScratch[i];
    }
    kept++;
  }
  header.count = kept;

  if (header.count >= EVENT_LOG_MAX_RECORDS) {
    memmove(eventLogScratch, eventLogScratch + 1, sizeof(EventLogRecord) * (EVENT_LOG_MAX_RECORDS - 1));
    header.count = EVENT_LOG_MAX_RECORDS - 1;
  }

  EventLogRecord &record = eventLogScratch[header.count++];
  memset(&record, 0, sizeof(record));
  record.uptimeSeconds = nowSeconds;
  strlcpy(record.category, category, sizeof(record.category));
  message.substring(0, sizeof(record.message) - 1).toCharArray(record.message, sizeof(record.message));
  saveEventLog(header, eventLogScratch);
}

void serviceEventLogging() {
  const ModuleSnapshot modules = modulesGetSnapshot();
  if (!eventLogDoorReedKnown) {
    eventLogDoorReedClosed = modules.doorReedClosed;
    eventLogDoorReedKnown = true;
  } else if (modules.doorReedClosed != eventLogDoorReedClosed) {
    eventLogDoorReedClosed = modules.doorReedClosed;
    if (logSettings.doorReed) {
      appendEventLog("door_reed", modules.doorReedClosed ? "Door closed" : "Door opened");
    }
  }

  if (!eventLogGarageReedKnown) {
    eventLogGarageReedClosed = modules.garageReedClosed;
    eventLogGarageReedKnown = true;
  } else if (modules.garageReedClosed != eventLogGarageReedClosed) {
    eventLogGarageReedClosed = modules.garageReedClosed;
    if (logSettings.garageReed) {
      appendEventLog("garage_reed", modules.garageReedClosed ? "Garage door closed" : "Garage door opened");
    }
  }

  const RelayAutomationSnapshot automation = relayAutomationGetSnapshot();
  if (logSettings.doorUnlock && automation.doorLockActive && !eventLogDoorUnlockActive) {
    appendEventLog("door_unlock", "Door unlock pulse");
  }
  if (logSettings.garageUnlock && automation.garageLockActive && !eventLogGarageUnlockActive) {
    appendEventLog("garage_unlock", "Garage door unlock pulse");
  }
  eventLogDoorUnlockActive = automation.doorLockActive;
  eventLogGarageUnlockActive = automation.garageLockActive;

  const RdmSnapshot rfid = rdmGetSnapshot();
  if (!eventLogRfidKnown) {
    eventLogRfidReads = rfid.totalReads;
    eventLogRfidKnown = true;
  } else if (rfid.totalReads != eventLogRfidReads) {
    eventLogRfidReads = rfid.totalReads;
    if (logSettings.rfid) {
      String message = rfid.lastTag;
      if (message.length() > 0 && rfid.lastEvent.length() > 0) {
        message += " - ";
      }
      message += rfid.lastEvent;
      appendEventLog("rfid", message);
    }
  }
}

void updateTdsDisabledSnapshot() {
  tdsSnapshot.enabled = false;
  tdsSnapshot.online = false;
  tdsSnapshot.address = String(tdsSettings.address);
  tdsSnapshot.lastEvent = "TDS monitor disabled";
  tdsSnapshot.nextPollMs = 0;
}

bool pollDue(uint32_t now, uint32_t nextPollMs) {
  return nextPollMs == 0 || static_cast<int32_t>(now - nextPollMs) >= 0;
}

void scheduleTdsPoll(uint32_t now, bool success) {
  tdsSnapshot.nextPollMs = now + (success ? TDS_POLL_INTERVAL_MS : API_FAILURE_BACKOFF_MS);
}

void scheduleAirPurifierPoll(uint32_t now, bool success) {
  airPurifierSnapshot.nextPollMs = now + (success ? AIR_PURIFIER_POLL_INTERVAL_MS : API_FAILURE_BACKOFF_MS);
}

void scheduleInverterPoll(InverterFlowSnapshot &snapshot, uint32_t now, uint32_t intervalMs, bool success) {
  snapshot.nextPollMs = now + (success ? intervalMs : API_FAILURE_BACKOFF_MS);
}

void applyTdsPayload(const String &payload) {
  float value = NAN;
  if (jsonNumberField(payload, "tds_ppm", value)) tdsSnapshot.tdsPpm = value;
  if (jsonNumberField(payload, "voltage", value)) tdsSnapshot.voltage = value;
  if (jsonNumberField(payload, "temp_c", value)) tdsSnapshot.tempC = value;
  if (jsonNumberField(payload, "temp_fallback_c", value)) tdsSnapshot.tempFallbackC = value;
  if (jsonNumberField(payload, "water_level_inches", value)) tdsSnapshot.waterLevelInches = value;
  if (jsonNumberField(payload, "water_level_percent", value)) tdsSnapshot.waterLevelPercent = value;
  if (jsonNumberField(payload, "water_volume_liters", value)) tdsSnapshot.waterVolumeLiters = value;
  if (jsonNumberField(payload, "tank_capacity_liters", value)) tdsSnapshot.tankCapacityLiters = value;
  if (jsonNumberField(payload, "age_ms", value)) tdsSnapshot.sampleAgeMs = static_cast<uint32_t>(value);
  jsonStringField(payload, "water_level_label", tdsSnapshot.waterLevelLabel);
}

void pollTdsMonitor() {
  tdsSnapshot.enabled = tdsSettings.enabled;
  tdsSnapshot.address = String(tdsSettings.address);

  if (!tdsSettings.enabled) {
    updateTdsDisabledSnapshot();
    return;
  }

  const uint32_t now = millis();
  if (!pollDue(now, tdsSnapshot.nextPollMs)) {
    return;
  }
  tdsSnapshot.lastPollMs = now;

  if (WiFi.status() != WL_CONNECTED) {
    tdsSnapshot.online = false;
    tdsSnapshot.lastEvent = "WiFi offline";
    scheduleTdsPoll(now, false);
    return;
  }

  HTTPClient http;
  http.setTimeout(TDS_HTTP_TIMEOUT_MS);
  if (!http.begin(tdsSnapshot.address)) {
    tdsSnapshot.online = false;
    tdsSnapshot.lastEvent = "Invalid TDS address";
    scheduleTdsPoll(now, false);
    return;
  }

  const int code = http.GET();
  if (code == HTTP_CODE_OK) {
    const String payload = http.getString();
    applyTdsPayload(payload);
    tdsSnapshot.online = true;
    tdsSnapshot.lastSuccessMs = now;
    tdsSnapshot.lastEvent = "TDS monitor online";
    scheduleTdsPoll(now, true);
  } else {
    tdsSnapshot.online = false;
    tdsSnapshot.lastEvent = "TDS HTTP " + String(code);
    scheduleTdsPoll(now, false);
  }
  http.end();
}

void updateAirPurifierDisabledSnapshot() {
  airPurifierSnapshot.enabled = false;
  airPurifierSnapshot.online = false;
  airPurifierSnapshot.address = String(airPurifierSettings.address);
  airPurifierSnapshot.lastEvent = "Air purifier disabled";
  airPurifierSnapshot.nextPollMs = 0;
}

bool jsonBoolField(const String &json, const char *field, bool &out) {
  const String key = "\"" + String(field) + "\":";
  int index = json.indexOf(key);
  if (index < 0) {
    return false;
  }
  index += key.length();
  while (index < static_cast<int>(json.length()) && isspace(json[index])) {
    index++;
  }
  if (json.substring(index, index + 4) == "true") {
    out = true;
    return true;
  }
  if (json.substring(index, index + 5) == "false") {
    out = false;
    return true;
  }
  return false;
}

void applyAirPurifierPayload(const String &payload) {
  float value = NAN;
  bool boolValue = false;
  if (jsonNumberField(payload, "pm1", value)) airPurifierSnapshot.pm1 = value;
  if (jsonNumberField(payload, "pm25", value)) airPurifierSnapshot.pm25 = value;
  if (jsonNumberField(payload, "pm10", value)) airPurifierSnapshot.pm10 = value;
  if (jsonNumberField(payload, "aq", value)) airPurifierSnapshot.aq = static_cast<uint8_t>(constrain(static_cast<int>(value), 0, 255));
  if (jsonNumberField(payload, "fan_speed_pct", value)) {
    airPurifierSnapshot.fanSpeedPct = static_cast<uint8_t>(constrain(static_cast<int>(value), 0, 100));
  }
  if (jsonNumberField(payload, "age_ms", value)) airPurifierSnapshot.sampleAgeMs = static_cast<uint32_t>(value);
  if (jsonBoolField(payload, "switch_on", boolValue)) airPurifierSnapshot.purifierOn = boolValue;
  if (jsonBoolField(payload, "fan_on", boolValue)) airPurifierSnapshot.fanOn = boolValue;
  if (jsonBoolField(payload, "auto_mode", boolValue)) airPurifierSnapshot.autoMode = boolValue;
  if (jsonBoolField(payload, "sensor_online", boolValue)) airPurifierSnapshot.sensorOnline = boolValue;
  if (jsonBoolField(payload, "pms_powered", boolValue)) airPurifierSnapshot.pmsPowered = boolValue;
}

void pollAirPurifier() {
  airPurifierSnapshot.enabled = airPurifierSettings.enabled;
  airPurifierSnapshot.address = String(airPurifierSettings.address);

  if (!airPurifierSettings.enabled) {
    updateAirPurifierDisabledSnapshot();
    return;
  }

  const uint32_t now = millis();
  if (!pollDue(now, airPurifierSnapshot.nextPollMs)) {
    return;
  }
  airPurifierSnapshot.lastPollMs = now;

  if (WiFi.status() != WL_CONNECTED) {
    airPurifierSnapshot.online = false;
    airPurifierSnapshot.lastEvent = "WiFi offline";
    scheduleAirPurifierPoll(now, false);
    return;
  }

  HTTPClient http;
  http.setTimeout(AIR_PURIFIER_HTTP_TIMEOUT_MS);
  if (!http.begin(airPurifierSnapshot.address)) {
    airPurifierSnapshot.online = false;
    airPurifierSnapshot.lastEvent = "Invalid air purifier address";
    scheduleAirPurifierPoll(now, false);
    return;
  }

  const int code = http.GET();
  if (code == HTTP_CODE_OK) {
    const String payload = http.getString();
    applyAirPurifierPayload(payload);
    airPurifierSnapshot.online = true;
    airPurifierSnapshot.lastSuccessMs = now;
    airPurifierSnapshot.lastEvent = "Air purifier online";
    scheduleAirPurifierPoll(now, true);
  } else {
    airPurifierSnapshot.online = false;
    airPurifierSnapshot.lastEvent = "Air purifier HTTP " + String(code);
    scheduleAirPurifierPoll(now, false);
  }
  http.end();
}

uint16_t modbusCrc16(const uint8_t *data, size_t length) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      crc = (crc & 0x0001) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
  }
  return crc;
}

uint8_t solarmanChecksum(const std::vector<uint8_t> &frame) {
  uint16_t checksum = 0;
  for (size_t i = 1; i + 2 < frame.size(); i++) {
    checksum += frame[i];
  }
  return checksum & 0xFF;
}

bool readNitroxHoldingRegisters(uint16_t start, uint16_t count, std::vector<uint16_t> &registers, String &error) {
  static uint8_t sequence = 0x40;
  WiFiClient client;
  client.setTimeout(NITROX_TCP_TIMEOUT_MS / 1000.0f);
  if (!client.connect(inverterSettings.nitroxHost, inverterSettings.nitroxPort, NITROX_TCP_TIMEOUT_MS)) {
    error = "Nitrox TCP connect failed";
    return false;
  }

  std::vector<uint8_t> modbus = {
    inverterSettings.nitroxSlaveId,
    0x03,
    static_cast<uint8_t>(start >> 8),
    static_cast<uint8_t>(start),
    static_cast<uint8_t>(count >> 8),
    static_cast<uint8_t>(count)
  };
  const uint16_t crc = modbusCrc16(modbus.data(), modbus.size());
  modbus.push_back(crc & 0xFF);
  modbus.push_back(crc >> 8);

  sequence++;
  const uint16_t payloadLength = 15 + modbus.size();
  std::vector<uint8_t> frame;
  frame.reserve(13 + payloadLength);
  frame.push_back(0xA5);
  frame.push_back(payloadLength & 0xFF);
  frame.push_back(payloadLength >> 8);
  frame.push_back(0x10);
  frame.push_back(0x45);
  frame.push_back(sequence);
  frame.push_back(0x00);
  frame.push_back(inverterSettings.nitroxLoggerSerial & 0xFF);
  frame.push_back((inverterSettings.nitroxLoggerSerial >> 8) & 0xFF);
  frame.push_back((inverterSettings.nitroxLoggerSerial >> 16) & 0xFF);
  frame.push_back((inverterSettings.nitroxLoggerSerial >> 24) & 0xFF);
  frame.push_back(0x02);
  frame.insert(frame.end(), 14, 0x00);
  frame.insert(frame.end(), modbus.begin(), modbus.end());
  frame.push_back(0x00);
  frame.push_back(0x15);
  frame[frame.size() - 2] = solarmanChecksum(frame);

  client.write(frame.data(), frame.size());

  std::vector<uint8_t> response;
  const uint32_t startedAt = millis();
  while (millis() - startedAt < NITROX_TCP_TIMEOUT_MS) {
    while (client.available()) {
      response.push_back(client.read());
    }
    if (response.size() >= 5) {
      for (size_t offset = 0; offset + 5 < response.size(); offset++) {
        if (response[offset] != 0xA5) continue;
        const uint16_t length = response[offset + 1] | (uint16_t(response[offset + 2]) << 8);
        const size_t total = 13 + length;
        if (response.size() - offset < total) continue;
        if (response[offset + total - 1] != 0x15 || response[offset + 5] != sequence ||
            response[offset + 3] != 0x10 || response[offset + 4] != 0x15) {
          continue;
        }
        std::vector<uint8_t> packet(response.begin() + offset, response.begin() + offset + total);
        if (packet[packet.size() - 2] != solarmanChecksum(packet)) {
          error = "Nitrox checksum failed";
          return false;
        }
        if (packet.size() < 31) {
          error = "Nitrox short response";
          return false;
        }
        const size_t mbStart = 25;
        const size_t mbEnd = packet.size() - 2;
        if (packet[mbStart] != inverterSettings.nitroxSlaveId || packet[mbStart + 1] != 0x03) {
          error = "Nitrox Modbus error";
          return false;
        }
        const uint8_t bytes = packet[mbStart + 2];
        if (mbStart + 3 + bytes + 2 > mbEnd || bytes != count * 2) {
          error = "Nitrox Modbus length mismatch";
          return false;
        }
        const uint16_t responseCrc = packet[mbStart + 3 + bytes] | (uint16_t(packet[mbStart + 4 + bytes]) << 8);
        if (responseCrc != modbusCrc16(&packet[mbStart], 3 + bytes)) {
          error = "Nitrox Modbus CRC failed";
          return false;
        }
        registers.clear();
        for (uint8_t i = 0; i < bytes; i += 2) {
          registers.push_back((uint16_t(packet[mbStart + 3 + i]) << 8) | packet[mbStart + 4 + i]);
        }
        return true;
      }
    }
    yield();
  }

  error = "Nitrox read timeout";
  return false;
}

void pollSolaxInverter() {
  if (!inverterSettings.solaxEnabled) {
    solaxSnapshot.online = false;
    solaxSnapshot.lastEvent = "SolaX disabled";
    solaxSnapshot.nextPollMs = 0;
    return;
  }

  const uint32_t now = millis();
  if (!pollDue(now, solaxSnapshot.nextPollMs)) return;
  solaxSnapshot.lastPollMs = now;

  if (WiFi.status() != WL_CONNECTED) {
    solaxSnapshot.online = false;
    solaxSnapshot.lastEvent = "WiFi offline";
    scheduleInverterPoll(solaxSnapshot, now, inverterSettings.solaxIntervalMs, false);
    return;
  }

  HTTPClient http;
  http.setTimeout(SOLAX_HTTP_TIMEOUT_MS);
  if (!http.begin(inverterSettings.solaxAddress)) {
    solaxSnapshot.online = false;
    solaxSnapshot.lastEvent = "SolaX invalid URL";
    scheduleInverterPoll(solaxSnapshot, now, inverterSettings.solaxIntervalMs, false);
    return;
  }
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  const String body = String("optType=ReadRealTimeData&pwd=") + inverterSettings.solaxPassword;
  const int code = http.POST(body);
  if (code == HTTP_CODE_OK) {
    const String payload = http.getString();
    float d0 = NAN;
    float d1 = NAN;
    float d2 = NAN;
    float d9 = NAN;
    float d10 = NAN;
    float d18 = NAN;
    float d22 = NAN;
    float d49 = NAN;
    float d76 = NAN;
    jsonArrayNumberAt(payload, "Data", 0, d0);
    jsonArrayNumberAt(payload, "Data", 1, d1);
    jsonArrayNumberAt(payload, "Data", 2, d2);
    jsonArrayNumberAt(payload, "Data", 9, d9);
    jsonArrayNumberAt(payload, "Data", 10, d10);
    jsonArrayNumberAt(payload, "Data", 18, d18);
    jsonArrayNumberAt(payload, "Data", 22, d22);
    jsonArrayNumberAt(payload, "Data", 49, d49);
    jsonArrayNumberAt(payload, "Data", 76, d76);
    solaxSnapshot.gridVoltageV = isfinite(d0) ? d0 / 10.0f : NAN;
    solaxSnapshot.pvPowerW = (isfinite(d9) ? d9 : 0) + (isfinite(d10) ? d10 : 0);
    solaxSnapshot.gridPowerW = d49;
    solaxSnapshot.homePowerW = d49;
    solaxSnapshot.batteryPowerW = isfinite(d22) ? -signed16(static_cast<uint16_t>(d22)) : NAN;
    solaxSnapshot.batterySoc = d76;
    solaxSnapshot.todayYieldKwh = isfinite(d18) ? d18 / 10.0f : NAN;
    solaxSnapshot.online = true;
    solaxSnapshot.lastSuccessMs = now;
    solaxSnapshot.lastEvent = "SolaX online";
    scheduleInverterPoll(solaxSnapshot, now, inverterSettings.solaxIntervalMs, true);
  } else {
    solaxSnapshot.online = false;
    solaxSnapshot.lastEvent = "SolaX HTTP " + String(code);
    scheduleInverterPoll(solaxSnapshot, now, inverterSettings.solaxIntervalMs, false);
  }
  http.end();
}

void pollNitroxInverter() {
  if (!inverterSettings.nitroxEnabled) {
    nitroxSnapshot.online = false;
    nitroxSnapshot.lastEvent = "Nitrox disabled";
    nitroxSnapshot.nextPollMs = 0;
    return;
  }

  const uint32_t now = millis();
  if (!pollDue(now, nitroxSnapshot.nextPollMs)) return;
  nitroxSnapshot.lastPollMs = now;

  if (WiFi.status() != WL_CONNECTED) {
    nitroxSnapshot.online = false;
    nitroxSnapshot.lastEvent = "WiFi offline";
    scheduleInverterPoll(nitroxSnapshot, now, inverterSettings.nitroxIntervalMs, false);
    return;
  }

  std::vector<uint16_t> regs;
  String error;
  if (!readNitroxHoldingRegisters(150, 41, regs, error)) {
    nitroxSnapshot.online = false;
    nitroxSnapshot.lastEvent = error;
    scheduleInverterPoll(nitroxSnapshot, now, inverterSettings.nitroxIntervalMs, false);
    return;
  }

  auto reg = [&](uint16_t address) -> uint16_t {
    const int index = int(address) - 150;
    return index >= 0 && index < int(regs.size()) ? regs[index] : 0;
  };

  nitroxSnapshot.gridVoltageV = reg(150) / 10.0f;
  nitroxSnapshot.gridPowerW = signed16(reg(169));
  nitroxSnapshot.homePowerW = reg(178);
  nitroxSnapshot.batterySoc = reg(184);
  nitroxSnapshot.pvPowerW = reg(186) + reg(187);
  nitroxSnapshot.batteryPowerW = signed16(reg(190));
  nitroxSnapshot.todayYieldKwh = NAN;
  nitroxSnapshot.online = true;
  nitroxSnapshot.lastSuccessMs = now;
  nitroxSnapshot.lastEvent = "Nitrox online";
  scheduleInverterPoll(nitroxSnapshot, now, inverterSettings.nitroxIntervalMs, true);
}

bool growattGet(const String &pathAndQuery, String &payload, String &error) {
  WiFiClientSecure secureClient;
  secureClient.setInsecure();

  HTTPClient http;
  http.setTimeout(GROWATT_HTTP_TIMEOUT_MS);
  const String url = String(inverterSettings.growattBaseUrl) + pathAndQuery;
  if (!http.begin(secureClient, url)) {
    error = "Growatt invalid URL";
    return false;
  }
  http.addHeader("token", inverterSettings.growattToken);
  http.addHeader("Accept", "application/json");
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    error = "Growatt HTTP " + String(code);
    http.end();
    return false;
  }
  payload = http.getString();
  http.end();

  float errorCode = NAN;
  if (jsonNumberField(payload, "error_code", errorCode) && static_cast<int>(errorCode) != 0) {
    String errorMessage;
    jsonStringField(payload, "error_msg", errorMessage);
    error = errorMessage.length() > 0 ? "Growatt " + errorMessage : "Growatt error " + String(static_cast<int>(errorCode));
    return false;
  }
  return true;
}

String extractGrowattDataField(const String &payload, const String &fieldName) {
  int dataIndex = payload.indexOf("\"data\":");
  if (dataIndex == -1) return "";
  
  int openBrace = payload.indexOf('{', dataIndex);
  if (openBrace == -1) return "";
  
  int closeBrace = payload.lastIndexOf('}');
  if (closeBrace <= openBrace) return "";
  
  String dataObj = payload.substring(openBrace, closeBrace + 1);
  
  String searchPattern = "\"" + fieldName + "\":";
  int fieldIndex = dataObj.indexOf(searchPattern);
  if (fieldIndex == -1) return "";
  
  int valueStart = fieldIndex + searchPattern.length();
  while (valueStart < dataObj.length() && (dataObj[valueStart] == ' ' || dataObj[valueStart] == '\n')) valueStart++;
  
  if (valueStart >= dataObj.length()) return "";
  
  String result = "";
  if (dataObj[valueStart] == '"') {
    valueStart++;
    int valueEnd = dataObj.indexOf('"', valueStart);
    if (valueEnd != -1) result = dataObj.substring(valueStart, valueEnd);
  } else if (dataObj[valueStart] == '[') {
    int bracketEnd = dataObj.indexOf(']', valueStart);
    if (bracketEnd != -1) result = dataObj.substring(valueStart, bracketEnd + 1);
  } else {
    int valueEnd = valueStart;
    while (valueEnd < dataObj.length() && dataObj[valueEnd] != ',' && dataObj[valueEnd] != '}') valueEnd++;
    result = dataObj.substring(valueStart, valueEnd);
    result.trim();
  }
  
  return result;
}

float parseStringFloat(const String &str) {
  if (str.length() == 0) return NAN;
  char *endPtr;
  float value = strtof(str.c_str(), &endPtr);
  return (endPtr != str.c_str()) ? value : NAN;
}

uint32_t growattResolvedPlantId() {
  if (inverterSettings.growattPlantId != 0) {
    return inverterSettings.growattPlantId;
  }

  String payload;
  String error;
  if (!growattGet("plant/list?page=&perpage=&search_type=&search_keyword=", payload, error)) {
    growattSnapshot.lastEvent = error;
    return 0;
  }

  String plantIdStr = extractGrowattDataField(payload, "plant_id");
  if (plantIdStr.length() == 0) plantIdStr = extractGrowattDataField(payload, "id");
  
  float plantId = parseStringFloat(plantIdStr);
  if (isnan(plantId) || plantId <= 0) {
    growattSnapshot.lastEvent = "Growatt plant not found";
    return 0;
  }
  return static_cast<uint32_t>(plantId);
}

void pollGrowattInverter() {
  if (!inverterSettings.growattEnabled) {
    growattSnapshot.online = false;
    growattSnapshot.lastEvent = "Growatt disabled";
    growattSnapshot.nextPollMs = 0;
    return;
  }

  const uint32_t now = millis();
  if (!pollDue(now, growattSnapshot.nextPollMs)) return;
  growattSnapshot.lastPollMs = now;

  if (WiFi.status() != WL_CONNECTED) {
    growattSnapshot.online = false;
    growattSnapshot.lastEvent = "WiFi offline";
    scheduleInverterPoll(growattSnapshot, now, inverterSettings.growattIntervalMs, false);
    return;
  }
  if (String(inverterSettings.growattToken).length() == 0) {
    growattSnapshot.online = false;
    growattSnapshot.lastEvent = "Growatt token missing";
    scheduleInverterPoll(growattSnapshot, now, inverterSettings.growattIntervalMs, false);
    return;
  }

  const uint32_t plantId = growattResolvedPlantId();
  if (plantId == 0) {
    growattSnapshot.online = false;
    scheduleInverterPoll(growattSnapshot, now, inverterSettings.growattIntervalMs, false);
    return;
  }

  String payload;
  String error;
  if (!growattGet("plant/data?plant_id=" + String(plantId), payload, error)) {
    growattSnapshot.online = false;
    growattSnapshot.lastEvent = error;
    scheduleInverterPoll(growattSnapshot, now, inverterSettings.growattIntervalMs, false);
    return;
  }

  float value = NAN;
  String strValue = extractGrowattDataField(payload, "current_power");
  if (strValue.length() == 0) strValue = extractGrowattDataField(payload, "power");
  if (strValue.length() > 0) {
    value = parseStringFloat(strValue);
    if (!isnan(value)) {
      const float powerW = fabsf(value) < 1000.0f ? value * 1000.0f : value;
      growattSnapshot.pvPowerW = powerW;
      growattSnapshot.gridPowerW = powerW;
    }
  }
  
  strValue = extractGrowattDataField(payload, "today_energy");
  if (strValue.length() == 0) strValue = extractGrowattDataField(payload, "eToday");
  if (strValue.length() == 0) strValue = extractGrowattDataField(payload, "energyToday");
  if (strValue.length() == 0) strValue = extractGrowattDataField(payload, "energy_today");
  if (strValue.length() > 0) {
    value = parseStringFloat(strValue);
    if (!isnan(value)) growattSnapshot.todayYieldKwh = value;
  }
  
  strValue = extractGrowattDataField(payload, "total_energy");
  if (strValue.length() == 0) strValue = extractGrowattDataField(payload, "total_power_generation");
  if (strValue.length() == 0) strValue = extractGrowattDataField(payload, "eTotal");
  if (strValue.length() == 0) strValue = extractGrowattDataField(payload, "totalEnergy");
  if (strValue.length() == 0) strValue = extractGrowattDataField(payload, "totalPowerGeneration");
  if (strValue.length() > 0) {
    value = parseStringFloat(strValue);
    if (!isnan(value)) growattSnapshot.totalEnergyKwh = value;
  }
  growattSnapshot.gridVoltageV = NAN;
  growattSnapshot.homePowerW = NAN;
  growattSnapshot.batteryPowerW = NAN;
  growattSnapshot.batterySoc = NAN;
  growattSnapshot.online = true;
  growattSnapshot.lastSuccessMs = now;
  growattSnapshot.lastEvent = "Growatt online";
  scheduleInverterPoll(growattSnapshot, now, inverterSettings.growattIntervalMs, true);
}

void pollInverters() {
  static uint8_t nextPoller = 0;
  switch (nextPoller) {
    case 0:
      pollSolaxInverter();
      break;
    case 1:
      pollNitroxInverter();
      break;
    default:
      pollGrowattInverter();
      break;
  }
  nextPoller = (nextPoller + 1) % 3;
}

void pollExternalMonitor() {
  const uint32_t now = millis();
  if (relayCommandPriorityUntilMs != 0 && static_cast<int32_t>(now - relayCommandPriorityUntilMs) < 0) {
    return;
  }
  static uint8_t nextPoller = 0;
  switch (nextPoller) {
    case 0:
      pollTdsMonitor();
      break;
    case 1:
      pollAirPurifier();
      break;
    case 2:
      pollSolaxInverter();
      break;
    case 3:
      pollNitroxInverter();
      break;
    default:
      pollGrowattInverter();
      break;
  }
  nextPoller = (nextPoller + 1) % 5;
}

void noteRelayCommandPriority() {
  relayCommandPriorityUntilMs = millis() + RELAY_COMMAND_PRIORITY_WINDOW_MS;
}

bool mq135SmokeAlarmActive(const ModuleSnapshot &modules) {
  return modules.mq135AnalogRaw >= mp3SoundSettings.smokeAlarmThresholdRaw;
}

void serviceMp3SmokeAlarm() {
  const ModuleSnapshot modules = modulesGetSnapshot();
  const bool active = mq135SmokeAlarmActive(modules);

  if (!mp3SoundSettings.smokeAlarmEnabled) {
    smokeAlarmWasActive = active;
    return;
  }

  if (active && !smokeAlarmWasActive) {
    mp3SetVolume(mp3SoundSettings.volume);
    mp3PlayFile(MP3_SOUND_FOLDER, mp3SoundSettings.smokeAlarmTrack);
  }
  smokeAlarmWasActive = active;
}

uint16_t radarPresenceDistanceCm(const Radar::TargetData &radar) {
  if (radar.detectionDistanceCm > 0) {
    return radar.detectionDistanceCm;
  }
  if (Radar::hasMovingTarget() && radar.movingDistanceCm > 0) {
    return radar.movingDistanceCm;
  }
  if (Radar::hasStationaryTarget() && radar.stationaryDistanceCm > 0) {
    return radar.stationaryDistanceCm;
  }
  return radar.movingDistanceCm > 0 ? radar.movingDistanceCm : radar.stationaryDistanceCm;
}

uint8_t radarPresenceEnergy(const Radar::TargetData &radar) {
  return radar.movingEnergy > radar.stationaryEnergy ? radar.movingEnergy : radar.stationaryEnergy;
}

bool isRadarPresenceValid() {
  const Radar::TargetData &radar = Radar::getTargetData();
  if (!Radar::isPresent() || Radar::getLastUpdateAgeMs() == UINT32_MAX) {
    return false;
  }

  const uint16_t distanceCm = radarPresenceDistanceCm(radar);
  const bool distanceValid = fm225RadarSettings.minDistanceCm == 0 ||
                             (distanceCm > 0 && distanceCm <= fm225RadarSettings.minDistanceCm);
  return distanceValid && radarPresenceEnergy(radar) >= fm225RadarSettings.minEnergy;
}

uint32_t fm225RadarCooldownRemainingMs(uint32_t now) {
  if (fm225RadarLastUnlockMs == 0 || now - fm225RadarLastUnlockMs >= FM225_RADAR_UNLOCK_COOLDOWN_MS) {
    return 0;
  }
  return FM225_RADAR_UNLOCK_COOLDOWN_MS - (now - fm225RadarLastUnlockMs);
}

bool handleFaceVerifiedUnlock() {
  const uint32_t now = millis();
  if (fm225RadarCooldownRemainingMs(now) > 0) {
    setFm225RadarStatus("Face verified - cooldown active");
    return false;
  }

  const bool pulsed = relayAutomationPulseDoorLock();
  if (pulsed) {
    fm225RadarLastUnlockMs = now;
    setFm225RadarStatus("Face verified - door unlocked");
  }
  return pulsed;
}

void activateFm225FromRadar() {
  fm225RadarVerifyActive = true;
  fm225RadarVerifyStartedMs = millis();
  fm225RadarLastAttemptMs = fm225RadarVerifyStartedMs;
  fm225VerifyPending = true;
  fm225VerifySummary = "Radar presence: verifying face...";
  fm225LastRecognizedUserId = 0;
  fm225LastRecognizedName = "";
  fm225LastVerifyResult = 0xFF;
  fm225LastStatus = "Verifying";
  setFm225RadarStatus("FM225 face detection active");
  FM225::verify(FM225_RADAR_VERIFY_TIMEOUT_SEC, false);
}

void serviceFm225RadarPresence() {
  if (!fm225RadarSettings.enabled) {
    fm225RadarVerifyActive = false;
    return;
  }

  const uint32_t now = millis();
  if (fm225RadarVerifyActive) {
    const uint32_t activeLimitMs = (uint32_t(FM225_RADAR_VERIFY_TIMEOUT_SEC) + 2UL) * 1000UL;
    if (now - fm225RadarVerifyStartedMs > activeLimitMs) {
      fm225RadarVerifyActive = false;
      fm225VerifyPending = false;
      fm225VerifySummary = "Rejected: no face recognized";
      setFm225RadarStatus("Face not recognized");
    }
    return;
  }

  if (fm225RadarCooldownRemainingMs(now) > 0) {
    setFm225RadarStatus("Face verified - cooldown active");
    return;
  }

  if (!Radar::isPresent()) {
    setFm225RadarStatus("Radar presence waiting");
    return;
  }

  if (!isRadarPresenceValid()) {
    setFm225RadarStatus("Radar condition not met");
    return;
  }

  if (fm225RadarLastAttemptMs != 0 && now - fm225RadarLastAttemptMs < FM225_RADAR_ATTEMPT_COOLDOWN_MS) {
    setFm225RadarStatus("Presence detected");
    return;
  }

  setFm225RadarStatus("Presence detected");
  activateFm225FromRadar();
}

void handleFm225Log(const String &message) {
  setFm225Event(message);
  Serial.println(message);
}

void handleFm225Recognized(uint16_t userId, const String &name) {
  fm225LastRecognizedUserId = userId;
  fm225LastRecognizedName = name;
  fm225LastVerifyResult = 0;
  fm225VerifyPending = false;
  fm225VerifySummary = "Verified: ID " + String(userId) + (name.length() ? " - " + name : "");
  fm225LastStatus = "Recognized";
  setFm225Event("Recognized ID " + String(userId) + " " + name);
  if (fm225RadarVerifyActive) {
    fm225RadarVerifyActive = false;
  }
  handleFaceVerifiedUnlock();
}

void handleFm225VerifyFailed(uint8_t result) {
  fm225LastVerifyResult = result;
  fm225VerifyPending = false;
  fm225VerifySummary = "Rejected: " + FM225::resultText(result);
  fm225LastStatus = "Verify failed";
  if (fm225RadarSettings.enabled && fm225RadarVerifyActive) {
    fm225RadarVerifyActive = false;
    setFm225RadarStatus("Face not recognized");
    return;
  }
  setFm225Event("Verify failed: " + FM225::resultText(result));
}

void handleFm225EnrollResult(uint8_t result) {
  fm225LastEnrollResult = result;
  fm225LastStatus = result == 0 ? "Enroll complete" : "Enroll failed";
  setFm225Event("Enroll result: " + FM225::resultText(result));
}

void handleFm225Status(const String &status) {
  fm225LastStatus = status;
  setFm225Event("Status: " + status);
}

void handleFm225UserInfo(uint16_t userId, const String &name) {
  setFm225Event("User " + String(userId) + ": " + name);
}

void handleFm225UserList(const std::vector<uint16_t> &userIds) {
  setFm225Event("Users listed: " + String(userIds.size()));
}

void handleFm225FaceState(const FM225::FaceState &state) {
  fm225LastStatus = FM225::faceStateText(state.state);
  setFm225Event("Face state: " + fm225LastStatus);
}

void handleFm225UsbUvc(const FM225::UsbUvcParameters &parameters) {
  setFm225Event("USB UVC updated, quality " + String(parameters.jpegQuality));
}

void handleFm225Image(const FM225::ImageInfo &info) {
  setFm225Event("Image packet " + String(info.packetCount));
}

String relayJson(uint16_t relayState) {
  String json = "[";
  for (uint8_t channel = 0; channel < RELAY_CHANNEL_COUNT; channel++) {
    if (channel > 0) {
      json += ",";
    }
    json += (relayState & (1U << channel)) ? "true" : "false";
  }
  json += "]";
  return json;
}

uint8_t countActiveRelays(uint16_t relayState) {
  uint8_t count = 0;
  for (uint8_t channel = 0; channel < RELAY_CHANNEL_COUNT; channel++) {
    if ((relayState & (1U << channel)) != 0) {
      count++;
    }
  }
  return count;
}

bool readWebStorageHeader(WebStorageHeader &header) {
  if (!storageReadBytes(WEB_STORAGE_ADDRESS, reinterpret_cast<uint8_t *>(&header), sizeof(header))) {
    return false;
  }

  if (header.magic != WEB_STORAGE_MAGIC || header.version != WEB_STORAGE_VERSION ||
      header.size != sizeof(WebStorageHeader) || header.fileCount == 0 ||
      header.fileCount > WEB_STORAGE_FILE_COUNT || header.crc != webStorageHeaderCrc(header)) {
    return false;
  }

  for (uint8_t i = 0; i < header.fileCount; i++) {
    const WebStorageEntry &entry = header.files[i];
    if (entry.offset < WEB_STORAGE_HEADER_BYTES || entry.length == 0 ||
        entry.offset + entry.length > WEB_STORAGE_BYTES || entry.path[0] != '/') {
      return false;
    }
  }

  return true;
}

const WebStorageEntry *findWebStorageEntry(const WebStorageHeader &header, const String &path) {
  for (uint8_t i = 0; i < header.fileCount; i++) {
    if (path == String(header.files[i].path)) {
      return &header.files[i];
    }
  }
  return nullptr;
}

bool readWebStorageFile(const WebStorageEntry &entry, String &content) {
  content = "";
  content.reserve(entry.length + 1);

  uint8_t buffer[256];
  uint32_t hash = 2166136261UL;
  uint32_t remaining = entry.length;
  uint32_t address = WEB_STORAGE_ADDRESS + entry.offset;

  while (remaining > 0) {
    const size_t chunk = min(static_cast<uint32_t>(sizeof(buffer)), remaining);
    if (!storageReadBytes(address, buffer, chunk)) {
      return false;
    }

    hash = fnv1aUpdate(hash, buffer, chunk);
    for (size_t i = 0; i < chunk; i++) {
      content += static_cast<char>(buffer[i]);
    }

    address += chunk;
    remaining -= chunk;
  }

  return hash == entry.crc;
}

bool serveWebStorageFile(const String &path) {
  WebStorageHeader header;
  if (!readWebStorageHeader(header)) {
    return false;
  }

  const WebStorageEntry *entry = findWebStorageEntry(header, path);
  if (entry == nullptr) {
    return false;
  }

  uint8_t buffer[512];
  uint32_t hash = 2166136261UL;
  uint32_t remaining = entry->length;
  uint32_t address = WEB_STORAGE_ADDRESS + entry->offset;

  if (path == "/index.html") {
    server.sendHeader("Cache-Control", "no-cache");
  } else if (path == "/techpanda.png") {
    server.sendHeader("Cache-Control", "public, max-age=86400");
  } else {
    server.sendHeader("Cache-Control", "public, max-age=300");
  }
  server.setContentLength(entry->length);
  server.send(200, contentTypeFor(path), "");

  WiFiClient client = server.client();
  while (remaining > 0 && client.connected()) {
    const size_t chunk = min(static_cast<uint32_t>(sizeof(buffer)), remaining);
    if (!storageReadBytes(address, buffer, chunk)) {
      webStorageLastEvent = "Winbond web file read failed";
      return true;
    }

    hash = fnv1aUpdate(hash, buffer, chunk);
    client.write(buffer, chunk);
    address += chunk;
    remaining -= chunk;
  }

  if (remaining != 0 || hash != entry->crc) {
    webStorageLastEvent = "Winbond web file CRC failed";
  }
  return true;
}

uint8_t webStorageFileCount() {
  WebStorageHeader header;
  return readWebStorageHeader(header) ? header.fileCount : 0;
}

bool webStorageReady() {
  return webStorageFileCount() > 0;
}

uint32_t crcGeneratedAsset(const GeneratedWebAsset &asset) {
  uint32_t hash = 2166136261UL;
  for (uint32_t offset = 0; offset < asset.length; offset++) {
    const uint8_t value = pgm_read_byte(asset.data + offset);
    hash = fnv1aUpdate(hash, &value, 1);
  }
  return hash;
}

bool writeGeneratedAssetToStorage(const GeneratedWebAsset &asset, uint32_t offset) {
  uint8_t buffer[256];
  uint32_t address = WEB_STORAGE_ADDRESS + offset;
  uint32_t remaining = asset.length;
  uint32_t sourceOffset = 0;

  while (remaining > 0) {
    const size_t chunk = min(static_cast<uint32_t>(sizeof(buffer)), remaining);
    for (size_t i = 0; i < chunk; i++) {
      buffer[i] = pgm_read_byte(asset.data + sourceOffset + i);
    }
    if (!storageWriteBytes(address, buffer, chunk)) {
      return false;
    }
    address += chunk;
    sourceOffset += chunk;
    remaining -= chunk;
  }

  return true;
}

bool seedWebStorageFromGeneratedAssets() {
  WebStorageHeader header;
  header.size = sizeof(WebStorageHeader);
  header.fileCount = GENERATED_WEB_ASSET_COUNT;

  uint32_t nextOffset = WEB_STORAGE_HEADER_BYTES;
  for (uint8_t i = 0; i < GENERATED_WEB_ASSET_COUNT; i++) {
    const GeneratedWebAsset &asset = GENERATED_WEB_ASSETS[i];
    if (asset.length == 0 || nextOffset + asset.length > WEB_STORAGE_BYTES) {
      webStorageLastSeedOk = false;
      webStorageLastEvent = String("Invalid generated web asset ") + asset.path;
      return false;
    }

    strlcpy(header.files[i].path, asset.path, sizeof(header.files[i].path));
    header.files[i].offset = nextOffset;
    header.files[i].length = asset.length;
    header.files[i].crc = crcGeneratedAsset(asset);
    nextOffset += asset.length;
  }

  const uint32_t eraseBytes = ((nextOffset + WEB_STORAGE_SECTOR_BYTES - 1) / WEB_STORAGE_SECTOR_BYTES) * WEB_STORAGE_SECTOR_BYTES;
  for (uint32_t offset = 0; offset < eraseBytes; offset += WEB_STORAGE_SECTOR_BYTES) {
    if (!storageEraseSector(WEB_STORAGE_ADDRESS + offset)) {
      webStorageLastSeedOk = false;
      webStorageLastEvent = "Winbond web erase failed";
      return false;
    }
  }

  for (uint8_t i = 0; i < GENERATED_WEB_ASSET_COUNT; i++) {
    if (!writeGeneratedAssetToStorage(GENERATED_WEB_ASSETS[i], header.files[i].offset)) {
      webStorageLastSeedOk = false;
      webStorageLastEvent = String("Winbond web write failed ") + header.files[i].path;
      return false;
    }
  }

  header.crc = webStorageHeaderCrc(header);
  webStorageLastSeedOk = storageWriteBytes(WEB_STORAGE_ADDRESS, reinterpret_cast<const uint8_t *>(&header), sizeof(header));
  webStorageLastEvent = webStorageLastSeedOk ? "Winbond web files seeded from firmware assets" : "Winbond web header write failed";
  return webStorageLastSeedOk;
}

bool webStorageMatchesGeneratedAssets() {
  WebStorageHeader header;
  if (!readWebStorageHeader(header) || header.fileCount != GENERATED_WEB_ASSET_COUNT) {
    return false;
  }

  for (uint8_t i = 0; i < GENERATED_WEB_ASSET_COUNT; i++) {
    const GeneratedWebAsset &asset = GENERATED_WEB_ASSETS[i];
    const WebStorageEntry *entry = findWebStorageEntry(header, asset.path);
    if (entry == nullptr || entry->length != asset.length || entry->crc != crcGeneratedAsset(asset)) {
      return false;
    }
  }
  return true;
}

void ensureWebStorageSeeded() {
  if (webStorageMatchesGeneratedAssets()) {
    webStorageLastSeedOk = true;
    webStorageLastEvent = "Winbond web files match firmware assets";
    return;
  }
  seedWebStorageFromGeneratedAssets();
}

bool isAuthenticated() {
  if (!securitySettings.loginEnabled) {
    return true;
  }
  if (!server.hasHeader("Cookie")) {
    return false;
  }
  const String cookie = server.header("Cookie");
  return cookie.indexOf("ha_auth=" + authToken()) >= 0;
}

void sendLoginPage(const String &message = "") {
  String html = "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
  html += "<title>Login - Smart Automation</title><style>";
  html += "body{margin:0;min-height:100vh;display:grid;place-items:center;background:#08111f;color:#eaf6ff;font-family:system-ui,Segoe UI,sans-serif}";
  html += "form{width:min(100% - 32px,380px);display:grid;gap:14px;padding:24px;background:linear-gradient(145deg,#4a2d9c,#031f4c 58%,#001440);border:1px solid rgba(125,211,252,.28);border-radius:10px;box-shadow:0 24px 80px rgba(0,0,0,.35)}";
  html += "h1{margin:0;font-size:1.4rem}p{margin:0;color:#b9d7ff}label{display:grid;gap:7px;color:#c7ddf7;font-size:.9rem}input{height:44px;padding:0 12px;color:#eaf6ff;background:#07111f;border:1px solid rgba(125,211,252,.22);border-radius:6px}";
  html += "button{height:44px;color:#03111f;font-weight:800;background:linear-gradient(135deg,#38bdf8,#7dd3fc);border:0;border-radius:6px}.error{color:#fecdd3}</style></head><body>";
  html += "<form method=\"post\" action=\"/api/login\"><h1>Smart Automation</h1><p>Login required</p>";
  if (message.length() > 0) {
    html += "<p class=\"error\">" + jsonEscape(message) + "</p>";
  }
  html += "<label>Username<input name=\"username\" type=\"text\" autocomplete=\"username\"></label>";
  html += "<label>Password<input name=\"password\" type=\"password\" autocomplete=\"current-password\"></label>";
  html += "<button type=\"submit\">Login</button></form></body></html>";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", html);
}

void sendWebStorageSeedPage() {
  String html = "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
  html += "<title>Seed Web Storage</title><style>";
  html += "body{margin:0;min-height:100vh;display:grid;place-items:center;background:#08111f;color:#eaf6ff;font-family:system-ui,Segoe UI,sans-serif}";
  html += "main{width:min(100% - 32px,430px);display:grid;gap:14px;padding:24px;background:#101722;border:1px solid rgba(125,211,252,.28);border-radius:10px;box-shadow:0 24px 80px rgba(0,0,0,.35)}";
  html += "h1{margin:0;font-size:1.35rem}p{margin:0;color:#b9d7ff;line-height:1.45}button{height:44px;color:#03111f;font-weight:800;background:#7dd3fc;border:0;border-radius:6px}</style></head>";
  html += "<body><main><h1>Winbond Web Storage</h1><p>The dashboard files are packed in firmware but have not been copied into the external Winbond flash yet.</p>";
  html += "<form method=\"post\" action=\"/api/web-storage/seed\"><button type=\"submit\">Seed Web Storage</button></form></main></body></html>";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "text/html", html);
}

bool requireAuth() {
  if (isAuthenticated()) {
    return true;
  }
  if (server.uri().startsWith("/api/")) {
    server.sendHeader("Cache-Control", "no-store");
    server.send(401, "application/json", "{\"error\":\"login_required\"}");
  } else {
    sendLoginPage();
  }
  return false;
}

void handleLoginApi() {
  const String username = server.arg("username");
  const String password = server.arg("password");
  if (username == String(securitySettings.username) && password == String(securitySettings.password)) {
    server.sendHeader("Set-Cookie", "ha_auth=" + authToken() + "; Max-Age=" + String(AUTH_COOKIE_MAX_AGE_SECONDS) + "; Path=/; SameSite=Lax");
    server.sendHeader("Location", "/");
    server.send(303, "text/plain", "Logged in");
    return;
  }
  sendLoginPage("Invalid username or password");
}

void handleRoot() {
  if (!requireAuth()) {
    return;
  }
  if (!serveWebStorageFile("/index.html")) {
    sendWebStorageSeedPage();
  }
}

void handleStaticFile() {
  if (!requireAuth()) {
    return;
  }
  String path = server.uri();
  if (path == "/") {
    path = "/index.html";
  }

  if (serveWebStorageFile(path)) {
    return;
  }

  server.send(404, "application/json", "{\"error\":\"not_found\"}");
}

void handleStatusApi() {
  if (!requireAuth()) {
    return;
  }
  const ModuleSnapshot modules = modulesGetSnapshot();
  const bool mq135AlarmActive = mq135SmokeAlarmActive(modules);
  const Mp3Snapshot mp3 = mp3GetSnapshot();
  const Radar::TargetData &radar = Radar::getTargetData();
  const uint32_t radarAgeMs = Radar::getLastUpdateAgeMs();
  const FM225::FaceState &fm225Face = FM225::getLastFaceState();
  const FM225::UsbUvcParameters &fm225Usb = FM225::getLastUsbUvcParameters();
  const FM225::ImageInfo &fm225Image = FM225::getImageInfo();
  const std::vector<uint16_t> &fm225Users = FM225::getLastUserIds();
  const String fm225Version = FM225::getLastVersion();
  const String fm225Serial = FM225::getLastSerialNumber();
  const String fm225DeviceId = FM225::getLastDeviceId();
  const String fm225Note = FM225::getLastNoteText();
  const bool fm225Responding = fm225LastEventMs > 0 || fm225Version.length() > 0 || fm225Serial.length() > 0 ||
                               fm225Face.updatedAtMs > 0 || fm225Users.size() > 0;
  const RdmSnapshot rfid = rdmGetSnapshot();
  const std::vector<String> rfidTags = rdmGetTags();
  const std::vector<String> rfidPendingTags = rdmGetPendingTags();
  const RelayAutomationSnapshot automation = relayAutomationGetSnapshot();
  const RS485Status rs485 = rs485GetStatus();
  const CANBusStatus can = canBusGetStatus();
  const int energyWatts = 420 + static_cast<int>((sinf(millis() / 30000.0f) + 1.0f) * 65.0f);
  const uint8_t activeRelays = countActiveRelays(modules.relayState);
  const float esp32TemperatureC = temperatureRead();
  const String firmwareBuild = String("AP-FW ") + __DATE__ + " " + __TIME__;

  String json = "{";
  json += "\"deviceName\":\"" + jsonEscape(settings.deviceName) + "\",";
  json += "\"firmware\":\"" + jsonEscape(firmwareBuild) + "\",";
  json += "\"esp32Temperature\":" + jsonFloatOrNull(esp32TemperatureC, 1) + ",";
  json += "\"uptimeSeconds\":" + String(millis() / 1000) + ",";
  json += "\"wifiConnected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"ip\":\"" + currentIpString() + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"temperature\":" + jsonFloatOrNull(modules.temperatureC, 1) + ",";
  json += "\"humidity\":" + jsonFloatOrNull(modules.humidityPercent, 1) + ",";
  json += "\"lux\":" + jsonFloatOrNull(modules.lux, 1) + ",";
  json += "\"bh1750Online\":" + String(modules.bh1750Online ? "true" : "false") + ",";
  json += "\"sht3xOnline\":" + String(modules.sht3xOnline ? "true" : "false") + ",";
  json += "\"mcp23017Online\":" + String(modules.mcp23017Online ? "true" : "false") + ",";
  json += "\"mcp23017Address\":" + String(modules.mcp23017Address) + ",";
  json += "\"i2cErrorCount\":" + String(modules.i2cErrorCount) + ",";
  json += "\"mq135DigitalActive\":" + String(modules.mq135DigitalActive ? "true" : "false") + ",";
  json += "\"mq135AlarmActive\":" + String(mq135AlarmActive ? "true" : "false") + ",";
  json += "\"mq135AlarmThresholdRaw\":" + String(mp3SoundSettings.smokeAlarmThresholdRaw) + ",";
  json += "\"mq135AnalogRaw\":" + String(modules.mq135AnalogRaw) + ",";
  json += "\"mq135AnalogVoltage\":" + jsonFloatOrNull(modules.mq135AnalogVoltage, 2) + ",";
  json += "\"ds18b20Online\":" + String(modules.ds18b20Online ? "true" : "false") + ",";
  json += "\"ds18b20DeviceCount\":" + String(modules.ds18b20DeviceCount) + ",";
  json += "\"ds18b20Temperature\":" + jsonFloatOrNull(modules.ds18b20TemperatureC, 1) + ",";
  json += "\"motion1Active\":" + String(modules.motion1Active ? "true" : "false") + ",";
  json += "\"motion2Active\":" + String(modules.motion2Active ? "true" : "false") + ",";
  json += "\"doorReedClosed\":" + String(modules.doorReedClosed ? "true" : "false") + ",";
  json += "\"garageReedClosed\":" + String(modules.garageReedClosed ? "true" : "false") + ",";
  json += "\"buzzerActive\":" + String(modules.buzzerActive ? "true" : "false") + ",";
  json += "\"storageOnline\":" + String(modules.storageOnline ? "true" : "false") + ",";
  json += "\"storageJedecId\":" + String(modules.storageJedecId) + ",";
  json += "\"storageStatusRegister\":" + String(modules.storageStatusRegister) + ",";
  json += "\"storageTotalBytes\":" + String(modules.storageTotalBytes) + ",";
  json += "\"storageAvailableBytes\":" + String(modules.storageAvailableBytes) + ",";
  json += "\"webStorageReady\":" + String(webStorageReady() ? "true" : "false") + ",";
  json += "\"webStorageFileCount\":" + String(webStorageFileCount()) + ",";
  json += "\"webStorageLastSeedOk\":" + String(webStorageLastSeedOk ? "true" : "false") + ",";
  json += "\"webStorageLastEvent\":\"" + jsonEscape(webStorageLastEvent) + "\",";
  json += "\"rs485Enabled\":" + String(rs485.enabled ? "true" : "false") + ",";
  json += "\"rs485Ready\":" + String(rs485.ready ? "true" : "false") + ",";
  json += "\"rs485Error\":" + String(rs485.error ? "true" : "false") + ",";
  json += "\"rs485Status\":\"" + jsonEscape(rs485.ready ? "Ready" : rs485.lastError) + "\",";
  json += "\"rs485BaudRate\":" + String(rs485.baudRate) + ",";
  json += "\"rs485RxPin\":" + String(PIN_RS485_RX) + ",";
  json += "\"rs485TxPin\":" + String(PIN_RS485_TX) + ",";
  json += "\"rs485DirectionPin\":" + String(PIN_RS485_DE_RE) + ",";
  json += "\"canEnabled\":" + String(can.enabled ? "true" : "false") + ",";
  json += "\"canReady\":" + String(can.ready ? "true" : "false") + ",";
  json += "\"canError\":" + String(can.error ? "true" : "false") + ",";
  json += "\"canStatus\":\"" + jsonEscape(can.ready ? "Ready" : can.lastError) + "\",";
  json += "\"canBitrate\":" + String(can.bitrate) + ",";
  json += "\"canTxPin\":" + String(PIN_CAN_TX) + ",";
  json += "\"canRxPin\":" + String(PIN_CAN_RX) + ",";
  json += "\"tdsMonitorEnabled\":" + String(tdsSnapshot.enabled ? "true" : "false") + ",";
  json += "\"tdsMonitorOnline\":" + String(tdsSnapshot.online ? "true" : "false") + ",";
  json += "\"tdsMonitorAddress\":\"" + jsonEscape(tdsSnapshot.address) + "\",";
  json += "\"tdsMonitorLastEvent\":\"" + jsonEscape(tdsSnapshot.lastEvent) + "\",";
  json += "\"tdsPpm\":" + jsonFloatValue(tdsSnapshot.tdsPpm, 1) + ",";
  json += "\"tdsVoltage\":" + jsonFloatValue(tdsSnapshot.voltage, 3) + ",";
  json += "\"tdsTempC\":" + jsonFloatValue(tdsSnapshot.tempC, 1) + ",";
  json += "\"tdsTempFallbackC\":" + jsonFloatValue(tdsSnapshot.tempFallbackC, 1) + ",";
  json += "\"tdsWaterLevelLabel\":\"" + jsonEscape(tdsSnapshot.waterLevelLabel) + "\",";
  json += "\"tdsWaterLevelInches\":" + jsonFloatValue(tdsSnapshot.waterLevelInches, 1) + ",";
  json += "\"tdsWaterLevelPercent\":" + jsonFloatValue(tdsSnapshot.waterLevelPercent, 0) + ",";
  json += "\"tdsWaterVolumeLiters\":" + jsonFloatValue(tdsSnapshot.waterVolumeLiters, 1) + ",";
  json += "\"tdsTankCapacityLiters\":" + jsonFloatValue(tdsSnapshot.tankCapacityLiters, 1) + ",";
  json += "\"tdsSampleAgeMs\":" + String(tdsSnapshot.sampleAgeMs) + ",";
  json += "\"tdsLastSuccessAgeMs\":" + String(tdsSnapshot.lastSuccessMs == 0 ? 0 : millis() - tdsSnapshot.lastSuccessMs) + ",";
  json += "\"airPurifierEnabled\":" + String(airPurifierSnapshot.enabled ? "true" : "false") + ",";
  json += "\"airPurifierOnline\":" + String(airPurifierSnapshot.online ? "true" : "false") + ",";
  json += "\"airPurifierOn\":" + String(airPurifierSnapshot.purifierOn ? "true" : "false") + ",";
  json += "\"airPurifierFanOn\":" + String(airPurifierSnapshot.fanOn ? "true" : "false") + ",";
  json += "\"airPurifierAutoMode\":" + String(airPurifierSnapshot.autoMode ? "true" : "false") + ",";
  json += "\"airPurifierSensorOnline\":" + String(airPurifierSnapshot.sensorOnline ? "true" : "false") + ",";
  json += "\"airPurifierPmsPowered\":" + String(airPurifierSnapshot.pmsPowered ? "true" : "false") + ",";
  json += "\"airPurifierAddress\":\"" + jsonEscape(airPurifierSnapshot.address) + "\",";
  json += "\"airPurifierLastEvent\":\"" + jsonEscape(airPurifierSnapshot.lastEvent) + "\",";
  json += "\"airPurifierPm1\":" + jsonFloatValue(airPurifierSnapshot.pm1, 1) + ",";
  json += "\"airPurifierPm25\":" + jsonFloatValue(airPurifierSnapshot.pm25, 1) + ",";
  json += "\"airPurifierPm10\":" + jsonFloatValue(airPurifierSnapshot.pm10, 1) + ",";
  json += "\"airPurifierAq\":" + String(airPurifierSnapshot.aq) + ",";
  json += "\"airPurifierFanSpeedPct\":" + String(airPurifierSnapshot.fanSpeedPct) + ",";
  json += "\"airPurifierSampleAgeMs\":" + String(airPurifierSnapshot.sampleAgeMs) + ",";
  json += "\"airPurifierLastSuccessAgeMs\":" + String(airPurifierSnapshot.lastSuccessMs == 0 ? 0 : millis() - airPurifierSnapshot.lastSuccessMs) + ",";
  json += "\"solaxEnabled\":" + String(inverterSettings.solaxEnabled ? "true" : "false") + ",";
  json += "\"solaxOnline\":" + String(solaxSnapshot.online ? "true" : "false") + ",";
  json += "\"solaxLastEvent\":\"" + jsonEscape(solaxSnapshot.lastEvent) + "\",";
  json += "\"solaxLastSuccessAgeMs\":" + String(solaxSnapshot.lastSuccessMs == 0 ? 0 : millis() - solaxSnapshot.lastSuccessMs) + ",";
  json += "\"solaxPvPowerW\":" + jsonFloatValue(solaxSnapshot.pvPowerW, 0) + ",";
  json += "\"solaxGridPowerW\":" + jsonFloatValue(solaxSnapshot.gridPowerW, 0) + ",";
  json += "\"solaxGridVoltageV\":" + jsonFloatValue(solaxSnapshot.gridVoltageV, 0) + ",";
  json += "\"solaxHomePowerW\":" + jsonFloatValue(solaxSnapshot.homePowerW, 0) + ",";
  json += "\"solaxBatteryPowerW\":" + jsonFloatValue(solaxSnapshot.batteryPowerW, 0) + ",";
  json += "\"solaxBatterySoc\":" + jsonFloatValue(solaxSnapshot.batterySoc, 0) + ",";
  json += "\"solaxTodayYieldKwh\":" + jsonFloatValue(solaxSnapshot.todayYieldKwh, 1) + ",";
  json += "\"nitroxEnabled\":" + String(inverterSettings.nitroxEnabled ? "true" : "false") + ",";
  json += "\"nitroxOnline\":" + String(nitroxSnapshot.online ? "true" : "false") + ",";
  json += "\"nitroxLastEvent\":\"" + jsonEscape(nitroxSnapshot.lastEvent) + "\",";
  json += "\"nitroxLastSuccessAgeMs\":" + String(nitroxSnapshot.lastSuccessMs == 0 ? 0 : millis() - nitroxSnapshot.lastSuccessMs) + ",";
  json += "\"nitroxPvPowerW\":" + jsonFloatValue(nitroxSnapshot.pvPowerW, 0) + ",";
  json += "\"nitroxGridPowerW\":" + jsonFloatValue(nitroxSnapshot.gridPowerW, 0) + ",";
  json += "\"nitroxGridVoltageV\":" + jsonFloatValue(nitroxSnapshot.gridVoltageV, 0) + ",";
  json += "\"nitroxHomePowerW\":" + jsonFloatValue(nitroxSnapshot.homePowerW, 0) + ",";
  json += "\"nitroxBatteryPowerW\":" + jsonFloatValue(nitroxSnapshot.batteryPowerW, 0) + ",";
  json += "\"nitroxBatterySoc\":" + jsonFloatValue(nitroxSnapshot.batterySoc, 0) + ",";
  json += "\"growattEnabled\":" + String(inverterSettings.growattEnabled ? "true" : "false") + ",";
  json += "\"growattOnline\":" + String(growattSnapshot.online ? "true" : "false") + ",";
  json += "\"growattLastEvent\":\"" + jsonEscape(growattSnapshot.lastEvent) + "\",";
  json += "\"growattLastSuccessAgeMs\":" + String(growattSnapshot.lastSuccessMs == 0 ? 0 : millis() - growattSnapshot.lastSuccessMs) + ",";
  json += "\"growattPvPowerW\":" + jsonFloatValue(growattSnapshot.pvPowerW, 0) + ",";
  json += "\"growattGridPowerW\":" + jsonFloatValue(growattSnapshot.gridPowerW, 0) + ",";
  json += "\"growattGridVoltageV\":" + jsonFloatValue(growattSnapshot.gridVoltageV, 0) + ",";
  json += "\"growattHomePowerW\":" + jsonFloatValue(growattSnapshot.homePowerW, 0) + ",";
  json += "\"growattBatteryPowerW\":" + jsonFloatValue(growattSnapshot.batteryPowerW, 0) + ",";
  json += "\"growattBatterySoc\":" + jsonFloatValue(growattSnapshot.batterySoc, 0) + ",";
  json += "\"growattTodayYieldKwh\":" + jsonFloatValue(growattSnapshot.todayYieldKwh, 1) + ",";
  json += "\"growattTotalEnergyKwh\":" + jsonFloatValue(growattSnapshot.totalEnergyKwh, 1) + ",";
  json += "\"mp3Initialized\":" + String(mp3.initialized ? "true" : "false") + ",";
  json += "\"mp3Playing\":" + String(mp3.playing ? "true" : "false") + ",";
  json += "\"mp3Folder\":" + String(mp3.folder) + ",";
  json += "\"mp3File\":" + String(mp3.file) + ",";
  json += "\"mp3TotalFiles\":" + String(mp3.totalFiles) + ",";
  json += "\"mp3Volume\":" + String(mp3.volume) + ",";
  json += "\"mp3StartupSoundEnabled\":" + String(mp3SoundSettings.startupSoundEnabled ? "true" : "false") + ",";
  json += "\"mp3StartupTrack\":" + String(mp3SoundSettings.startupTrack) + ",";
  json += "\"mp3SmokeAlarmEnabled\":" + String(mp3SoundSettings.smokeAlarmEnabled ? "true" : "false") + ",";
  json += "\"mp3SmokeAlarmTrack\":" + String(mp3SoundSettings.smokeAlarmTrack) + ",";
  json += "\"mp3SmokeAlarmThresholdRaw\":" + String(mp3SoundSettings.smokeAlarmThresholdRaw) + ",";
  json += "\"radarOnline\":" + String(radar.updatedAtMs > 0 ? "true" : "false") + ",";
  json += "\"radarPresent\":" + String(Radar::isPresent() ? "true" : "false") + ",";
  json += "\"radarMovingTarget\":" + String(Radar::hasMovingTarget() ? "true" : "false") + ",";
  json += "\"radarStationaryTarget\":" + String(Radar::hasStationaryTarget() ? "true" : "false") + ",";
  json += "\"radarState\":\"" + Radar::targetStateName(radar.state) + "\",";
  json += "\"radarMovingDistanceCm\":" + String(radar.movingDistanceCm) + ",";
  json += "\"radarMovingEnergy\":" + String(radar.movingEnergy) + ",";
  json += "\"radarStationaryDistanceCm\":" + String(radar.stationaryDistanceCm) + ",";
  json += "\"radarStationaryEnergy\":" + String(radar.stationaryEnergy) + ",";
  json += "\"radarDetectionDistanceCm\":" + String(radar.detectionDistanceCm) + ",";
  json += "\"radarEngineeringMode\":" + String(radar.engineeringMode ? "true" : "false") + ",";
  json += "\"radarLastUpdateAgeMs\":" + String(radarAgeMs == UINT32_MAX ? 0 : radarAgeMs) + ",";
  json += "\"fm225UartReady\":true,";
  json += "\"fm225Responding\":" + String(fm225Responding ? "true" : "false") + ",";
  json += "\"fm225Status\":\"" + jsonEscape(fm225LastStatus) + "\",";
  json += "\"fm225LastEvent\":\"" + jsonEscape(fm225LastEvent) + "\",";
  json += "\"fm225LastEventAgeMs\":" + String(fm225LastEventMs == 0 ? 0 : millis() - fm225LastEventMs) + ",";
  json += "\"fm225RadarPresenceEnabled\":" + String(fm225RadarSettings.enabled ? "true" : "false") + ",";
  json += "\"fm225RadarMinDistanceCm\":" + String(fm225RadarSettings.minDistanceCm) + ",";
  json += "\"fm225RadarMinEnergy\":" + String(fm225RadarSettings.minEnergy) + ",";
  json += "\"fm225RadarPresenceValid\":" + String(isRadarPresenceValid() ? "true" : "false") + ",";
  json += "\"fm225RadarStatus\":\"" + jsonEscape(fm225RadarStatus) + "\",";
  json += "\"fm225RadarVerifyActive\":" + String(fm225RadarVerifyActive ? "true" : "false") + ",";
  json += "\"fm225RadarCooldownRemainingMs\":" + String(fm225RadarCooldownRemainingMs(millis())) + ",";
  json += "\"fm225Version\":\"" + jsonEscape(fm225Version) + "\",";
  json += "\"fm225SerialNumber\":\"" + jsonEscape(fm225Serial) + "\",";
  json += "\"fm225DeviceId\":\"" + jsonEscape(fm225DeviceId) + "\",";
  json += "\"fm225LastNote\":\"" + jsonEscape(fm225Note) + "\",";
  json += "\"fm225UserCount\":" + String(fm225Users.size()) + ",";
  json += "\"fm225Users\":" + jsonStringArray(fm225Users) + ",";
  json += "\"fm225LastRecognizedUserId\":" + String(fm225LastRecognizedUserId) + ",";
  json += "\"fm225LastRecognizedName\":\"" + jsonEscape(fm225LastRecognizedName) + "\",";
  json += "\"fm225LastVerifyResult\":" + String(fm225LastVerifyResult) + ",";
  json += "\"fm225VerifyPending\":" + String(fm225VerifyPending ? "true" : "false") + ",";
  json += "\"fm225VerifySummary\":\"" + jsonEscape(fm225VerifySummary) + "\",";
  json += "\"fm225LastEnrollResult\":" + String(fm225LastEnrollResult) + ",";
  json += "\"fm225FaceState\":\"" + jsonEscape(FM225::faceStateText(fm225Face.state)) + "\",";
  json += "\"fm225FaceUpdated\":" + String(fm225Face.updatedAtMs > 0 ? "true" : "false") + ",";
  json += "\"fm225FaceYaw\":" + String(fm225Face.yaw) + ",";
  json += "\"fm225FacePitch\":" + String(fm225Face.pitch) + ",";
  json += "\"fm225FaceRoll\":" + String(fm225Face.roll) + ",";
  json += "\"fm225FaceLeft\":" + String(fm225Face.left) + ",";
  json += "\"fm225FaceTop\":" + String(fm225Face.top) + ",";
  json += "\"fm225FaceRight\":" + String(fm225Face.right) + ",";
  json += "\"fm225FaceBottom\":" + String(fm225Face.bottom) + ",";
  json += "\"fm225UsbType\":" + String(fm225Usb.usbType) + ",";
  json += "\"fm225UsbRotate180\":" + String(fm225Usb.rotate180 ? "true" : "false") + ",";
  json += "\"fm225UsbMirror\":" + String(fm225Usb.mirror ? "true" : "false") + ",";
  json += "\"fm225UsbJpegQuality\":" + String(fm225Usb.jpegQuality) + ",";
  json += "\"fm225ImagePacketCount\":" + String(fm225Image.packetCount) + ",";
  json += "\"fm225ImageJpegCount\":" + String(fm225Image.jpegCount) + ",";
  json += "\"fm225ImageLastPayloadSize\":" + String(fm225Image.lastPayloadSize) + ",";
  json += "\"fm225ImageLastPacketWasJpeg\":" + String(fm225Image.lastPacketWasJpeg ? "true" : "false") + ",";
  json += "\"fm225ImageLastHeader\":\"" + jsonEscape(fm225Image.lastHeaderHex) + "\",";
  json += "\"rfidInitialized\":" + String(rfid.initialized ? "true" : "false") + ",";
  json += "\"rfidTagPresent\":" + String(rfid.tagPresent ? "true" : "false") + ",";
  json += "\"rfidLastAuthorized\":" + String(rfid.lastAuthorized ? "true" : "false") + ",";
  json += "\"rfidLastTag\":\"" + jsonEscape(rfid.lastTag) + "\",";
  json += "\"rfidLastEvent\":\"" + jsonEscape(rfid.lastEvent) + "\",";
  json += "\"rfidTagCount\":" + String(rfid.tagCount) + ",";
  json += "\"rfidAddModeActive\":" + String(rfid.addModeActive ? "true" : "false") + ",";
  json += "\"rfidAddModeRemainingMs\":" + String(rfid.addModeRemainingMs) + ",";
  json += "\"rfidPendingTagCount\":" + String(rfid.pendingTagCount) + ",";
  json += "\"rfidPendingTags\":" + jsonRfidTagArray(rfidPendingTags) + ",";
  json += "\"rfidDoorUnlockEnabled\":" + String(rfid.doorUnlockEnabled ? "true" : "false") + ",";
  json += "\"rfidTotalReads\":" + String(rfid.totalReads) + ",";
  json += "\"rfidLastReadAgeMs\":" + String(rfid.lastReadMs == 0 ? 0 : millis() - rfid.lastReadMs) + ",";
  json += "\"rfidTags\":" + jsonRfidTagArray(rfidTags) + ",";
  json += "\"relayAutomationStorageAvailable\":" + String(automation.storageAvailable ? "true" : "false") + ",";
  json += "\"relayAutomationLoaded\":" + String(automation.settingsLoadedFromStorage ? "true" : "false") + ",";
  json += "\"relayAutomationLastSaveOk\":" + String(automation.lastSaveOk ? "true" : "false") + ",";
  json += "\"doorLockOn\":" + String(automation.doorLockActive ? "true" : "false") + ",";
  json += "\"doorLockRemainingMs\":" + String(automation.doorLockRemainingMs) + ",";
  json += "\"garageLockOn\":" + String(automation.garageLockActive ? "true" : "false") + ",";
  json += "\"garageLockRemainingMs\":" + String(automation.garageLockRemainingMs) + ",";
  json += "\"outdoorLightOn\":" + String(automation.outdoorLightOn ? "true" : "false") + ",";
  json += "\"outdoorLightMode\":\"" + String(relayAutomationModeName(automation.settings.outdoorLightMode)) + "\",";
  json += "\"outdoorLightManualState\":" + String(automation.settings.outdoorLightManualState ? "true" : "false") + ",";
  json += "\"exhaustFanOn\":" + String(automation.exhaustFanOn ? "true" : "false") + ",";
  json += "\"exhaustFanMode\":\"" + String(relayAutomationModeName(automation.settings.exhaustFanMode)) + "\",";
  json += "\"exhaustFanManualState\":" + String(automation.settings.exhaustFanManualState ? "true" : "false") + ",";
  json += "\"motionLight1On\":" + String(automation.motionLight1On ? "true" : "false") + ",";
  json += "\"motionLight1Mode\":\"" + String(relayAutomationModeName(automation.settings.motionLight1Mode)) + "\",";
  json += "\"motionLight1ManualState\":" + String(automation.settings.motionLight1ManualState ? "true" : "false") + ",";
  json += "\"motionLight1RemainingMs\":" + String(automation.motionLight1RemainingMs) + ",";
  json += "\"motionLight2On\":" + String(automation.motionLight2On ? "true" : "false") + ",";
  json += "\"motionLight2Mode\":\"" + String(relayAutomationModeName(automation.settings.motionLight2Mode)) + "\",";
  json += "\"motionLight2ManualState\":" + String(automation.settings.motionLight2ManualState ? "true" : "false") + ",";
  json += "\"motionLight2RemainingMs\":" + String(automation.motionLight2RemainingMs) + ",";
  json += "\"doorLockPulseMs\":" + String(automation.settings.doorLockPulseMs) + ",";
  json += "\"garageLockPulseMs\":" + String(automation.settings.garageLockPulseMs) + ",";
  json += "\"outdoorLightOnBelowLux\":" + String(automation.settings.outdoorLightOnBelowLux, 1) + ",";
  json += "\"outdoorLightOffAboveLux\":" + String(automation.settings.outdoorLightOffAboveLux, 1) + ",";
  json += "\"exhaustFanOnAboveTemperature\":" + String(automation.settings.exhaustFanOnAboveTemperature, 1) + ",";
  json += "\"exhaustFanOffBelowTemperature\":" + String(automation.settings.exhaustFanOffBelowTemperature, 1) + ",";
  json += "\"motionLight1DurationMs\":" + String(automation.settings.motionLight1DurationMs) + ",";
  json += "\"motionLight2DurationMs\":" + String(automation.settings.motionLight2DurationMs) + ",";
  json += "\"lightValid\":" + String(modules.lightValid ? "true" : "false") + ",";
  json += "\"climateValid\":" + String(modules.climateValid ? "true" : "false") + ",";
  json += "\"relayState\":" + String(modules.relayState) + ",";
  json += "\"relays\":" + relayJson(modules.relayState) + ",";
  json += "\"relayConfigs\":" + relayConfigsJson(automation.settings) + ",";
  json += "\"activeRelays\":" + String(activeRelays) + ",";
  json += "\"energyWatts\":" + String(energyWatts) + ",";
  json += "\"activeDevices\":" + String(activeRelays) + ",";
  json += "\"securityState\":\"Armed Stay\",";
  json += "\"automationMode\":\"" + jsonEscape(settings.mode) + "\",";
  json += "\"automationEnabled\":" + String(settings.automationEnabled ? "true" : "false");
  json += "}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void handleGetSettingsApi() {
  if (!requireAuth()) {
    return;
  }
  const RelayAutomationSettings relaySettings = relayAutomationGetSettings();

  String json = "{";
  json += "\"deviceName\":\"" + jsonEscape(settings.deviceName) + "\",";
  json += "\"automationEnabled\":" + String(settings.automationEnabled ? "true" : "false") + ",";
  json += "\"notificationsEnabled\":" + String(settings.notificationsEnabled ? "true" : "false") + ",";
  json += "\"targetTemperature\":" + String(settings.targetTemperature) + ",";
  json += "\"brightness\":" + String(settings.brightness) + ",";
  json += "\"mode\":\"" + jsonEscape(settings.mode) + "\",";
  json += "\"wifiSsid\":\"" + jsonEscape(String(networkSettings.ssid)) + "\",";
  json += "\"mdnsHostname\":\"" + jsonEscape(String(networkSettings.mdnsHostname)) + "\",";
  json += "\"otaEnabled\":" + String(networkSettings.otaEnabled ? "true" : "false") + ",";
  json += "\"loginAuthEnabled\":" + String(securitySettings.loginEnabled ? "true" : "false") + ",";
  json += "\"loginUsername\":\"" + jsonEscape(String(securitySettings.username)) + "\",";
  json += "\"logRfidEnabled\":" + String(logSettings.rfid ? "true" : "false") + ",";
  json += "\"logFm225Enabled\":" + String(logSettings.fm225 ? "true" : "false") + ",";
  json += "\"logDoorReedEnabled\":" + String(logSettings.doorReed ? "true" : "false") + ",";
  json += "\"logGarageReedEnabled\":" + String(logSettings.garageReed ? "true" : "false") + ",";
  json += "\"logDoorUnlockEnabled\":" + String(logSettings.doorUnlock ? "true" : "false") + ",";
  json += "\"logGarageUnlockEnabled\":" + String(logSettings.garageUnlock ? "true" : "false") + ",";
  json += "\"doorLockPulseMs\":" + String(relaySettings.doorLockPulseMs) + ",";
  json += "\"garageLockPulseMs\":" + String(relaySettings.garageLockPulseMs) + ",";
  json += "\"outdoorLightOnBelowLux\":" + String(relaySettings.outdoorLightOnBelowLux, 1) + ",";
  json += "\"outdoorLightOffAboveLux\":" + String(relaySettings.outdoorLightOffAboveLux, 1) + ",";
  json += "\"exhaustFanOnAboveTemperature\":" + String(relaySettings.exhaustFanOnAboveTemperature, 1) + ",";
  json += "\"exhaustFanOffBelowTemperature\":" + String(relaySettings.exhaustFanOffBelowTemperature, 1) + ",";
  json += "\"motionLight1DurationMs\":" + String(relaySettings.motionLight1DurationMs) + ",";
  json += "\"motionLight2DurationMs\":" + String(relaySettings.motionLight2DurationMs) + ",";
  json += "\"rfidDoorUnlockEnabled\":" + String(rdmGetDoorUnlockEnabled() ? "true" : "false") + ",";
  json += "\"mp3Volume\":" + String(mp3SoundSettings.volume) + ",";
  json += "\"mp3StartupSoundEnabled\":" + String(mp3SoundSettings.startupSoundEnabled ? "true" : "false") + ",";
  json += "\"mp3StartupTrack\":" + String(mp3SoundSettings.startupTrack) + ",";
  json += "\"mp3SmokeAlarmEnabled\":" + String(mp3SoundSettings.smokeAlarmEnabled ? "true" : "false") + ",";
  json += "\"mp3SmokeAlarmTrack\":" + String(mp3SoundSettings.smokeAlarmTrack) + ",";
  json += "\"mp3SmokeAlarmThresholdRaw\":" + String(mp3SoundSettings.smokeAlarmThresholdRaw) + ",";
  json += "\"tdsMonitorEnabled\":" + String(tdsSettings.enabled ? "true" : "false") + ",";
  json += "\"tdsMonitorAddress\":\"" + jsonEscape(String(tdsSettings.address)) + "\",";
  json += "\"airPurifierEnabled\":" + String(airPurifierSettings.enabled ? "true" : "false") + ",";
  json += "\"airPurifierAddress\":\"" + jsonEscape(String(airPurifierSettings.address)) + "\",";
  json += "\"rs485Enabled\":" + String(communicationSettings.rs485Enabled ? "true" : "false") + ",";
  json += "\"rs485BaudRate\":" + String(communicationSettings.rs485BaudRate) + ",";
  json += "\"rs485RxPin\":" + String(PIN_RS485_RX) + ",";
  json += "\"rs485TxPin\":" + String(PIN_RS485_TX) + ",";
  json += "\"rs485DirectionPin\":" + String(PIN_RS485_DE_RE) + ",";
  json += "\"canEnabled\":" + String(communicationSettings.canEnabled ? "true" : "false") + ",";
  json += "\"canBitrate\":" + String(communicationSettings.canBitrate) + ",";
  json += "\"canTxPin\":" + String(PIN_CAN_TX) + ",";
  json += "\"canRxPin\":" + String(PIN_CAN_RX) + ",";
  json += "\"solaxEnabled\":" + String(inverterSettings.solaxEnabled ? "true" : "false") + ",";
  json += "\"solaxAddress\":\"" + jsonEscape(String(inverterSettings.solaxAddress)) + "\",";
  json += "\"solaxPassword\":\"" + jsonEscape(String(inverterSettings.solaxPassword)) + "\",";
  json += "\"solaxIntervalMs\":" + String(inverterSettings.solaxIntervalMs) + ",";
  json += "\"nitroxEnabled\":" + String(inverterSettings.nitroxEnabled ? "true" : "false") + ",";
  json += "\"nitroxHost\":\"" + jsonEscape(String(inverterSettings.nitroxHost)) + "\",";
  json += "\"nitroxPort\":" + String(inverterSettings.nitroxPort) + ",";
  json += "\"nitroxLoggerSerial\":" + String(inverterSettings.nitroxLoggerSerial) + ",";
  json += "\"nitroxSlaveId\":" + String(inverterSettings.nitroxSlaveId) + ",";
  json += "\"nitroxIntervalMs\":" + String(inverterSettings.nitroxIntervalMs) + ",";
  json += "\"growattEnabled\":" + String(inverterSettings.growattEnabled ? "true" : "false") + ",";
  json += "\"growattBaseUrl\":\"" + jsonEscape(String(inverterSettings.growattBaseUrl)) + "\",";
  json += "\"growattToken\":\"" + jsonEscape(String(inverterSettings.growattToken)) + "\",";
  json += "\"growattPlantId\":" + String(inverterSettings.growattPlantId) + ",";
  json += "\"growattIntervalMs\":" + String(inverterSettings.growattIntervalMs) + ",";
  json += "\"inverterSettingsSaved\":" + String(inverterSettingsLastSaveOk ? "true" : "false") + ",";
  json += "\"fm225RadarPresenceEnabled\":" + String(fm225RadarSettings.enabled ? "true" : "false") + ",";
  json += "\"fm225RadarMinDistanceCm\":" + String(fm225RadarSettings.minDistanceCm) + ",";
  json += "\"fm225RadarMinEnergy\":" + String(fm225RadarSettings.minEnergy) + ",";
  json += "\"outdoorLightMode\":\"" + String(relayAutomationModeName(relaySettings.outdoorLightMode)) + "\",";
  json += "\"exhaustFanMode\":\"" + String(relayAutomationModeName(relaySettings.exhaustFanMode)) + "\",";
  json += "\"motionLight1Mode\":\"" + String(relayAutomationModeName(relaySettings.motionLight1Mode)) + "\",";
  json += "\"motionLight2Mode\":\"" + String(relayAutomationModeName(relaySettings.motionLight2Mode)) + "\",";
  json += "\"relayConfigs\":" + relayConfigsJson(relaySettings);
  json += "}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

void handleEventLogsApi() {
  if (!requireAuth()) {
    return;
  }

  EventLogHeader header;
  memset(eventLogScratch, 0, sizeof(eventLogScratch));
  loadEventLog(header, eventLogScratch);
  const uint32_t nowSeconds = millis() / 1000UL;
  bool pruned = false;
  uint16_t kept = 0;
  for (uint16_t i = 0; i < header.count; i++) {
    if (nowSeconds >= LOG_RETENTION_SECONDS && eventLogScratch[i].uptimeSeconds + LOG_RETENTION_SECONDS < nowSeconds) {
      pruned = true;
      continue;
    }
    if (kept != i) {
      eventLogScratch[kept] = eventLogScratch[i];
    }
    kept++;
  }
  header.count = kept;
  if (pruned) {
    saveEventLog(header, eventLogScratch);
  }

  String json = "{";
  json += "\"count\":" + String(header.count) + ",";
  json += "\"logs\":[";
  for (uint16_t i = 0; i < header.count; i++) {
    if (i > 0) {
      json += ",";
    }
    json += "{";
    json += "\"uptimeSeconds\":" + String(eventLogScratch[i].uptimeSeconds) + ",";
    json += "\"category\":\"" + jsonEscape(String(eventLogScratch[i].category)) + "\",";
    json += "\"message\":\"" + jsonEscape(String(eventLogScratch[i].message)) + "\"";
    json += "}";
  }
  json += "]}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}

bool formBool(const char *name) {
  return server.hasArg(name) && server.arg(name) == "true";
}

int boundedFormInt(const char *name, int fallback, int minimum, int maximum) {
  if (!server.hasArg(name)) {
    return fallback;
  }

  const int value = server.arg(name).toInt();
  return constrain(value, minimum, maximum);
}

float boundedFormFloat(const char *name, float fallback, float minimum, float maximum) {
  if (!server.hasArg(name)) {
    return fallback;
  }

  const float value = server.arg(name).toFloat();
  if (!isfinite(value)) {
    return fallback;
  }
  return constrain(value, minimum, maximum);
}

String relayConfigField(uint8_t channel, const char *field) {
  return "relay" + String(channel + 1) + field;
}

String relayPinName(uint8_t channel) {
  return String(channel < 8 ? "GPA" : "GPB") + String(channel % 8);
}

String relayConfigsJson(const RelayAutomationSettings &relaySettings) {
  String json = "[";
  for (uint8_t channel = 0; channel < RELAY_CHANNEL_COUNT; channel++) {
    if (channel > 0) {
      json += ",";
    }
    const RelayChannelConfig &config = relaySettings.channels[channel];
    json += "{";
    json += "\"channel\":" + String(channel) + ",";
    json += "\"relayNumber\":" + String(channel + 1) + ",";
    json += "\"mcpPin\":\"" + relayPinName(channel) + "\",";
    json += "\"name\":\"" + jsonEscape(String(config.name)) + "\",";
    json += "\"mode\":\"" + String(relayChannelModeName(config.mode)) + "\",";
    json += "\"showInDashboard\":" + String(config.showInDashboard ? "true" : "false") + ",";
    json += "\"currentState\":" + String(relayGet(channel) ? "true" : "false") + ",";
    json += "\"pulseDurationMs\":" + String(config.pulseDurationMs) + ",";
    json += "\"pulseRole\":\"" + String(relayPulseRoleName(config.pulseRole)) + "\",";
    json += "\"automaticControlType\":\"" + String(relayAutomaticControlTypeName(config.automaticControlType)) + "\",";
    json += "\"sensor\":\"" + String(relaySensorTypeName(config.sensor)) + "\",";
    json += "\"comparison\":\"" + String(relayComparisonConditionName(config.comparison)) + "\",";
    json += "\"onThreshold\":" + String(config.onThreshold, 1) + ",";
    json += "\"offThreshold\":" + String(config.offThreshold, 1) + ",";
    json += "\"scheduleOnMinutes\":" + String(config.scheduleOnMinutes) + ",";
    json += "\"scheduleOffMinutes\":" + String(config.scheduleOffMinutes) + ",";
    json += "\"enabledWeekdays\":" + String(config.enabledWeekdays);
    json += "}";
  }
  json += "]";
  return json;
}

void updateRelayChannelSettingsFromForm(RelayAutomationSettings &relaySettings) {
  for (uint8_t channel = 0; channel < RELAY_CHANNEL_COUNT; channel++) {
    RelayChannelConfig &config = relaySettings.channels[channel];
    const String nameField = relayConfigField(channel, "Name");
    if (server.hasArg(nameField)) {
      String name = server.arg(nameField);
      name.trim();
      if (name.length() == 0) {
        name = "Relay " + String(channel + 1);
      }
      memset(config.name, 0, sizeof(config.name));
      name.substring(0, sizeof(config.name) - 1).toCharArray(config.name, sizeof(config.name));
    }

    const String modeField = relayConfigField(channel, "Mode");
    RelayChannelMode mode;
    if (server.hasArg(modeField) && relayAutomationParseChannelMode(server.arg(modeField), mode)) {
      config.mode = mode;
    }

    config.showInDashboard = formBool(relayConfigField(channel, "ShowInDashboard").c_str());
    config.currentState = formBool(relayConfigField(channel, "CurrentState").c_str());
    config.pulseDurationMs =
      static_cast<uint32_t>(boundedFormInt(relayConfigField(channel, "PulseDurationMs").c_str(), config.pulseDurationMs, 100, 604800000));
    RelayPulseRole pulseRole;
    if (server.hasArg(relayConfigField(channel, "PulseRole")) &&
        relayAutomationParsePulseRole(server.arg(relayConfigField(channel, "PulseRole")), pulseRole)) {
      config.pulseRole = pulseRole;
    }
    RelayAutomaticControlType automaticType;
    if (server.hasArg(relayConfigField(channel, "AutomaticControlType")) &&
        relayAutomationParseAutomaticControlType(server.arg(relayConfigField(channel, "AutomaticControlType")), automaticType)) {
      config.automaticControlType = automaticType;
    }
    RelaySensorType sensor;
    if (server.hasArg(relayConfigField(channel, "Sensor")) &&
        relayAutomationParseSensorType(server.arg(relayConfigField(channel, "Sensor")), sensor)) {
      config.sensor = sensor;
    }
    RelayComparisonCondition condition;
    if (server.hasArg(relayConfigField(channel, "Comparison")) &&
        relayAutomationParseComparisonCondition(server.arg(relayConfigField(channel, "Comparison")), condition)) {
      config.comparison = condition;
    }
    config.onThreshold = boundedFormFloat(relayConfigField(channel, "OnThreshold").c_str(), config.onThreshold, -100.0f, 100000.0f);
    config.offThreshold = boundedFormFloat(relayConfigField(channel, "OffThreshold").c_str(), config.offThreshold, -100.0f, 100000.0f);
    config.scheduleOnMinutes =
      static_cast<uint16_t>(boundedFormInt(relayConfigField(channel, "ScheduleOnMinutes").c_str(), config.scheduleOnMinutes, 0, 1439));
    config.scheduleOffMinutes =
      static_cast<uint16_t>(boundedFormInt(relayConfigField(channel, "ScheduleOffMinutes").c_str(), config.scheduleOffMinutes, 0, 1439));
    config.enabledWeekdays =
      static_cast<uint8_t>(boundedFormInt(relayConfigField(channel, "EnabledWeekdays").c_str(), config.enabledWeekdays, 0, 127));
  }
}

void handlePostSettingsApi() {
  if (!requireAuth()) {
    return;
  }
  if (server.hasArg("deviceName")) {
    settings.deviceName = server.arg("deviceName");
    settings.deviceName.trim();
    if (settings.deviceName.length() == 0) {
      settings.deviceName = "Home Automation Hub";
    }
  }

  settings.automationEnabled = formBool("automationEnabled");
  settings.notificationsEnabled = formBool("notificationsEnabled");
  settings.targetTemperature = boundedFormInt("targetTemperature", settings.targetTemperature, 16, 32);
  settings.brightness = boundedFormInt("brightness", settings.brightness, 0, 100);

  if (server.hasArg("mode")) {
    const String mode = server.arg("mode");
    if (mode == "auto" || mode == "home" || mode == "away" || mode == "night") {
      settings.mode = mode;
    }
  }

  if (server.hasArg("wifiSsid")) {
    String ssid = server.arg("wifiSsid");
    ssid.trim();
    if (ssid.length() > 0) {
      memset(networkSettings.ssid, 0, sizeof(networkSettings.ssid));
      ssid.substring(0, WIFI_SSID_SIZE - 1).toCharArray(networkSettings.ssid, WIFI_SSID_SIZE);
    }
  }
  if (server.hasArg("wifiPassword")) {
    String password = server.arg("wifiPassword");
    password.trim();
    if (password.length() > 0) {
      memset(networkSettings.password, 0, sizeof(networkSettings.password));
      password.substring(0, WIFI_PASSWORD_SIZE - 1).toCharArray(networkSettings.password, WIFI_PASSWORD_SIZE);
    }
  }
  if (server.hasArg("mdnsHostname")) {
    const String hostname = sanitizeMdnsHostname(server.arg("mdnsHostname"));
    memset(networkSettings.mdnsHostname, 0, sizeof(networkSettings.mdnsHostname));
    hostname.substring(0, MDNS_HOSTNAME_SIZE - 1).toCharArray(networkSettings.mdnsHostname, MDNS_HOSTNAME_SIZE);
  }
  networkSettings.otaEnabled = formBool("otaEnabled");
  sanitizeNetworkSettings(networkSettings);
  saveNetworkSettings();

  securitySettings.loginEnabled = formBool("loginAuthEnabled");
  if (server.hasArg("loginUsername")) {
    String username = server.arg("loginUsername");
    username.trim();
    if (username.length() > 0) {
      memset(securitySettings.username, 0, sizeof(securitySettings.username));
      username.substring(0, SECURITY_USERNAME_SIZE - 1).toCharArray(securitySettings.username, SECURITY_USERNAME_SIZE);
    }
  }
  if (server.hasArg("loginPassword")) {
    String password = server.arg("loginPassword");
    password.trim();
    if (password.length() > 0) {
      memset(securitySettings.password, 0, sizeof(securitySettings.password));
      password.substring(0, SECURITY_PASSWORD_SIZE - 1).toCharArray(securitySettings.password, SECURITY_PASSWORD_SIZE);
    }
  }
  sanitizeSecuritySettings(securitySettings);
  saveSecuritySettings();

  logSettings.rfid = formBool("logRfidEnabled");
  logSettings.fm225 = formBool("logFm225Enabled");
  logSettings.doorReed = formBool("logDoorReedEnabled");
  logSettings.garageReed = formBool("logGarageReedEnabled");
  logSettings.doorUnlock = formBool("logDoorUnlockEnabled");
  logSettings.garageUnlock = formBool("logGarageUnlockEnabled");
  saveLogSettings();

  RelayAutomationSettings relaySettings = relayAutomationGetSettings();
  relaySettings.doorLockPulseMs =
    boundedFormInt("doorLockPulseMs", relaySettings.doorLockPulseMs, 100, 30000);
  relaySettings.garageLockPulseMs =
    boundedFormInt("garageLockPulseMs", relaySettings.garageLockPulseMs, 100, 30000);
  relaySettings.outdoorLightOnBelowLux =
    boundedFormFloat("outdoorLightOnBelowLux", relaySettings.outdoorLightOnBelowLux, 0.0f, 100000.0f);
  relaySettings.outdoorLightOffAboveLux =
    boundedFormFloat("outdoorLightOffAboveLux", relaySettings.outdoorLightOffAboveLux, 0.0f, 100000.0f);
  relaySettings.exhaustFanOnAboveTemperature =
    boundedFormFloat("exhaustFanOnAboveTemperature", relaySettings.exhaustFanOnAboveTemperature, -40.0f, 100.0f);
  relaySettings.exhaustFanOffBelowTemperature =
    boundedFormFloat("exhaustFanOffBelowTemperature", relaySettings.exhaustFanOffBelowTemperature, -40.0f, 100.0f);
  relaySettings.motionLight1DurationMs =
    boundedFormInt("motionLight1DurationMs", relaySettings.motionLight1DurationMs, 1000, 3600000);
  relaySettings.motionLight2DurationMs =
    boundedFormInt("motionLight2DurationMs", relaySettings.motionLight2DurationMs, 1000, 3600000);
  if (server.hasArg("rfidDoorUnlockEnabled")) {
    rdmSetDoorUnlockEnabled(formBool("rfidDoorUnlockEnabled"));
  }
  mp3SoundSettings.volume =
    static_cast<uint8_t>(boundedFormInt("mp3Volume", mp3SoundSettings.volume, 0, MP3_SOUND_MAX_VOLUME));
  mp3SoundSettings.startupSoundEnabled = formBool("mp3StartupSoundEnabled");
  mp3SoundSettings.startupTrack =
    static_cast<uint8_t>(boundedFormInt("mp3StartupTrack", mp3SoundSettings.startupTrack, MP3_SOUND_MIN_TRACK, MP3_SOUND_MAX_TRACK));
  mp3SoundSettings.smokeAlarmEnabled = formBool("mp3SmokeAlarmEnabled");
  mp3SoundSettings.smokeAlarmTrack =
    static_cast<uint8_t>(boundedFormInt("mp3SmokeAlarmTrack", mp3SoundSettings.smokeAlarmTrack, MP3_SOUND_MIN_TRACK, MP3_SOUND_MAX_TRACK));
  mp3SoundSettings.smokeAlarmThresholdRaw =
    static_cast<uint16_t>(boundedFormInt("mp3SmokeAlarmThresholdRaw", mp3SoundSettings.smokeAlarmThresholdRaw, 0, MQ135_ALARM_MAX_RAW));
  sanitizeMp3SoundSettings(mp3SoundSettings);
  saveMp3SoundSettings();
  mp3SetVolume(mp3SoundSettings.volume);
  tdsSettings.enabled = formBool("tdsMonitorEnabled");
  if (server.hasArg("tdsMonitorAddress")) {
    const String address = normalizeTdsAddress(server.arg("tdsMonitorAddress"));
    memset(tdsSettings.address, 0, sizeof(tdsSettings.address));
    address.substring(0, TDS_ADDRESS_SIZE - 1).toCharArray(tdsSettings.address, TDS_ADDRESS_SIZE);
  }
  saveTdsMonitorSettings();
  tdsSnapshot.lastPollMs = 0;
  tdsSnapshot.nextPollMs = 0;
  tdsSnapshot.address = String(tdsSettings.address);
  if (!tdsSettings.enabled) {
    updateTdsDisabledSnapshot();
  }
  airPurifierSettings.enabled = formBool("airPurifierEnabled");
  if (server.hasArg("airPurifierAddress")) {
    const String address = normalizeAirPurifierAddress(server.arg("airPurifierAddress"));
    memset(airPurifierSettings.address, 0, sizeof(airPurifierSettings.address));
    address.substring(0, AIR_PURIFIER_ADDRESS_SIZE - 1).toCharArray(airPurifierSettings.address, AIR_PURIFIER_ADDRESS_SIZE);
  }
  saveAirPurifierSettings();
  airPurifierSnapshot.lastPollMs = 0;
  airPurifierSnapshot.nextPollMs = 0;
  airPurifierSnapshot.address = String(airPurifierSettings.address);
  if (!airPurifierSettings.enabled) {
    updateAirPurifierDisabledSnapshot();
  }
  communicationSettings.rs485Enabled = formBool("rs485Enabled");
  communicationSettings.rs485BaudRate =
    static_cast<uint32_t>(boundedFormInt("rs485BaudRate", communicationSettings.rs485BaudRate, 1200, 921600));
  communicationSettings.canEnabled = formBool("canEnabled");
  communicationSettings.canBitrate =
    static_cast<uint32_t>(boundedFormInt("canBitrate", communicationSettings.canBitrate, 125000, 1000000));
  sanitizeCommunicationSettings(communicationSettings);
  saveCommunicationSettings();
  applyCommunicationSettings();
  inverterSettings.solaxEnabled = formBool("solaxEnabled");
  if (server.hasArg("solaxAddress")) {
    const String address = normalizeSolaxAddress(server.arg("solaxAddress"));
    memset(inverterSettings.solaxAddress, 0, sizeof(inverterSettings.solaxAddress));
    address.substring(0, INVERTER_ADDRESS_SIZE - 1).toCharArray(inverterSettings.solaxAddress, INVERTER_ADDRESS_SIZE);
  }
  if (server.hasArg("solaxPassword")) {
    String password = server.arg("solaxPassword");
    password.trim();
    memset(inverterSettings.solaxPassword, 0, sizeof(inverterSettings.solaxPassword));
    password.substring(0, INVERTER_PASSWORD_SIZE - 1).toCharArray(inverterSettings.solaxPassword, INVERTER_PASSWORD_SIZE);
  }
  inverterSettings.solaxIntervalMs =
    static_cast<uint32_t>(boundedFormInt("solaxIntervalSeconds", inverterSettings.solaxIntervalMs / 1000, 5, 300)) * 1000UL;
  inverterSettings.nitroxEnabled = formBool("nitroxEnabled");
  if (server.hasArg("nitroxHost")) {
    String host = server.arg("nitroxHost");
    host.trim();
    memset(inverterSettings.nitroxHost, 0, sizeof(inverterSettings.nitroxHost));
    host.substring(0, INVERTER_HOST_SIZE - 1).toCharArray(inverterSettings.nitroxHost, INVERTER_HOST_SIZE);
  }
  inverterSettings.nitroxPort =
    static_cast<uint16_t>(boundedFormInt("nitroxPort", inverterSettings.nitroxPort, 1, 65535));
  inverterSettings.nitroxLoggerSerial =
    static_cast<uint32_t>(boundedFormInt("nitroxLoggerSerial", inverterSettings.nitroxLoggerSerial, 1, 2147483647));
  inverterSettings.nitroxSlaveId =
    static_cast<uint8_t>(boundedFormInt("nitroxSlaveId", inverterSettings.nitroxSlaveId, 1, 247));
  inverterSettings.nitroxIntervalMs =
    static_cast<uint32_t>(boundedFormInt("nitroxIntervalSeconds", inverterSettings.nitroxIntervalMs / 1000, 5, 300)) * 1000UL;
  inverterSettings.growattEnabled = formBool("growattEnabled");
  const bool requestedGrowattEnabled = inverterSettings.growattEnabled;
  if (server.hasArg("growattBaseUrl")) {
    const String baseUrl = normalizeGrowattBaseUrl(server.arg("growattBaseUrl"));
    memset(inverterSettings.growattBaseUrl, 0, sizeof(inverterSettings.growattBaseUrl));
    baseUrl.substring(0, INVERTER_ADDRESS_SIZE - 1).toCharArray(inverterSettings.growattBaseUrl, INVERTER_ADDRESS_SIZE);
  }
  if (server.hasArg("growattToken")) {
    String token = server.arg("growattToken");
    token.trim();
    memset(inverterSettings.growattToken, 0, sizeof(inverterSettings.growattToken));
    token.substring(0, GROWATT_TOKEN_SIZE - 1).toCharArray(inverterSettings.growattToken, GROWATT_TOKEN_SIZE);
  }
  inverterSettings.growattPlantId =
    static_cast<uint32_t>(boundedFormInt("growattPlantId", inverterSettings.growattPlantId, 0, 2147483647));
  inverterSettings.growattIntervalMs =
    static_cast<uint32_t>(boundedFormInt("growattIntervalSeconds", inverterSettings.growattIntervalMs / 1000, 60, 3600)) * 1000UL;
  sanitizeInverterSettings(inverterSettings);
  inverterSettings.growattEnabled = requestedGrowattEnabled;
  inverterSettingsLastSaveOk = saveInverterSettings();
  solaxSnapshot.lastPollMs = 0;
  nitroxSnapshot.lastPollMs = 0;
  growattSnapshot.lastPollMs = 0;
  solaxSnapshot.nextPollMs = 0;
  nitroxSnapshot.nextPollMs = 0;
  growattSnapshot.nextPollMs = 0;
  if (!inverterSettings.solaxEnabled) {
    solaxSnapshot.online = false;
    solaxSnapshot.lastEvent = "SolaX disabled";
  }
  if (!inverterSettings.nitroxEnabled) {
    nitroxSnapshot.online = false;
    nitroxSnapshot.lastEvent = "Nitrox disabled";
  }
  if (!inverterSettings.growattEnabled) {
    growattSnapshot.online = false;
    growattSnapshot.lastEvent = "Growatt disabled";
  }
  fm225RadarSettings.enabled = formBool("fm225RadarPresenceEnabled");
  fm225RadarSettings.minDistanceCm =
    boundedFormInt("fm225RadarMinDistanceCm", fm225RadarSettings.minDistanceCm, 0, 1000);
  fm225RadarSettings.minEnergy =
    boundedFormInt("fm225RadarMinEnergy", fm225RadarSettings.minEnergy, 0, 100);
  saveFm225RadarSettings();

  RelayAutomationMode automationMode;
  if (server.hasArg("outdoorLightMode") && relayAutomationParseMode(server.arg("outdoorLightMode"), automationMode)) {
    relaySettings.outdoorLightMode = automationMode;
  }
  if (server.hasArg("exhaustFanMode") && relayAutomationParseMode(server.arg("exhaustFanMode"), automationMode)) {
    relaySettings.exhaustFanMode = automationMode;
  }
  if (server.hasArg("motionLight1Mode") && relayAutomationParseMode(server.arg("motionLight1Mode"), automationMode)) {
    relaySettings.motionLight1Mode = automationMode;
  }
  if (server.hasArg("motionLight2Mode") && relayAutomationParseMode(server.arg("motionLight2Mode"), automationMode)) {
    relaySettings.motionLight2Mode = automationMode;
  }
  updateRelayChannelSettingsFromForm(relaySettings);
  relayAutomationUpdateSettings(relaySettings);

  handleGetSettingsApi();
}

bool parseRelayChannel(uint8_t &channel) {
  if (!server.hasArg("channel")) {
    server.send(400, "application/json", "{\"error\":\"missing_channel\"}");
    return false;
  }

  const int parsedChannel = server.arg("channel").toInt();
  if (parsedChannel < 0 || parsedChannel >= RELAY_CHANNEL_COUNT) {
    server.send(400, "application/json", "{\"error\":\"invalid_channel\"}");
    return false;
  }

  channel = static_cast<uint8_t>(parsedChannel);
  return true;
}

void sendRelayApiResponse(bool ok) {
  const ModuleSnapshot modules = modulesGetSnapshot();
  String json = "{";
  json += "\"ok\":" + String(ok ? "true" : "false") + ",";
  json += "\"mcp23017Online\":" + String(modules.mcp23017Online ? "true" : "false") + ",";
  json += "\"relayState\":" + String(modules.relayState) + ",";
  json += "\"relays\":" + relayJson(modules.relayState);
  json += "}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(ok ? 200 : 503, "application/json", json);
}

void sendMp3ApiResponse(bool ok) {
  const Mp3Snapshot mp3 = mp3GetSnapshot();
  String json = "{";
  json += "\"ok\":" + String(ok ? "true" : "false") + ",";
  json += "\"initialized\":" + String(mp3.initialized ? "true" : "false") + ",";
  json += "\"playing\":" + String(mp3.playing ? "true" : "false") + ",";
  json += "\"folder\":" + String(mp3.folder) + ",";
  json += "\"file\":" + String(mp3.file) + ",";
  json += "\"totalFiles\":" + String(mp3.totalFiles) + ",";
  json += "\"volume\":" + String(mp3.volume);
  json += "}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(ok ? 200 : 503, "application/json", json);
}

int formInt(const char *name, int fallback) {
  if (!server.hasArg(name)) {
    return fallback;
  }
  return server.arg(name).toInt();
}

String formString(const char *name, const String &fallback) {
  if (!server.hasArg(name)) {
    return fallback;
  }

  String value = server.arg(name);
  value.trim();
  return value;
}

void sendFm225ApiResponse(bool ok = true) {
  const FM225::FaceState &face = FM225::getLastFaceState();
  const std::vector<uint16_t> &users = FM225::getLastUserIds();

  String json = "{";
  json += "\"ok\":" + String(ok ? "true" : "false") + ",";
  json += "\"status\":\"" + jsonEscape(fm225LastStatus) + "\",";
  json += "\"lastEvent\":\"" + jsonEscape(fm225LastEvent) + "\",";
  json += "\"version\":\"" + jsonEscape(FM225::getLastVersion()) + "\",";
  json += "\"serialNumber\":\"" + jsonEscape(FM225::getLastSerialNumber()) + "\",";
  json += "\"userCount\":" + String(users.size()) + ",";
  json += "\"users\":" + jsonStringArray(users) + ",";
  json += "\"faceState\":\"" + jsonEscape(FM225::faceStateText(face.state)) + "\"";
  json += "}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(ok ? 200 : 400, "application/json", json);
}

void sendRelayAutomationApiResponse(bool ok = true) {
  const RelayAutomationSnapshot automation = relayAutomationGetSnapshot();
  String json = "{";
  json += "\"ok\":" + String(ok ? "true" : "false") + ",";
  json += "\"storageAvailable\":" + String(automation.storageAvailable ? "true" : "false") + ",";
  json += "\"loaded\":" + String(automation.settingsLoadedFromStorage ? "true" : "false") + ",";
  json += "\"lastSaveOk\":" + String(automation.lastSaveOk ? "true" : "false") + ",";
  json += "\"doorLockOn\":" + String(automation.doorLockActive ? "true" : "false") + ",";
  json += "\"garageLockOn\":" + String(automation.garageLockActive ? "true" : "false") + ",";
  json += "\"outdoorLightOn\":" + String(automation.outdoorLightOn ? "true" : "false") + ",";
  json += "\"outdoorLightMode\":\"" + String(relayAutomationModeName(automation.settings.outdoorLightMode)) + "\",";
  json += "\"exhaustFanOn\":" + String(automation.exhaustFanOn ? "true" : "false") + ",";
  json += "\"exhaustFanMode\":\"" + String(relayAutomationModeName(automation.settings.exhaustFanMode)) + "\",";
  json += "\"motionLight1On\":" + String(automation.motionLight1On ? "true" : "false") + ",";
  json += "\"motionLight1Mode\":\"" + String(relayAutomationModeName(automation.settings.motionLight1Mode)) + "\",";
  json += "\"motionLight2On\":" + String(automation.motionLight2On ? "true" : "false") + ",";
  json += "\"motionLight2Mode\":\"" + String(relayAutomationModeName(automation.settings.motionLight2Mode)) + "\"";
  json += "}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(ok ? 200 : 400, "application/json", json);
}

void sendRfidApiResponse(bool ok = true) {
  const RdmSnapshot rfid = rdmGetSnapshot();
  const std::vector<String> tags = rdmGetTags();
  const std::vector<String> pendingTags = rdmGetPendingTags();

  String json = "{";
  json += "\"ok\":" + String(ok ? "true" : "false") + ",";
  json += "\"initialized\":" + String(rfid.initialized ? "true" : "false") + ",";
  json += "\"tagPresent\":" + String(rfid.tagPresent ? "true" : "false") + ",";
  json += "\"lastAuthorized\":" + String(rfid.lastAuthorized ? "true" : "false") + ",";
  json += "\"lastTag\":\"" + jsonEscape(rfid.lastTag) + "\",";
  json += "\"lastEvent\":\"" + jsonEscape(rfid.lastEvent) + "\",";
  json += "\"tagCount\":" + String(rfid.tagCount) + ",";
  json += "\"addModeActive\":" + String(rfid.addModeActive ? "true" : "false") + ",";
  json += "\"addModeRemainingMs\":" + String(rfid.addModeRemainingMs) + ",";
  json += "\"pendingTagCount\":" + String(rfid.pendingTagCount) + ",";
  json += "\"pendingTags\":" + jsonRfidTagArray(pendingTags) + ",";
  json += "\"doorUnlockEnabled\":" + String(rfid.doorUnlockEnabled ? "true" : "false") + ",";
  json += "\"totalReads\":" + String(rfid.totalReads) + ",";
  json += "\"tags\":" + jsonRfidTagArray(tags);
  json += "}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(ok ? 200 : 400, "application/json", json);
}

void handleRelayApi() {
  noteRelayCommandPriority();
  uint8_t channel = 0;
  if (!parseRelayChannel(channel)) {
    return;
  }

  if (!server.hasArg("enabled")) {
    server.send(400, "application/json", "{\"error\":\"missing_enabled\"}");
    return;
  }

  const bool enabled = server.arg("enabled") == "true";
  if (enabled && !relayAutomationChannelEnabled(channel)) {
    sendRelayApiResponse(false);
    return;
  }
  sendRelayApiResponse(relaySet(channel, enabled));
}

void handleRelayInchingApi() {
  noteRelayCommandPriority();
  uint8_t channel = 0;
  if (!parseRelayChannel(channel)) {
    return;
  }

  const uint32_t durationMs = server.hasArg("durationMs") ? server.arg("durationMs").toInt() : 1000;
  if (durationMs < 50 || durationMs > 600000) {
    server.send(400, "application/json", "{\"error\":\"invalid_duration\"}");
    return;
  }

  if (!relayAutomationChannelAllowsPulse(channel)) {
    sendRelayApiResponse(false);
    return;
  }
  sendRelayApiResponse(relayInching(channel, durationMs));
}

void handleRelayManualChannelApi() {
  noteRelayCommandPriority();
  uint8_t channel = 0;
  if (!parseRelayChannel(channel)) {
    return;
  }
  if (!server.hasArg("enabled")) {
    server.send(400, "application/json", "{\"error\":\"missing_enabled\"}");
    return;
  }
  sendRelayApiResponse(relayAutomationSetChannelManualState(channel, server.arg("enabled") == "true"));
}

void handleRelayPulseChannelApi() {
  noteRelayCommandPriority();
  uint8_t channel = 0;
  if (!parseRelayChannel(channel)) {
    return;
  }
  sendRelayApiResponse(relayAutomationPulseChannel(channel));
}

void handleRelayAutomationStatusApi() {
  sendRelayAutomationApiResponse();
}

void handleRelayAutomationModeApi() {
  RelayAutomationDevice device;
  RelayAutomationMode mode;
  if (!relayAutomationParseDevice(formString("device"), device) ||
      !relayAutomationParseMode(formString("mode"), mode)) {
    server.send(400, "application/json", "{\"error\":\"invalid_mode_request\"}");
    return;
  }

  sendRelayAutomationApiResponse(relayAutomationSetMode(device, mode));
}

void handleRelayAutomationManualApi() {
  RelayAutomationDevice device;
  if (!relayAutomationParseDevice(formString("device"), device)) {
    server.send(400, "application/json", "{\"error\":\"invalid_device\"}");
    return;
  }

  sendRelayAutomationApiResponse(relayAutomationSetManualState(device, formBool("enabled")));
}

void handleRelayAutomationToggleApi() {
  RelayAutomationDevice device;
  if (!relayAutomationParseDevice(formString("device"), device)) {
    server.send(400, "application/json", "{\"error\":\"invalid_device\"}");
    return;
  }

  sendRelayAutomationApiResponse(relayAutomationToggleManualState(device));
}

void handleRelayAutomationDoorPulseApi() {
  sendRelayAutomationApiResponse(relayAutomationPulseDoorLock());
}

void handleRelayAutomationGaragePulseApi() {
  sendRelayAutomationApiResponse(relayAutomationPulseGarageLock());
}

void handleRelayAutomationSaveApi() {
  sendRelayAutomationApiResponse(relayAutomationSaveSettings());
}

void handleRelayAutomationLoadApi() {
  const bool ok = relayAutomationLoadSettings();
  sendRelayAutomationApiResponse(ok);
}

void handleRs485TestSendApi() {
  if (!requireAuth()) {
    return;
  }
  String text = formString("text", "RS485 demo");
  text = text.substring(0, 96);
  const bool ok = rs485SendText(text);
  server.sendHeader("Cache-Control", "no-store");
  server.send(ok ? 200 : 400, "application/json", String("{\"ok\":") + (ok ? "true" : "false") + "}");
}

void handleCanTestSendApi() {
  if (!requireAuth()) {
    return;
  }
  CANBusFrame frame;
  frame.id = static_cast<uint32_t>(boundedFormInt("id", 0x123, 0, 0x7FF));
  frame.extended = false;
  frame.length = static_cast<uint8_t>(boundedFormInt("length", 0, 0, 8));
  for (uint8_t i = 0; i < frame.length; i++) {
    const String field = "byte" + String(i);
    frame.data[i] = static_cast<uint8_t>(boundedFormInt(field.c_str(), 0, 0, 255));
  }
  const bool ok = canBusSend(frame);
  server.sendHeader("Cache-Control", "no-store");
  server.send(ok ? 200 : 400, "application/json", String("{\"ok\":") + (ok ? "true" : "false") + "}");
}

void handleMp3PlayApi() {
  sendMp3ApiResponse(mp3Play());
}

void handleMp3PlayFileApi() {
  const uint8_t track =
    static_cast<uint8_t>(boundedFormInt("track", MP3_SOUND_MIN_TRACK, MP3_SOUND_MIN_TRACK, MP3_SOUND_MAX_TRACK));
  sendMp3ApiResponse(mp3PlayFile(MP3_SOUND_FOLDER, track));
}

void handleMp3NextApi() {
  sendMp3ApiResponse(mp3Next());
}

void handleMp3PreviousApi() {
  sendMp3ApiResponse(mp3Previous());
}

void handleMp3PauseApi() {
  sendMp3ApiResponse(mp3Pause());
}

void handleMp3StopApi() {
  sendMp3ApiResponse(mp3Stop());
}

void handleMp3VolumeApi() {
  mp3SoundSettings.volume =
    static_cast<uint8_t>(boundedFormInt("volume", mp3SoundSettings.volume, 0, MP3_SOUND_MAX_VOLUME));
  sanitizeMp3SoundSettings(mp3SoundSettings);
  const bool ok = mp3SetVolume(mp3SoundSettings.volume);
  sendMp3ApiResponse(ok);
}

void handleFm225OpenApi() {
  FM225::openPort();
  sendFm225ApiResponse();
}

void handleFm225CloseApi() {
  FM225::closePort();
  sendFm225ApiResponse();
}

void handleFm225ResetApi() {
  FM225::reset();
  sendFm225ApiResponse();
}

void handleFm225StatusApi() {
  FM225::getStatus();
  sendFm225ApiResponse();
}

void handleFm225VersionApi() {
  FM225::getVersion();
  sendFm225ApiResponse();
}

void handleFm225SerialApi() {
  FM225::getSerialNumber();
  sendFm225ApiResponse();
}

void handleFm225VerifyApi() {
  const uint8_t timeoutSec = constrain(formInt("timeoutSec", 10), 1, 60);
  fm225VerifyPending = true;
  fm225VerifySummary = "Verifying face...";
  fm225LastRecognizedUserId = 0;
  fm225LastRecognizedName = "";
  fm225LastVerifyResult = 0xFF;
  fm225LastStatus = "Verifying";
  setFm225Event("Verifying face...");
  FM225::verify(timeoutSec, formBool("powerDown"));
  sendFm225ApiResponse();
}

void handleFm225CancelApi() {
  FM225::cancel();
  sendFm225ApiResponse();
}

void handleFm225ListUsersApi() {
  FM225::listUsers();
  sendFm225ApiResponse();
}

void handleFm225GetUserApi() {
  const int userId = formInt("userId", -1);
  if (userId < 0 || userId > 65535) {
    server.send(400, "application/json", "{\"error\":\"invalid_user_id\"}");
    return;
  }

  FM225::getUserInfo(static_cast<uint16_t>(userId));
  sendFm225ApiResponse();
}

void handleFm225EnrollDynamicApi() {
  String name = formString("name", "User");
  if (name.length() == 0) {
    name = "User";
  }

  FM225::enrollDynamic(name);
  sendFm225ApiResponse();
}

void handleFm225EnrollSingleApi() {
  String name = formString("name", "User");
  if (name.length() == 0) {
    name = "User";
  }

  const uint8_t timeoutSec = constrain(formInt("timeoutSec", 15), 1, 60);
  FM225::enrollSingle(name, formBool("admin"), timeoutSec);
  sendFm225ApiResponse();
}

void handleFm225EnrollDirectionalApi() {
  String name = formString("name", "User");
  if (name.length() == 0) {
    name = "User";
  }

  const uint8_t direction = constrain(formInt("direction", static_cast<int>(FM225::FaceDirection::Middle)), 0, 31);
  const uint8_t timeoutSec = constrain(formInt("timeoutSec", 15), 1, 60);
  FM225::enrollDirectional(name, static_cast<FM225::FaceDirection>(direction), formBool("admin"), timeoutSec);
  sendFm225ApiResponse();
}

void handleFm225EnrollIntegratedApi() {
  String name = formString("name", "User");
  if (name.length() == 0) {
    name = "User";
  }

  const uint8_t direction = constrain(formInt("direction", 0x1F), 0, 31);
  const uint8_t enrollType = constrain(formInt("enrollType", 0), 0, 1);
  const uint8_t duplicateMode = constrain(formInt("duplicateMode", 1), 0, 1);
  const uint8_t timeoutSec = constrain(formInt("timeoutSec", 15), 1, 60);
  FM225::enrollIntegrated(name,
                          static_cast<FM225::FaceDirection>(direction),
                          enrollType == 0 ? FM225::EnrollType::Interactive : FM225::EnrollType::Single,
                          duplicateMode,
                          formBool("admin"),
                          timeoutSec);
  sendFm225ApiResponse();
}

void handleFm225DeleteUserApi() {
  const int userId = formInt("userId", -1);
  if (userId < 0 || userId > 65535) {
    server.send(400, "application/json", "{\"error\":\"invalid_user_id\"}");
    return;
  }

  FM225::deleteUser(static_cast<uint16_t>(userId));
  sendFm225ApiResponse();
}

void handleFm225DeleteAllApi() {
  FM225::deleteAllUsers();
  sendFm225ApiResponse();
}

void handleFm225DemoApi() {
  FM225::setDemoMode(formBool("enabled"));
  sendFm225ApiResponse();
}

void handleFm225ReadUsbApi() {
  FM225::readUsbUvcParameters();
  sendFm225ApiResponse();
}

void handleFm225SetUsbApi() {
  const uint8_t usbType = formInt("usbType", 0x20) == 0x11 ? 0x11 : 0x20;
  const uint8_t jpegQuality = constrain(formInt("jpegQuality", 80), 10, 99);
  FM225::setUsbUvcParameters(usbType, formBool("rotate180"), formBool("mirror"), jpegQuality);
  sendFm225ApiResponse();
}

void handleFm225UpgradeApi() {
  FM225::upgradeFirmware();
  sendFm225ApiResponse();
}

bool parseHexKey(const String &hex, uint8_t key[16]) {
  String clean = hex;
  clean.replace(" ", "");
  clean.replace(":", "");
  clean.replace("-", "");
  if (clean.length() != 32) {
    return false;
  }

  for (uint8_t i = 0; i < 16; i++) {
    const char high = clean[i * 2];
    const char low = clean[i * 2 + 1];
    if (!isxdigit(high) || !isxdigit(low)) {
      return false;
    }
    key[i] = static_cast<uint8_t>(strtoul(clean.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16));
  }
  return true;
}

void handleFm225InitEncryptionApi() {
  const uint32_t seed = static_cast<uint32_t>(formInt("seed", 0x12345678));
  const uint8_t mode = constrain(formInt("mode", 0), 0, 255);
  FM225::initEncryption(seed, mode);
  sendFm225ApiResponse();
}

void handleFm225ReleaseKeyApi() {
  uint8_t key[16];
  if (!parseHexKey(formString("key"), key)) {
    server.send(400, "application/json", "{\"error\":\"invalid_key\"}");
    return;
  }

  FM225::setReleaseEncryptionKey(key);
  sendFm225ApiResponse();
}

void handleFm225DebugKeyApi() {
  uint8_t key[16];
  if (!parseHexKey(formString("key"), key)) {
    server.send(400, "application/json", "{\"error\":\"invalid_key\"}");
    return;
  }

  FM225::setDebugEncryptionKey(key);
  sendFm225ApiResponse();
}

String rfidFormTagOrLast() {
  String tag = formString("tag");
  if (tag.length() == 0) {
    tag = rdmGetSnapshot().lastTag;
  }
  return rdmNormalizeTag(tag);
}

void handleRfidAddApi() {
  const String tag = rfidFormTagOrLast();
  if (tag.length() != 10) {
    server.send(400, "application/json", "{\"error\":\"invalid_tag\"}");
    return;
  }

  sendRfidApiResponse(rdmAddTag(tag));
}

void handleRfidStartAddModeApi() {
  rdmEnterAddMode();
  sendRfidApiResponse();
}

void handleRfidCancelAddModeApi() {
  rdmCancelAddMode();
  sendRfidApiResponse();
}

void handleRfidSavePendingApi() {
  sendRfidApiResponse(rdmSavePendingTags());
}

void handleRfidDeleteApi() {
  const String tag = rfidFormTagOrLast();
  if (tag.length() != 10) {
    server.send(400, "application/json", "{\"error\":\"invalid_tag\"}");
    return;
  }

  sendRfidApiResponse(rdmDeleteTag(tag));
}

void handleRfidClearApi() {
  rdmClearTags();
  sendRfidApiResponse();
}

void handleWebStorageSeedApi() {
  if (!requireAuth()) {
    return;
  }
  const bool ok = seedWebStorageFromGeneratedAssets();
  String json = "{";
  json += "\"ok\":" + String(ok ? "true" : "false") + ",";
  json += "\"ready\":" + String(webStorageReady() ? "true" : "false") + ",";
  json += "\"fileCount\":" + String(webStorageFileCount()) + ",";
  json += "\"lastEvent\":\"" + jsonEscape(webStorageLastEvent) + "\"";
  json += "}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(ok ? 200 : 500, "application/json", json);
}

void handleRestartApi() {
  if (!requireAuth()) {
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", "{\"ok\":true,\"message\":\"restarting\"}");
  delay(150);
  ESP.restart();
}

void startOtaService() {
  if (otaServiceStarted || !networkSettings.otaEnabled || WiFi.status() != WL_CONNECTED) {
    return;
  }

  ArduinoOTA.setHostname(networkSettings.mdnsHostname);
  ArduinoOTA
    .onStart([]() {
      Serial.println("ArduinoOTA update started");
    })
    .onEnd([]() {
      Serial.println("ArduinoOTA update finished");
    })
    .onError([](ota_error_t error) {
      Serial.print("ArduinoOTA error ");
      Serial.println(static_cast<int>(error));
    });
  ArduinoOTA.begin();
  otaServiceStarted = true;
  Serial.print("ArduinoOTA enabled on ");
  Serial.print(networkSettings.mdnsHostname);
  Serial.println(".local");
}

void startFallbackAp() {
  if (fallbackApStarted) {
    return;
  }

  WiFi.mode(String(networkSettings.ssid).length() > 0 ? WIFI_AP_STA : WIFI_AP);
  fallbackApStarted = WiFi.softAP(HA_FALLBACK_AP_SSID);
  if (fallbackApStarted) {
    Serial.print("Fallback WiFi AP started: ");
    Serial.print(HA_FALLBACK_AP_SSID);
    Serial.print(" at http://");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("Fallback WiFi AP failed to start");
  }
}

void beginWiFiConnect() {
  if (String(networkSettings.ssid).length() == 0) {
    Serial.println("WiFi SSID is empty. Starting fallback AP.");
    startFallbackAp();
    return;
  }

  WiFi.mode(fallbackApStarted ? WIFI_AP_STA : WIFI_STA);
  WiFi.begin(networkSettings.ssid, networkSettings.password);
  wifiConnectActive = true;
  wifiConnectStartedMs = millis();
  wifiLastRetryMs = wifiConnectStartedMs;

  Serial.print("Connecting to WiFi SSID ");
  Serial.println(networkSettings.ssid);
}

void serviceNetwork() {
  const uint32_t now = millis();

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnectActive = false;
    if (!mdnsServiceStarted && MDNS.begin(networkSettings.mdnsHostname)) {
      mdnsServiceStarted = true;
      Serial.print("mDNS responder started: ");
      Serial.print(networkSettings.mdnsHostname);
      Serial.println(".local");
      configTime(5 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    }
    startOtaService();
    if (otaServiceStarted) {
      ArduinoOTA.handle();
    }
    return;
  }

  if (wifiConnectActive && now - wifiConnectStartedMs >= WIFI_CONNECT_WINDOW_MS) {
    wifiConnectActive = false;
    Serial.println("WiFi connection window expired. Continuing without blocking.");
    startFallbackAp();
  }

  if (!wifiConnectActive && String(networkSettings.ssid).length() > 0 &&
      now - wifiLastRetryMs >= WIFI_RETRY_INTERVAL_MS) {
    beginWiFiConnect();
  }
}

void connectWiFi() {
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  beginWiFiConnect();
}

String currentIpString() {
  if (WiFi.status() == WL_CONNECTED) {
    return WiFi.localIP().toString();
  }
  if (fallbackApStarted) {
    return WiFi.softAPIP().toString();
  }
  return "0.0.0.0";
}

void registerRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/index.html", HTTP_GET, handleRoot);
  server.on("/styles.css", HTTP_GET, handleStaticFile);
  server.on("/app.js", HTTP_GET, handleStaticFile);
  server.on("/techpanda.png", HTTP_GET, handleStaticFile);
  server.on("/api/login", HTTP_POST, handleLoginApi);
  server.on("/api/status", HTTP_GET, handleStatusApi);
  server.on("/api/settings", HTTP_GET, handleGetSettingsApi);
  server.on("/api/settings", HTTP_POST, handlePostSettingsApi);
  server.on("/api/logs", HTTP_GET, handleEventLogsApi);
  server.on("/api/restart", HTTP_POST, handleRestartApi);
  server.on("/api/web-storage/seed", HTTP_POST, handleWebStorageSeedApi);
  server.on("/api/relay", HTTP_POST, handleRelayApi);
  server.on("/api/relay/inching", HTTP_POST, handleRelayInchingApi);
  server.on("/api/relay/itching", HTTP_POST, handleRelayInchingApi);
  server.on("/api/relay/manual", HTTP_POST, handleRelayManualChannelApi);
  server.on("/api/relay/pulse", HTTP_POST, handleRelayPulseChannelApi);
  server.on("/api/automation/status", HTTP_GET, handleRelayAutomationStatusApi);
  server.on("/api/automation/mode", HTTP_POST, handleRelayAutomationModeApi);
  server.on("/api/automation/manual", HTTP_POST, handleRelayAutomationManualApi);
  server.on("/api/automation/toggle", HTTP_POST, handleRelayAutomationToggleApi);
  server.on("/api/automation/door/pulse", HTTP_POST, handleRelayAutomationDoorPulseApi);
  server.on("/api/automation/garage/pulse", HTTP_POST, handleRelayAutomationGaragePulseApi);
  server.on("/api/automation/save", HTTP_POST, handleRelayAutomationSaveApi);
  server.on("/api/automation/load", HTTP_POST, handleRelayAutomationLoadApi);
  server.on("/api/rs485/test-send", HTTP_POST, handleRs485TestSendApi);
  server.on("/api/can/test-send", HTTP_POST, handleCanTestSendApi);
  server.on("/api/mp3/play", HTTP_POST, handleMp3PlayApi);
  server.on("/api/mp3/file", HTTP_POST, handleMp3PlayFileApi);
  server.on("/api/mp3/next", HTTP_POST, handleMp3NextApi);
  server.on("/api/mp3/previous", HTTP_POST, handleMp3PreviousApi);
  server.on("/api/mp3/pause", HTTP_POST, handleMp3PauseApi);
  server.on("/api/mp3/stop", HTTP_POST, handleMp3StopApi);
  server.on("/api/mp3/volume", HTTP_POST, handleMp3VolumeApi);
  server.on("/api/fm225/open", HTTP_POST, handleFm225OpenApi);
  server.on("/api/fm225/close", HTTP_POST, handleFm225CloseApi);
  server.on("/api/fm225/reset", HTTP_POST, handleFm225ResetApi);
  server.on("/api/fm225/status", HTTP_POST, handleFm225StatusApi);
  server.on("/api/fm225/version", HTTP_POST, handleFm225VersionApi);
  server.on("/api/fm225/serial", HTTP_POST, handleFm225SerialApi);
  server.on("/api/fm225/verify", HTTP_POST, handleFm225VerifyApi);
  server.on("/api/fm225/cancel", HTTP_POST, handleFm225CancelApi);
  server.on("/api/fm225/list-users", HTTP_POST, handleFm225ListUsersApi);
  server.on("/api/fm225/get-user", HTTP_POST, handleFm225GetUserApi);
  server.on("/api/fm225/enroll-dynamic", HTTP_POST, handleFm225EnrollDynamicApi);
  server.on("/api/fm225/enroll-single", HTTP_POST, handleFm225EnrollSingleApi);
  server.on("/api/fm225/enroll-directional", HTTP_POST, handleFm225EnrollDirectionalApi);
  server.on("/api/fm225/enroll-integrated", HTTP_POST, handleFm225EnrollIntegratedApi);
  server.on("/api/fm225/delete-user", HTTP_POST, handleFm225DeleteUserApi);
  server.on("/api/fm225/delete-all", HTTP_POST, handleFm225DeleteAllApi);
  server.on("/api/fm225/demo", HTTP_POST, handleFm225DemoApi);
  server.on("/api/fm225/usb-read", HTTP_POST, handleFm225ReadUsbApi);
  server.on("/api/fm225/usb-set", HTTP_POST, handleFm225SetUsbApi);
  server.on("/api/fm225/upgrade", HTTP_POST, handleFm225UpgradeApi);
  server.on("/api/fm225/encryption/init", HTTP_POST, handleFm225InitEncryptionApi);
  server.on("/api/fm225/encryption/release-key", HTTP_POST, handleFm225ReleaseKeyApi);
  server.on("/api/fm225/encryption/debug-key", HTTP_POST, handleFm225DebugKeyApi);
  server.on("/api/rfid/add", HTTP_POST, handleRfidAddApi);
  server.on("/api/rfid/add-mode/start", HTTP_POST, handleRfidStartAddModeApi);
  server.on("/api/rfid/add-mode/cancel", HTTP_POST, handleRfidCancelAddModeApi);
  server.on("/api/rfid/add-mode/save", HTTP_POST, handleRfidSavePendingApi);
  server.on("/api/rfid/delete", HTTP_POST, handleRfidDeleteApi);
  server.on("/api/rfid/clear", HTTP_POST, handleRfidClearApi);
  server.onNotFound(handleStaticFile);
}
}

void webServerBegin() {
  FM225::setLogCallback(handleFm225Log);
  FM225::setFaceRecognizedCallback(handleFm225Recognized);
  FM225::setVerificationFailedCallback(handleFm225VerifyFailed);
  FM225::setEnrollResultCallback(handleFm225EnrollResult);
  FM225::setStatusCallback(handleFm225Status);
  FM225::setUserInfoCallback(handleFm225UserInfo);
  FM225::setUserListCallback(handleFm225UserList);
  FM225::setFaceStateCallback(handleFm225FaceState);
  FM225::setUsbUvcCallback(handleFm225UsbUvc);
  FM225::setImageCallback(handleFm225Image);

  loadFm225RadarSettings();
  loadTdsMonitorSettings();
  loadAirPurifierSettings();
  loadCommunicationSettings();
  applyCommunicationSettings();
  loadInverterSettings();
  loadMp3SoundSettings();
  loadNetworkSettings();
  loadSecuritySettings();
  loadLogSettings();
  ensureWebStorageSeeded();
  mp3SetVolume(mp3SoundSettings.volume);
  if (mp3SoundSettings.startupSoundEnabled) {
    mp3PlayFile(MP3_SOUND_FOLDER, mp3SoundSettings.startupTrack);
  }
  tdsSnapshot.address = String(tdsSettings.address);
  tdsSnapshot.enabled = tdsSettings.enabled;
  if (!tdsSettings.enabled) {
    updateTdsDisabledSnapshot();
  }
  airPurifierSnapshot.address = String(airPurifierSettings.address);
  airPurifierSnapshot.enabled = airPurifierSettings.enabled;
  if (!airPurifierSettings.enabled) {
    updateAirPurifierDisabledSnapshot();
  }
  connectWiFi();

  registerRoutes();
  const char *headerKeys[] = {"Cookie"};
  server.collectHeaders(headerKeys, 1);
  server.begin();
  Serial.println("HTTP server started");
}

void webServerLoop() {
  serviceNetwork();

  server.handleClient();
  serviceFm225RadarPresence();
  serviceMp3SmokeAlarm();
  serviceEventLogging();
  pollExternalMonitor();
}
