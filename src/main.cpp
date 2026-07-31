#include <Arduino.h>
#include <time.h>

#include "CANBusManager.h"
#include "RS485Manager.h"
#include "fm225.h"
#include "mp3.h"
#include "modules.h"
#include "radar.h"
#include "rdm.h"
#include "relay_automation.h"
#include "web.h"

namespace {
constexpr uint32_t STARTUP_TIME_DISPLAY_MS = 5000;
}  // namespace

void setup() {
  Serial.begin(115200);

  modulesBegin();
  mp3Begin();
  radar_begin();
  fm225_begin();
  rdmBegin();
  relayAutomationBegin();
  rs485Begin(false, 9600);
  canBusBegin(false, 500000);
  webServerBegin();
}

void loop() {
  modulesLoop();
  mp3Loop();
  radar_loop();
  fm225_loop();
  rdmLoop();
  relayAutomationLoop();
  rs485Loop();
  canBusLoop();

  // 7-segment display removed

  webServerLoop();
}
