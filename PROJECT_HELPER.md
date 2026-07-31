# Project Helper

## Communication Demo Additions

Added files:
- `include/RS485Manager.h`
- `src/RS485Manager.cpp`
- `include/CANBusManager.h`
- `src/CANBusManager.cpp`

Startup integration:
- `src/main.cpp` initializes RS485 and CAN once during startup with disabled defaults.
- `src/web.cpp` loads persisted communication settings from Winbond storage and applies them without stopping the rest of the project if a bus fails.
- The Settings tab now includes a `Communication` sub-tab with simple RS485 and CAN controls.

## Final GPIO Assignments

RS485 / MAX3485:
- RX / RO: GPIO15
- TX / DI: GPIO18
- RE# and DE tied together: GPIO48
- GPIO48 LOW: receive mode
- GPIO48 HIGH: transmit mode
- Startup and idle state: receive mode

CAN / SN65HVD232 using ESP32 TWAI:
- CAN TX: GPIO5
- CAN RX: GPIO6
- Classical CAN only
- No MCP2515 or UART-based CAN path is used

Detected GPIO conflicts:
- No conflict was found for RS485 GPIO15, GPIO18, or GPIO48 in the existing `include/pinout.h` assignments.
- GPIO5 and GPIO6 were selected for CAN because they were unused in the current project pin assignments.

## Connector Mappings

RS485 connector:
- Pin 1 = A
- Pin 2 = B
- Pin 3 = GND

CAN connector:
- Pin 1 = CANH
- Pin 2 = CANL
- Pin 3 = GND

## Defaults

RS485:
- Default baud rate: 9600
- Default enabled state: disabled

CAN:
- Default bitrate: 500000 bit/s
- Default enabled state: disabled

Communication settings are persisted in the same Winbond-backed settings storage style used by the rest of the web settings. If storage is unavailable or the stored record is invalid, compiled defaults are used.

## Basic Module Usage

RS485:
- `rs485Begin(enabled, baudRate)` initializes the interface once.
- `rs485SetEnabled(enabled)` enables or disables the port.
- `rs485SetBaudRate(baudRate)` changes the baud rate.
- `rs485Send(...)` / `rs485SendText(...)` send raw bytes/text.
- `rs485Receive(...)` reads currently available bytes without blocking.
- `rs485GetStatus()` returns simple ready/error state.

CAN:
- `canBusBegin(enabled, bitrate)` initializes the TWAI setup once.
- `canBusSetEnabled(enabled)` starts or stops TWAI.
- `canBusSetBitrate(bitrate)` changes bitrate.
- `canBusSend(frame)` sends one Classical CAN frame with up to 8 data bytes.
- `canBusReceive(frame)` polls for one received frame without blocking.
- `canBusGetStatus()` returns simple ready/error state.

## Protocol Scope

No higher-level RS485 or CAN protocol is implemented. This demo intentionally does not include Modbus, CANopen, OBD-II, J1939, ISO-TP, device-specific protocol handling, advanced queues, recovery systems, or detailed statistics.
