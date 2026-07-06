#include "fm225.h"

#include <map>

namespace FM225 {

namespace {

constexpr uint8_t SYNC_0 = 0xEF;
constexpr uint8_t SYNC_1 = 0xAA;
constexpr uint8_t MID_REPLY = 0x00;
constexpr uint8_t MID_NOTE = 0x01;
constexpr uint8_t MID_IMAGE = 0x02;

constexpr uint8_t CMD_RESET = 0x10;
constexpr uint8_t CMD_GET_STATUS = 0x11;
constexpr uint8_t CMD_VERIFY = 0x12;
constexpr uint8_t CMD_ENROLL = 0x13;
constexpr uint8_t CMD_ENROLL_SINGLE = 0x1D;
constexpr uint8_t CMD_DELETE_USER = 0x20;
constexpr uint8_t CMD_DELETE_ALL = 0x21;
constexpr uint8_t CMD_GET_USER_INFO = 0x22;
constexpr uint8_t CMD_FACE_RESET = 0x23;
constexpr uint8_t CMD_GET_ALL_USER_ID = 0x24;
constexpr uint8_t CMD_ENROLL_ITG = 0x26;
constexpr uint8_t CMD_GET_VERSION = 0x30;
constexpr uint8_t CMD_INIT_ENCRYPTION = 0x50;
constexpr uint8_t CMD_SET_RELEASE_ENC_KEY = 0x52;
constexpr uint8_t CMD_SET_DEBUG_ENC_KEY = 0x53;
constexpr uint8_t CMD_GET_SN = 0x93;
constexpr uint8_t CMD_READ_USB_UVC = 0xB0;
constexpr uint8_t CMD_SET_USB_UVC = 0xB1;
constexpr uint8_t CMD_UPGRADE_FW = 0xF6;
constexpr uint8_t CMD_ENROLL_WITH_PHOTO = 0xF7;
constexpr uint8_t CMD_DEMO_MODE = 0xFE;

constexpr uint8_t NID_READY = 0x00;
constexpr uint8_t NID_FACE_STATE = 0x01;
constexpr uint8_t NID_UNKNOWN_ERROR = 0x02;
constexpr uint8_t NID_OTA_DONE = 0x03;
constexpr uint8_t NID_EYE_STATE = 0x04;

// Use a local UART0 instance instead of the optional Serial0 global; some
// Arduino-ESP32/PlatformIO combinations for ESP32-S3 do not declare Serial0.
// UART1 is used by the MP3 module and UART2 is used by the LD2410B radar.
HardwareSerial fm225Serial(0);
HardwareSerial *fmSerial = &fm225Serial;
std::vector<uint8_t> rxBuf;
std::vector<uint16_t> lastUserIds;
std::map<uint16_t, String> fetchedIdToName;
bool awaitingList = false;

FaceState lastFaceState;
UsbUvcParameters lastUsbUvcParameters;
ImageInfo imageInfo;
std::vector<uint8_t> lastJpegImage;
uint8_t lastNoteId = 0xFF;
String lastNoteText;
String lastVersion;
String lastSerialNumber;
String lastDeviceId;

LogCallback logCallback = nullptr;
FaceRecognizedCallback faceRecognizedCallback = nullptr;
ResultCallback verificationFailedCallback = nullptr;
ResultCallback enrollResultCallback = nullptr;
StatusCallback statusCallback = nullptr;
UserInfoCallback userInfoCallback = nullptr;
UserListCallback userListCallback = nullptr;
FaceStateCallback faceStateCallback = nullptr;
UsbUvcCallback usbUvcCallback = nullptr;
ImageCallback imageCallback = nullptr;

void addLog(const String &message) {
  if (logCallback) {
    logCallback(message);
    return;
  }

  Serial.println(message);
}

uint8_t xorChecksum(const uint8_t *packet, size_t size) {
  uint8_t checksum = 0;
  for (size_t i = 2; i < size; ++i) {
    checksum ^= packet[i];
  }
  return checksum;
}

bool packetChecksumOk(const std::vector<uint8_t> &packet) {
  if (packet.size() < 6) return false;

  uint8_t checksum = 0;
  for (size_t i = 2; i + 1 < packet.size(); ++i) {
    checksum ^= packet[i];
  }
  return checksum == packet.back();
}

uint16_t readU16BE(const std::vector<uint8_t> &data, size_t pos) {
  if (pos + 1 >= data.size()) return 0;
  return (uint16_t(data[pos]) << 8) | uint16_t(data[pos + 1]);
}

int16_t readI16LE(const std::vector<uint8_t> &data, size_t pos) {
  if (pos + 1 >= data.size()) return 0;
  return int16_t(uint16_t(data[pos]) | (uint16_t(data[pos + 1]) << 8));
}

void appendU16BE(std::vector<uint8_t> &data, uint16_t value) {
  data.push_back((value >> 8) & 0xFF);
  data.push_back(value & 0xFF);
}

void appendU32BE(std::vector<uint8_t> &data, uint32_t value) {
  data.push_back((value >> 24) & 0xFF);
  data.push_back((value >> 16) & 0xFF);
  data.push_back((value >> 8) & 0xFF);
  data.push_back(value & 0xFF);
}

String normalizeName(const String &name) {
  String clean = sanitize(name);
  clean.trim();
  if (clean.length() >= USER_NAME_SIZE) {
    clean = clean.substring(0, USER_NAME_SIZE - 1);
  }
  return clean;
}

void appendName(std::vector<uint8_t> &data, const String &name) {
  String clean = normalizeName(name);
  size_t start = data.size();
  data.insert(data.end(), USER_NAME_SIZE, 0);
  for (size_t i = 0; i < clean.length(); ++i) {
    data[start + i] = uint8_t(clean[i]);
  }
}

String readPacketString(const std::vector<uint8_t> &packet, size_t start, size_t end) {
  String out;
  for (size_t i = start; i < end && i < packet.size(); ++i) {
    if (packet[i] == 0) break;
    out += char(packet[i]);
  }
  return sanitize(out);
}

String readFixedString(const std::vector<uint8_t> &packet, size_t start, size_t maxLen) {
  return readPacketString(packet, start, start + maxLen);
}

String bytesToHex(const uint8_t *data, size_t size, size_t maxBytes = 16) {
  String out;
  size_t count = size < maxBytes ? size : maxBytes;
  out.reserve(count * 3);
  for (size_t i = 0; i < count; ++i) {
    if (i) out += ' ';
    if (data[i] < 0x10) out += '0';
    out += String(data[i], HEX);
  }
  out.toUpperCase();
  return out;
}

String statusText(uint8_t status) {
  switch (status) {
    case 0:
      return "IDLE";
    case 1:
      return "BUSY";
    case 2:
      return "ERROR";
    case 3:
      return "INVALID";
    default:
      return "UNKNOWN " + String(status);
  }
}

String commandName(uint8_t command) {
  switch (command) {
    case CMD_RESET:
      return "RESET";
    case CMD_GET_STATUS:
      return "GET_STATUS";
    case CMD_VERIFY:
      return "VERIFY";
    case CMD_ENROLL:
      return "ENROLL";
    case CMD_ENROLL_SINGLE:
      return "ENROLL_SINGLE";
    case CMD_DELETE_USER:
      return "DELETE_USER";
    case CMD_DELETE_ALL:
      return "DELETE_ALL";
    case CMD_GET_USER_INFO:
      return "GET_USER_INFO";
    case CMD_FACE_RESET:
      return "FACE_RESET";
    case CMD_GET_ALL_USER_ID:
      return "GET_ALL_USER_ID";
    case CMD_ENROLL_ITG:
      return "ENROLL_ITG";
    case CMD_GET_VERSION:
      return "GET_VERSION";
    case CMD_INIT_ENCRYPTION:
      return "INIT_ENCRYPTION";
    case CMD_SET_RELEASE_ENC_KEY:
      return "SET_RELEASE_ENC_KEY";
    case CMD_SET_DEBUG_ENC_KEY:
      return "SET_DEBUG_ENC_KEY";
    case CMD_GET_SN:
      return "GET_SN";
    case CMD_READ_USB_UVC:
      return "READ_USB_UVC";
    case CMD_SET_USB_UVC:
      return "SET_USB_UVC";
    case CMD_UPGRADE_FW:
      return "UPGRADE_FW";
    case CMD_ENROLL_WITH_PHOTO:
      return "ENROLL_WITH_PHOTO";
    case CMD_DEMO_MODE:
      return "DEMO_MODE";
    default:
      return String("0x") + String(command, HEX);
  }
}

String directionText(uint8_t directionMask) {
  String out;
  if (directionMask & uint8_t(FaceDirection::Up)) out += "up ";
  if (directionMask & uint8_t(FaceDirection::Down)) out += "down ";
  if (directionMask & uint8_t(FaceDirection::Left)) out += "left ";
  if (directionMask & uint8_t(FaceDirection::Right)) out += "right ";
  if (directionMask & uint8_t(FaceDirection::Middle)) out += "middle ";
  out.trim();
  return out.length() ? out : "none";
}

void handleUsersReply(const std::vector<uint8_t> &packet) {
  if (packet.size() < 8) return;

  uint8_t count = packet[7];
  lastUserIds.clear();
  fetchedIdToName.clear();

  for (int i = 0; i < count; ++i) {
    size_t pos = 8 + 2 * i;
    if (pos + 1 >= packet.size() - 1) break;
    uint16_t id = readU16BE(packet, pos);
    lastUserIds.push_back(id);
    getUserInfo(id);
  }

  awaitingList = false;
  addLog("FM225 users listed: " + String(lastUserIds.size()));
  if (userListCallback) userListCallback(lastUserIds);
}

void handleVerifyReply(uint8_t result, const std::vector<uint8_t> &packet) {
  if (result == 0x00 && packet.size() >= 41) {
    uint16_t userId = readU16BE(packet, 7);
    String name = readFixedString(packet, 9, USER_NAME_SIZE);
    uint8_t admin = packet.size() > 41 ? packet[41] : 0;
    uint8_t unlockStatus = packet.size() > 42 ? packet[42] : 0;
    addLog("Face recognized: ID " + String(userId) + " " + name + ", admin " + String(admin) +
           ", unlock " + String(unlockStatus));
    if (faceRecognizedCallback) faceRecognizedCallback(userId, name);
  } else {
    addLog("Face not recognized: " + resultText(result));
    if (verificationFailedCallback) verificationFailedCallback(result);
  }
}

void handleEnrollReply(uint8_t command, uint8_t result, const std::vector<uint8_t> &packet) {
  if (result == 0x00) {
    String message = String("FM225 ") + commandName(command) + " OK";
    if (packet.size() >= 10) {
      uint16_t userId = readU16BE(packet, 7);
      message += ", user " + String(userId);
      if (packet.size() > 9) message += ", directions " + directionText(packet[9]);
    }
    addLog(message);
  } else {
    addLog(String("FM225 ") + commandName(command) + " failed: " + resultText(result));
  }
  if (enrollResultCallback) enrollResultCallback(result);
}

void handleUserInfoReply(uint8_t result, const std::vector<uint8_t> &packet) {
  if (result != 0) {
    addLog("FM225 user info failed: " + resultText(result));
    return;
  }

  if (packet.size() >= 41) {
    uint16_t userId = readU16BE(packet, 7);
    String name = readFixedString(packet, 9, USER_NAME_SIZE);
    uint8_t admin = packet.size() > 41 ? packet[41] : 0;
    fetchedIdToName[userId] = name;
    addLog("FM225 user: " + String(userId) + " " + name + ", admin " + String(admin));
    if (userInfoCallback) userInfoCallback(userId, name);
  }
}

void handleUsbUvcReply(uint8_t result, const std::vector<uint8_t> &packet) {
  if (result != 0) {
    addLog("FM225 USB UVC read failed: " + resultText(result));
    return;
  }

  if (packet.size() >= 10) {
    uint8_t flags = packet[8];
    lastUsbUvcParameters.usbType = packet[7];
    lastUsbUvcParameters.rotate180 = flags & 0x01;
    lastUsbUvcParameters.mirror = flags & 0x02;
    lastUsbUvcParameters.jpegQuality = packet[9];
    lastUsbUvcParameters.updatedAtMs = millis();
    addLog("FM225 USB UVC: type 0x" + String(lastUsbUvcParameters.usbType, HEX) +
           ", rotate " + String(lastUsbUvcParameters.rotate180 ? "yes" : "no") +
           ", mirror " + String(lastUsbUvcParameters.mirror ? "yes" : "no") +
           ", quality " + String(lastUsbUvcParameters.jpegQuality));
    if (usbUvcCallback) usbUvcCallback(lastUsbUvcParameters);
  }
}

void handleReply(const std::vector<uint8_t> &packet) {
  if (packet.size() < 7) return;

  uint8_t command = packet[5];
  uint8_t result = packet[6];

  switch (command) {
    case CMD_GET_ALL_USER_ID:
      if (result == 0) {
        handleUsersReply(packet);
      } else {
        awaitingList = false;
        addLog("FM225 list users failed: " + resultText(result));
      }
      return;

    case CMD_VERIFY:
      handleVerifyReply(result, packet);
      return;

    case CMD_ENROLL:
    case CMD_ENROLL_SINGLE:
    case CMD_ENROLL_ITG:
      handleEnrollReply(command, result, packet);
      return;

    case CMD_DELETE_USER:
      addLog(result == 0 ? "FM225 user deleted" : "FM225 delete failed: " + resultText(result));
      return;

    case CMD_DELETE_ALL:
      if (result == 0) {
        lastUserIds.clear();
        fetchedIdToName.clear();
        addLog("FM225 all users deleted");
      } else {
        addLog("FM225 delete all failed: " + resultText(result));
      }
      return;

    case CMD_GET_STATUS: {
      String text = packet.size() > 7 && result == 0 ? statusText(packet[7]) : resultText(result);
      addLog("FM225 status: " + text);
      if (statusCallback) statusCallback(text);
      return;
    }

    case CMD_GET_VERSION:
      if (result == 0) {
        lastVersion = readPacketString(packet, 7, packet.size() - 1);
        addLog("FM225 version: " + lastVersion);
      } else {
        addLog("FM225 version request failed: " + resultText(result));
      }
      return;

    case CMD_GET_SN:
      if (result == 0) {
        lastSerialNumber = readFixedString(packet, 7, 32);
        addLog("FM225 serial: " + lastSerialNumber);
      } else {
        addLog("FM225 serial request failed: " + resultText(result));
      }
      return;

    case CMD_GET_USER_INFO:
      handleUserInfoReply(result, packet);
      return;

    case CMD_INIT_ENCRYPTION:
      if (result == 0) {
        lastDeviceId = readFixedString(packet, 7, 20);
        addLog("FM225 encryption initialized, device ID: " + lastDeviceId);
      } else {
        addLog("FM225 encryption init failed: " + resultText(result));
      }
      return;

    case CMD_SET_RELEASE_ENC_KEY:
    case CMD_SET_DEBUG_ENC_KEY:
      addLog(result == 0 ? String("FM225 ") + commandName(command) + " OK"
                         : String("FM225 ") + commandName(command) + " failed: " + resultText(result));
      return;

    case CMD_READ_USB_UVC:
      handleUsbUvcReply(result, packet);
      return;

    case CMD_SET_USB_UVC:
      addLog(result == 0 ? "FM225 USB UVC parameters set"
                         : "FM225 USB UVC set failed: " + resultText(result));
      return;

    case CMD_UPGRADE_FW:
      if (result == 0 && packet.size() > 7) {
        addLog("FM225 firmware upgrade progress: " + String(packet[7]) + "%");
      } else {
        addLog("FM225 firmware upgrade reply: " + resultText(result));
      }
      return;

    case CMD_ENROLL_WITH_PHOTO:
      if (result == 0 && packet.size() >= 9) {
        addLog("FM225 photo enroll ACK seq " + String(readU16BE(packet, 7)));
      } else {
        addLog("FM225 photo enroll failed: " + resultText(result));
      }
      return;

    case CMD_DEMO_MODE:
      addLog(result == 0 ? "FM225 demo mode changed" : "FM225 demo mode failed: " + resultText(result));
      return;

    case CMD_RESET:
    case CMD_FACE_RESET:
      addLog(result == 0 ? String("FM225 ") + commandName(command) + " OK"
                         : String("FM225 ") + commandName(command) + " failed: " + resultText(result));
      return;

    default:
      addLog(String("FM225 reply ") + commandName(command) + ": " + resultText(result));
      return;
  }
}

void handleFaceStateNote(const std::vector<uint8_t> &packet) {
  if (packet.size() < 22) return;

  lastFaceState.state = readI16LE(packet, 6);
  lastFaceState.left = readI16LE(packet, 8);
  lastFaceState.top = readI16LE(packet, 10);
  lastFaceState.right = readI16LE(packet, 12);
  lastFaceState.bottom = readI16LE(packet, 14);
  lastFaceState.yaw = readI16LE(packet, 16);
  lastFaceState.pitch = readI16LE(packet, 18);
  lastFaceState.roll = readI16LE(packet, 20);
  lastFaceState.updatedAtMs = millis();

  addLog("FM225 face state: " + faceStateText(lastFaceState.state) + ", yaw " + String(lastFaceState.yaw) +
         ", pitch " + String(lastFaceState.pitch) + ", roll " + String(lastFaceState.roll));
  if (faceStateCallback) faceStateCallback(lastFaceState);
}

void handleNote(const std::vector<uint8_t> &packet) {
  if (packet.size() < 7) return;

  lastNoteId = packet[5];
  switch (lastNoteId) {
    case NID_READY:
      lastNoteText = "READY";
      addLog("FM225 ready");
      if (statusCallback) statusCallback("READY");
      break;
    case NID_FACE_STATE:
      lastNoteText = "FACE_STATE";
      handleFaceStateNote(packet);
      break;
    case NID_UNKNOWN_ERROR:
      lastNoteText = "UNKNOWN_ERROR";
      addLog("FM225 note: unknown error");
      break;
    case NID_OTA_DONE:
      lastNoteText = "OTA_DONE";
      addLog("FM225 note: OTA done");
      break;
    case NID_EYE_STATE:
      lastNoteText = "EYE_STATE";
      addLog("FM225 note: eye state");
      break;
    default:
      lastNoteText = "UNKNOWN_NOTE_" + String(lastNoteId);
      addLog("FM225 note: " + lastNoteText);
      break;
  }
}

void handleImage(const std::vector<uint8_t> &packet) {
  uint16_t payloadLen = readU16BE(packet, 3);
  size_t payloadStart = 5;
  size_t availablePayload = packet.size() > 6 ? packet.size() - 6 : 0;
  size_t payloadSize = payloadLen < availablePayload ? payloadLen : availablePayload;
  const uint8_t *payload = payloadSize ? &packet[payloadStart] : nullptr;
  bool isJpeg = payloadSize >= 3 && payload[0] == 0xFF && payload[1] == 0xD8 && payload[2] == 0xFF;

  imageInfo.packetCount++;
  imageInfo.lastPayloadSize = payloadLen;
  imageInfo.lastPacketAtMs = millis();
  imageInfo.lastPacketWasJpeg = isJpeg;
  imageInfo.lastHeaderHex = payload ? bytesToHex(payload, payloadSize) : "";

  if (isJpeg) {
    imageInfo.jpegCount++;
    lastJpegImage.assign(payload, payload + payloadSize);
  }

  addLog(String("FM225 image packet #") + imageInfo.packetCount + ": " + String(payloadLen) +
         " bytes, header " + imageInfo.lastHeaderHex + (isJpeg ? ", JPEG cached" : ""));
  if (imageCallback) imageCallback(imageInfo);
}

void handlePacket(const std::vector<uint8_t> &packet) {
  if (packet.size() < 6) return;

  if (!packetChecksumOk(packet)) {
    addLog("FM225 packet checksum mismatch");
    return;
  }

  uint8_t msgId = packet[2];
  switch (msgId) {
    case MID_REPLY:
      handleReply(packet);
      break;
    case MID_NOTE:
      handleNote(packet);
      break;
    case MID_IMAGE:
      handleImage(packet);
      break;
    default:
      addLog("FM225 unknown message id: 0x" + String(msgId, HEX));
      break;
  }
}

}  // namespace

void begin(uint32_t baud, int rxPin, int txPin) {
  begin(baud, rxPin, txPin, fm225Serial);
}

void begin(uint32_t baud, int rxPin, int txPin, HardwareSerial &serial) {
  fmSerial = &serial;
  fmSerial->begin(baud, SERIAL_8N1, rxPin, txPin);
  rxBuf.clear();
  addLog("FM225 UART started on RX GPIO" + String(rxPin) + ", TX GPIO" + String(txPin));
}

void loop() {
  while (fmSerial->available()) {
    rxBuf.push_back((uint8_t)fmSerial->read());
    if (rxBuf.size() > 2048) {
      rxBuf.erase(rxBuf.begin(), rxBuf.begin() + (rxBuf.size() - 1024));
    }
    yield();
  }

  while (rxBuf.size() >= 6) {
    size_t offset = 0;
    while (offset + 1 < rxBuf.size() && (rxBuf[offset] != SYNC_0 || rxBuf[offset + 1] != SYNC_1)) {
      ++offset;
    }
    if (offset) {
      rxBuf.erase(rxBuf.begin(), rxBuf.begin() + offset);
    }

    if (rxBuf.size() < 6) return;

    uint16_t payloadLen = (uint16_t(rxBuf[3]) << 8) | rxBuf[4];
    if (payloadLen > 1024) {
      rxBuf.erase(rxBuf.begin());
      continue;
    }

    size_t packetLen = size_t(payloadLen) + 6;
    if (rxBuf.size() < packetLen) return;

    std::vector<uint8_t> packet(rxBuf.begin(), rxBuf.begin() + packetLen);
    rxBuf.erase(rxBuf.begin(), rxBuf.begin() + packetLen);
    handlePacket(packet);
    yield();
  }
}

void setLogCallback(LogCallback callback) {
  logCallback = callback;
}

void setFaceRecognizedCallback(FaceRecognizedCallback callback) {
  faceRecognizedCallback = callback;
}

void setVerificationFailedCallback(ResultCallback callback) {
  verificationFailedCallback = callback;
}

void setEnrollResultCallback(ResultCallback callback) {
  enrollResultCallback = callback;
}

void setStatusCallback(StatusCallback callback) {
  statusCallback = callback;
}

void setUserInfoCallback(UserInfoCallback callback) {
  userInfoCallback = callback;
}

void setUserListCallback(UserListCallback callback) {
  userListCallback = callback;
}

void setFaceStateCallback(FaceStateCallback callback) {
  faceStateCallback = callback;
}

void setUsbUvcCallback(UsbUvcCallback callback) {
  usbUvcCallback = callback;
}

void setImageCallback(ImageCallback callback) {
  imageCallback = callback;
}

String sanitize(const String &value) {
  String out;
  out.reserve(value.length());
  for (size_t i = 0; i < value.length(); ++i) {
    char c = value[i];
    if (c >= 32 && c <= 126) out += c;
  }
  return out;
}

String resultText(uint8_t result) {
  switch (result) {
    case 0:
      return "SUCCESS";
    case 1:
      return "REJECTED";
    case 2:
      return "ABORTED";
    case 4:
      return "CAMERA_FAILED";
    case 5:
      return "UNKNOWN_REASON";
    case 6:
      return "INVALID_PARAM";
    case 7:
      return "NO_MEMORY";
    case 8:
      return "UNKNOWN_USER";
    case 9:
      return "MAX_USER";
    case 10:
      return "FACE_ENROLLED";
    case 12:
      return "LIVENESS_CHECK_FAILED";
    case 13:
      return "TIMEOUT";
    case 14:
      return "AUTHORIZATION_FAILED";
    case 19:
      return "READ_FILE_FAILED";
    case 20:
      return "WRITE_FILE_FAILED";
    case 21:
      return "NO_ENCRYPT";
    case 23:
      return "NO_RGB_IMAGE";
    case 24:
      return "JPG_PHOTO_LARGE";
    case 25:
      return "JPG_PHOTO_SMALL";
    default:
      return "CODE_" + String(result);
  }
}

String faceStateText(int16_t state) {
  switch (state) {
    case 0:
      return "NORMAL";
    case 1:
      return "NO_FACE";
    case 2:
      return "TOO_UP";
    case 3:
      return "TOO_DOWN";
    case 4:
      return "TOO_LEFT";
    case 5:
      return "TOO_RIGHT";
    case 6:
      return "FAR";
    case 7:
      return "CLOSE";
    case 8:
      return "EYEBROW_OCCLUSION";
    case 9:
      return "EYE_OCCLUSION";
    case 10:
      return "FACE_OCCLUSION";
    case 11:
      return "DIRECTION_ERROR";
    case 12:
      return "OPEN_EYE";
    case 13:
      return "EYE_CLOSED";
    case 14:
      return "EYE_UNKNOWN";
    default:
      return "UNKNOWN_" + String(state);
  }
}

void sendPacket(const uint8_t *packet, size_t size) {
  fmSerial->write(packet, size);
}

void sendCommand(uint8_t command, const std::vector<uint8_t> &data) {
  std::vector<uint8_t> packet;
  packet.reserve(6 + data.size());
  packet.push_back(SYNC_0);
  packet.push_back(SYNC_1);
  packet.push_back(command);
  packet.push_back((data.size() >> 8) & 0xFF);
  packet.push_back(data.size() & 0xFF);
  packet.insert(packet.end(), data.begin(), data.end());
  packet.push_back(xorChecksum(packet.data(), packet.size()));
  sendPacket(packet.data(), packet.size());
}

void openPort() {
  getVersion();
  addLog("FM225 wake/version command sent");
}

void closePort() {
  static const uint8_t packet[] = {0xEF, 0xAA, 0x31, 0x00, 0x00, 0x31};
  sendPacket(packet, sizeof(packet));
  addLog("FM225 close/power-down command sent");
}

void reset() {
  sendCommand(CMD_RESET);
  addLog("FM225 reset sent");
}

void listUsers() {
  sendCommand(CMD_GET_ALL_USER_ID);
  awaitingList = true;
  addLog("FM225 list users sent");
}

void verify(uint8_t timeoutSec, bool powerDownRightAway) {
  std::vector<uint8_t> data;
  data.push_back(powerDownRightAway ? 1 : 0);
  data.push_back(timeoutSec);
  sendCommand(CMD_VERIFY, data);
  addLog("FM225 verify sent, timeout " + String(timeoutSec) + "s");
}

void cancel() {
  sendCommand(CMD_FACE_RESET);
  addLog("FM225 face reset sent");
}

void getVersion() {
  sendCommand(CMD_GET_VERSION);
  addLog("FM225 version requested");
}

void getStatus() {
  sendCommand(CMD_GET_STATUS);
  addLog("FM225 status requested");
}

void getSerialNumber() {
  sendCommand(CMD_GET_SN);
  addLog("FM225 serial number requested");
}

void getUserInfo(uint16_t userId) {
  std::vector<uint8_t> data;
  appendU16BE(data, userId);
  sendCommand(CMD_GET_USER_INFO, data);
  addLog("FM225 user info requested: " + String(userId));
}

void enrollDirectional(const String &name, FaceDirection direction, bool admin, uint8_t timeoutSec) {
  String cleanName = normalizeName(name);
  std::vector<uint8_t> data;
  data.push_back(admin ? 1 : 0);
  appendName(data, cleanName);
  data.push_back(uint8_t(direction));
  data.push_back(timeoutSec);
  sendCommand(CMD_ENROLL, data);
  addLog("FM225 directional enroll started: " + cleanName + ", direction " + directionText(uint8_t(direction)));
}

void enrollSingle(const String &name, bool admin, uint8_t timeoutSec) {
  String cleanName = normalizeName(name);
  std::vector<uint8_t> data;
  data.push_back(admin ? 1 : 0);
  appendName(data, cleanName);
  data.push_back(uint8_t(FaceDirection::Undefined));
  data.push_back(timeoutSec);
  sendCommand(CMD_ENROLL_SINGLE, data);
  addLog("FM225 single enroll started: " + cleanName);
}

void enrollIntegrated(const String &name,
                      FaceDirection direction,
                      EnrollType enrollType,
                      uint8_t enableDuplicate,
                      bool admin,
                      uint8_t timeoutSec) {
  String cleanName = normalizeName(name);
  if (uint8_t(direction) == 0x1F && enrollType == EnrollType::Interactive) {
    std::vector<uint8_t> data;
    data.push_back(0x05);
    appendName(data, cleanName);
    data.push_back(uint8_t(FaceDirection::Middle));
    data.push_back(timeoutSec);
    data.push_back(admin ? 1 : 0);
    data.push_back(0);
    data.push_back(0);
    data.push_back(0);
    data.push_back(5);
    sendCommand(CMD_ENROLL_ITG, data);
    addLog("FM225 multi-direction enroll started: " + cleanName + ", directions 5");
    return;
  }

  std::vector<uint8_t> data;
  data.push_back(admin ? 1 : 0);
  appendName(data, cleanName);
  data.push_back(uint8_t(direction));
  data.push_back(uint8_t(enrollType));
  data.push_back(enableDuplicate);
  data.push_back(timeoutSec);
  data.push_back(0);
  data.push_back(0);
  data.push_back(0);
  sendCommand(CMD_ENROLL_ITG, data);
  addLog("FM225 integrated enroll started: " + cleanName);
}

void enrollDynamic(const String &name) {
  String cleanName = normalizeName(name);
  std::vector<uint8_t> data;
  data.push_back(0x01);
  appendName(data, cleanName);
  data.push_back(uint8_t(FaceDirection::Middle));
  data.push_back(0x01);
  data.push_back(0x01);
  data.push_back(uint8_t(FaceDirection::Up));
  data.push_back(0x00);
  data.push_back(0x00);
  data.push_back(0x00);
  sendCommand(CMD_ENROLL_ITG, data);
  addLog("FM225 enroll started: " + cleanName);
}

void deleteUser(uint16_t userId) {
  std::vector<uint8_t> data;
  appendU16BE(data, userId);
  sendCommand(CMD_DELETE_USER, data);
  addLog("FM225 delete user requested: " + String(userId));
}

void deleteAllUsers() {
  sendCommand(CMD_DELETE_ALL);
  addLog("FM225 delete all users requested");
}

void initEncryption(uint32_t seed, uint8_t mode) {
  std::vector<uint8_t> data;
  appendU32BE(data, seed);
  data.push_back(mode);
  sendCommand(CMD_INIT_ENCRYPTION, data);
  addLog("FM225 encryption init requested");
}

void setReleaseEncryptionKey(const uint8_t key[16]) {
  std::vector<uint8_t> data(key, key + 16);
  sendCommand(CMD_SET_RELEASE_ENC_KEY, data);
  addLog("FM225 release encryption key sent");
}

void setDebugEncryptionKey(const uint8_t key[16]) {
  std::vector<uint8_t> data(key, key + 16);
  sendCommand(CMD_SET_DEBUG_ENC_KEY, data);
  addLog("FM225 debug encryption key sent");
}

void readUsbUvcParameters() {
  sendCommand(CMD_READ_USB_UVC);
  addLog("FM225 USB UVC parameters requested");
}

void setUsbUvcParameters(uint8_t usbType, bool rotate180, bool mirror, uint8_t jpegQuality) {
  std::vector<uint8_t> data;
  uint8_t flags = (rotate180 ? 0x01 : 0x00) | (mirror ? 0x02 : 0x00);
  data.push_back(usbType == 0x11 ? 0x11 : 0x20);
  data.push_back(flags);
  data.push_back((uint8_t)constrain(jpegQuality, 10, 99));
  sendCommand(CMD_SET_USB_UVC, data);
  addLog("FM225 USB UVC parameters sent");
}

void upgradeFirmware() {
  sendCommand(CMD_UPGRADE_FW);
  addLog("FM225 firmware upgrade command sent");
}

void enrollWithPhotoStart(uint32_t photoSize) {
  std::vector<uint8_t> data;
  appendU16BE(data, 0);
  appendU32BE(data, photoSize);
  sendCommand(CMD_ENROLL_WITH_PHOTO, data);
  addLog("FM225 photo enroll start sent, size " + String(photoSize));
}

void enrollWithPhotoChunk(uint16_t sequence, const uint8_t *data, size_t size) {
  std::vector<uint8_t> payload;
  appendU16BE(payload, sequence);
  size_t chunkSize = size > PHOTO_CHUNK_SIZE ? PHOTO_CHUNK_SIZE : size;
  payload.insert(payload.end(), data, data + chunkSize);
  sendCommand(CMD_ENROLL_WITH_PHOTO, payload);
  addLog("FM225 photo enroll chunk sent, seq " + String(sequence) + ", bytes " + String(chunkSize));
}

void setDemoMode(bool enabled) {
  std::vector<uint8_t> data;
  data.push_back(enabled ? 1 : 0);
  sendCommand(CMD_DEMO_MODE, data);
  addLog(String("FM225 demo mode ") + (enabled ? "enable" : "disable") + " requested");
}

const std::vector<uint16_t> &getLastUserIds() {
  return lastUserIds;
}

String getFetchedUserName(uint16_t userId) {
  auto it = fetchedIdToName.find(userId);
  return it == fetchedIdToName.end() ? String() : it->second;
}

const FaceState &getLastFaceState() {
  return lastFaceState;
}

const UsbUvcParameters &getLastUsbUvcParameters() {
  return lastUsbUvcParameters;
}

const ImageInfo &getImageInfo() {
  return imageInfo;
}

const std::vector<uint8_t> &getLastJpegImage() {
  return lastJpegImage;
}

uint8_t getLastNoteId() {
  return lastNoteId;
}

String getLastNoteText() {
  return lastNoteText;
}

String getLastVersion() {
  return lastVersion;
}

String getLastSerialNumber() {
  return lastSerialNumber;
}

String getLastDeviceId() {
  return lastDeviceId;
}

}  // namespace FM225

void fm225_begin(uint32_t baud) {
  FM225::begin(baud);
}

void fm225_loop() {
  FM225::loop();
}

void cmd_open_port() {
  FM225::openPort();
}

void cmd_close_port() {
  FM225::closePort();
}

void cmd_reset() {
  FM225::reset();
}

void cmd_list_users() {
  FM225::listUsers();
}

void cmd_verify() {
  FM225::verify();
}

void cmd_cancel() {
  FM225::cancel();
}

void cmd_get_version() {
  FM225::getVersion();
}

void cmd_get_status() {
  FM225::getStatus();
}

void cmd_get_sn() {
  FM225::getSerialNumber();
}

void cmd_get_userinfo(uint16_t userId) {
  FM225::getUserInfo(userId);
}

void cmd_enroll_dynamic(const String &name) {
  FM225::enrollDynamic(name);
}

void cmd_enroll_directional(const String &name, uint8_t direction, bool admin, uint8_t timeoutSec) {
  FM225::enrollDirectional(name, FM225::FaceDirection(direction), admin, timeoutSec);
}

void cmd_enroll_single(const String &name, bool admin, uint8_t timeoutSec) {
  FM225::enrollSingle(name, admin, timeoutSec);
}

void cmd_enroll_integrated(const String &name,
                           uint8_t direction,
                           uint8_t enrollType,
                           uint8_t duplicateMode,
                           bool admin,
                           uint8_t timeoutSec) {
  FM225::enrollIntegrated(name,
                          FM225::FaceDirection(direction),
                          enrollType == 0 ? FM225::EnrollType::Interactive : FM225::EnrollType::Single,
                          duplicateMode,
                          admin,
                          timeoutSec);
}

void cmd_delete_user(uint16_t userId) {
  FM225::deleteUser(userId);
}

void cmd_delete_all_users() {
  FM225::deleteAllUsers();
}

void cmd_demo_mode(bool enabled) {
  FM225::setDemoMode(enabled);
}

void cmd_read_usb_uvc() {
  FM225::readUsbUvcParameters();
}

void cmd_set_usb_uvc(uint8_t usbType, bool rotate180, bool mirror, uint8_t jpegQuality) {
  FM225::setUsbUvcParameters(usbType, rotate180, mirror, jpegQuality);
}

void cmd_upgrade_fw() {
  FM225::upgradeFirmware();
}

void cmd_init_encryption(uint32_t seed, uint8_t mode) {
  FM225::initEncryption(seed, mode);
}

void cmd_set_release_enc_key(const uint8_t key[16]) {
  FM225::setReleaseEncryptionKey(key);
}

void cmd_set_debug_enc_key(const uint8_t key[16]) {
  FM225::setDebugEncryptionKey(key);
}
