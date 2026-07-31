#include "CANBusManager.h"

#include <driver/twai.h>

#include "pinout.h"

namespace {
CANBusStatus status;
bool driverInstalled = false;
bool driverStarted = false;

twai_timing_config_t timingConfig(uint32_t bitrate) {
  switch (bitrate) {
    case 125000:
      return TWAI_TIMING_CONFIG_125KBITS();
    case 250000:
      return TWAI_TIMING_CONFIG_250KBITS();
    case 1000000:
      return TWAI_TIMING_CONFIG_1MBITS();
    case 500000:
    default:
      return TWAI_TIMING_CONFIG_500KBITS();
  }
}

void stopDriver() {
  if (driverStarted) {
    twai_stop();
    driverStarted = false;
  }
  if (driverInstalled) {
    twai_driver_uninstall();
    driverInstalled = false;
  }
  status.ready = false;
}

bool startDriver() {
  stopDriver();

  twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(
    static_cast<gpio_num_t>(PIN_CAN_TX),
    static_cast<gpio_num_t>(PIN_CAN_RX),
    TWAI_MODE_NORMAL);
  general.tx_queue_len = 3;
  general.rx_queue_len = 3;

  twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  twai_timing_config_t timing = timingConfig(status.bitrate);

  esp_err_t result = twai_driver_install(&general, &timing, &filter);
  if (result != ESP_OK) {
    status.error = true;
    status.lastError = "TWAI install failed";
    return false;
  }
  driverInstalled = true;

  result = twai_start();
  if (result != ESP_OK) {
    status.error = true;
    status.lastError = "TWAI start failed";
    stopDriver();
    return false;
  }

  driverStarted = true;
  status.ready = true;
  status.error = false;
  status.lastError = "";
  return true;
}
}

void canBusBegin(bool enabled, uint32_t bitrate) {
  pinMode(PIN_CAN_TX, INPUT);
  status.enabled = enabled;
  status.bitrate = bitrate > 0 ? bitrate : 500000;
  status.ready = false;
  status.error = false;
  status.lastError = enabled ? "CAN not started" : "CAN disabled";
  if (enabled) {
    startDriver();
  }
}

void canBusLoop() {
}

bool canBusSetEnabled(bool enabled) {
  status.enabled = enabled;
  if (!enabled) {
    stopDriver();
    pinMode(PIN_CAN_TX, INPUT);
    status.error = false;
    status.lastError = "CAN disabled";
    return true;
  }
  return startDriver();
}

bool canBusSetBitrate(uint32_t bitrate) {
  if (bitrate != 125000 && bitrate != 250000 && bitrate != 500000 && bitrate != 1000000) {
    status.error = true;
    status.lastError = "Invalid CAN bitrate";
    return false;
  }
  status.bitrate = bitrate;
  if (!status.enabled) {
    return true;
  }
  return startDriver();
}

CANBusStatus canBusGetStatus() {
  return status;
}

bool canBusSend(const CANBusFrame &frame) {
  if (!status.enabled || !driverStarted || frame.length > 8) {
    return false;
  }

  twai_message_t message = {};
  message.identifier = frame.id;
  message.extd = frame.extended ? 1 : 0;
  message.data_length_code = frame.length;
  for (uint8_t i = 0; i < frame.length; i++) {
    message.data[i] = frame.data[i];
  }

  return twai_transmit(&message, 0) == ESP_OK;
}

bool canBusReceive(CANBusFrame &frame) {
  if (!status.enabled || !driverStarted) {
    return false;
  }

  twai_message_t message = {};
  if (twai_receive(&message, 0) != ESP_OK) {
    return false;
  }

  frame.id = message.identifier;
  frame.extended = message.extd != 0;
  frame.length = min<uint8_t>(message.data_length_code, 8);
  for (uint8_t i = 0; i < frame.length; i++) {
    frame.data[i] = message.data[i];
  }
  return true;
}
