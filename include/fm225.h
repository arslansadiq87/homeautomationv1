#ifndef FM225_H
#define FM225_H

#include <Arduino.h>
#include <vector>

#include "pinout.h"

namespace FM225 {

constexpr uint32_t DEFAULT_BAUD = 115200;
constexpr size_t USER_NAME_SIZE = 32;
constexpr size_t PHOTO_CHUNK_SIZE = 900;

enum class FaceDirection : uint8_t {
  Undefined = 0x00,
  Up = 0x01,
  Down = 0x02,
  Left = 0x04,
  Right = 0x08,
  Middle = 0x10,
};

enum class EnrollType : uint8_t {
  Interactive = 0,
  Single = 1,
};

struct FaceState {
  int16_t state = -1;
  int16_t left = 0;
  int16_t top = 0;
  int16_t right = 0;
  int16_t bottom = 0;
  int16_t yaw = 0;
  int16_t pitch = 0;
  int16_t roll = 0;
  uint32_t updatedAtMs = 0;
};

struct UsbUvcParameters {
  uint8_t usbType = 0;
  bool rotate180 = false;
  bool mirror = false;
  uint8_t jpegQuality = 0;
  uint32_t updatedAtMs = 0;
};

struct ImageInfo {
  uint32_t packetCount = 0;
  uint32_t jpegCount = 0;
  uint16_t lastPayloadSize = 0;
  uint32_t lastPacketAtMs = 0;
  bool lastPacketWasJpeg = false;
  String lastHeaderHex;
};

using LogCallback = void (*)(const String &message);
using FaceRecognizedCallback = void (*)(uint16_t userId, const String &name);
using ResultCallback = void (*)(uint8_t result);
using StatusCallback = void (*)(const String &status);
using UserInfoCallback = void (*)(uint16_t userId, const String &name);
using UserListCallback = void (*)(const std::vector<uint16_t> &userIds);
using FaceStateCallback = void (*)(const FaceState &state);
using UsbUvcCallback = void (*)(const UsbUvcParameters &parameters);
using ImageCallback = void (*)(const ImageInfo &info);

void begin(uint32_t baud = DEFAULT_BAUD,
           int rxPin = PIN_FM225_RX,
           int txPin = PIN_FM225_TX);
void begin(uint32_t baud, int rxPin, int txPin, HardwareSerial &serial);
void loop();

void setLogCallback(LogCallback callback);
void setFaceRecognizedCallback(FaceRecognizedCallback callback);
void setVerificationFailedCallback(ResultCallback callback);
void setEnrollResultCallback(ResultCallback callback);
void setStatusCallback(StatusCallback callback);
void setUserInfoCallback(UserInfoCallback callback);
void setUserListCallback(UserListCallback callback);
void setFaceStateCallback(FaceStateCallback callback);
void setUsbUvcCallback(UsbUvcCallback callback);
void setImageCallback(ImageCallback callback);

String sanitize(const String &value);
String resultText(uint8_t result);
String faceStateText(int16_t state);

void sendPacket(const uint8_t *packet, size_t size);
void sendCommand(uint8_t command, const std::vector<uint8_t> &data = std::vector<uint8_t>());
void openPort();
void closePort();
void reset();
void listUsers();
void verify(uint8_t timeoutSec = 10, bool powerDownRightAway = false);
void cancel();
void getVersion();
void getStatus();
void getSerialNumber();
void getUserInfo(uint16_t userId);
void enrollDirectional(const String &name, FaceDirection direction, bool admin = false, uint8_t timeoutSec = 15);
void enrollSingle(const String &name, bool admin = false, uint8_t timeoutSec = 15);
void enrollIntegrated(const String &name,
                      FaceDirection direction,
                      EnrollType enrollType,
                      uint8_t enableDuplicate,
                      bool admin = false,
                      uint8_t timeoutSec = 15);
void enrollDynamic(const String &name);
void deleteUser(uint16_t userId);
void deleteAllUsers();
void initEncryption(uint32_t seed, uint8_t mode);
void setReleaseEncryptionKey(const uint8_t key[16]);
void setDebugEncryptionKey(const uint8_t key[16]);
void readUsbUvcParameters();
void setUsbUvcParameters(uint8_t usbType, bool rotate180, bool mirror, uint8_t jpegQuality);
void upgradeFirmware();
void enrollWithPhotoStart(uint32_t photoSize);
void enrollWithPhotoChunk(uint16_t sequence, const uint8_t *data, size_t size);
void setDemoMode(bool enabled);

const std::vector<uint16_t> &getLastUserIds();
String getFetchedUserName(uint16_t userId);
const FaceState &getLastFaceState();
const UsbUvcParameters &getLastUsbUvcParameters();
const ImageInfo &getImageInfo();
const std::vector<uint8_t> &getLastJpegImage();
uint8_t getLastNoteId();
String getLastNoteText();
String getLastVersion();
String getLastSerialNumber();
String getLastDeviceId();

}  // namespace FM225

void fm225_begin(uint32_t baud = FM225::DEFAULT_BAUD);
void fm225_loop();
void cmd_open_port();
void cmd_close_port();
void cmd_reset();
void cmd_list_users();
void cmd_verify();
void cmd_cancel();
void cmd_get_version();
void cmd_get_status();
void cmd_get_sn();
void cmd_get_userinfo(uint16_t userId);
void cmd_enroll_dynamic(const String &name);
void cmd_enroll_directional(const String &name, uint8_t direction, bool admin, uint8_t timeoutSec);
void cmd_enroll_single(const String &name, bool admin, uint8_t timeoutSec);
void cmd_enroll_integrated(const String &name,
                           uint8_t direction,
                           uint8_t enrollType,
                           uint8_t duplicateMode,
                           bool admin,
                           uint8_t timeoutSec);
void cmd_delete_user(uint16_t userId);
void cmd_delete_all_users();
void cmd_demo_mode(bool enabled);
void cmd_read_usb_uvc();
void cmd_set_usb_uvc(uint8_t usbType, bool rotate180, bool mirror, uint8_t jpegQuality);
void cmd_upgrade_fw();
void cmd_init_encryption(uint32_t seed, uint8_t mode);
void cmd_set_release_enc_key(const uint8_t key[16]);
void cmd_set_debug_enc_key(const uint8_t key[16]);

#endif
