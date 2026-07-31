# Home Automation Hub

ESP32-S3 home automation firmware built with PlatformIO and Arduino. The project provides a local web dashboard for sensors, relays, RFID access, face verification, radar presence detection, MP3 alerts, inverter monitoring, and persistent settings stored on an external Winbond W25Q128 flash chip.

## Current Status

- Target board: ESP32-S3 DevKitC-1
- Framework: Arduino through PlatformIO
- Web dashboard: embedded HTTP server on port 80
- Web asset storage: raw dashboard files served from external W25Q128 SPI flash
- External storage: W25Q128 SPI flash for persistent settings, event logs, RFID tags, and dashboard assets
- Network behavior: non-blocking startup with fallback WiFi access point
- Secret handling: local credentials are kept in `include/secrets_local.h`, which is ignored by Git

## Features

- Web dashboard for live status and configuration
- Relay control with manual, inching, and automation modes
- Door lock and garage lock pulse control
- Outdoor light automation based on BH1750 lux readings
- Exhaust fan automation based on SHT3x temperature readings
- Motion light automation from two motion sensors
- MQ135 smoke/gas alarm trigger with optional MP3 alert
- DS18B20 water/temperature probe support
- RDM6300 RFID tag authorization and add mode
- HLK-LD2410B radar target detection
- HLK-FM225 face verification, enrollment, user management, and radar-triggered verification
- TDS monitor polling from an external HTTP endpoint
- SolaX inverter polling
- Nitrox/Deye Solarman TCP inverter polling
- Growatt OpenAPI polling
- Event logging with configurable categories
- OTA support when enabled and WiFi is connected
- mDNS hostname support

## Non-Blocking Design

The firmware is designed to keep running even if modules are missing, damaged, disconnected, or external APIs are offline.

- WiFi does not block boot. The web server starts immediately.
- If WiFi credentials are empty or the station connection window expires, the device starts a fallback AP.
- External API polling uses short timeouts and failure backoff.
- TDS and inverter polling failure does not stop the main loop.
- MP3 commands are queued instead of using fixed command delays.
- SHT3x reads use a request/read state machine instead of blocking conversion delay.
- DS18B20 conversion is asynchronous.
- Missing Winbond flash causes storage operations to fail cleanly while the firmware continues.
- Missing MCP23017 relay expander keeps all relay outputs OFF and retries detection in the background.

Some Arduino networking calls are still synchronous internally, but their timeout windows are intentionally short and failures are backed off.

## Hardware Pinout

Pin definitions live in `include/pinout.h`.

| Function | GPIO |
| --- | ---: |
| I2C SDA | 8 |
| I2C SCL | 9 |
| Passive buzzer | 16 |
| MQ135 digital output | 14 |
| MQ135 analog output | 4 |
| DS18B20 OneWire | 13 |
| Motion sensor 1 | 11 |
| Motion sensor 2 | 10 |
| Door reed switch | 7 |
| Garage reed switch | 42 |
| W25Q128 CS | 21 |
| W25Q128 SCK | 36 |
| W25Q128 MISO | 39 |
| W25Q128 MOSI | 35 |
| MP3 RX | 47 |
| MP3 TX | 45 |
| LD2410B radar RX | 40 |
| LD2410B radar TX | 41 |
| FM225 RX | 38 |
| FM225 TX | 37 |
| RDM6300 RX | 17 |

I2C addresses:

| Device | Address |
| --- | ---: |
| BH1750 light sensor | `0x23` |
| SHT3x temperature/humidity sensor | `0x44` |
| MCP23017 relay expander | `0x20` |

The MCP23017 address is fixed at `0x20` (A0, A1, and A2 connected to GND).

## Project Layout

```text
data/                  Web dashboard source files packed into firmware and seeded to Winbond
include/               Header files, pinout, and secret defaults
lib/                   PlatformIO library folder
src/                   Firmware source
test/                  PlatformIO test folder
platformio.ini         PlatformIO board and dependency config
README.md              Project documentation
```

Important source files:

| File | Purpose |
| --- | --- |
| `src/main.cpp` | Firmware setup and loop orchestration |
| `src/modules.cpp` | Sensors, relays, buzzer, I2C, SPI storage |
| `src/web.cpp` | Web server, dashboard API, settings, network, inverter/TDS polling |
| `src/relay_automation.cpp` | Relay automation rules and saved settings |
| `src/rdm.cpp` | RDM6300 RFID reader and tag storage |
| `src/radar.cpp` | HLK-LD2410B parser and commands |
| `src/fm225.cpp` | HLK-FM225 protocol and face operations |
| `src/mp3.cpp` | Serial MP3 module command queue |

## Requirements

- VS Code with PlatformIO extension, or PlatformIO Core installed
- ESP32-S3 DevKitC-1 compatible board
- USB cable for upload and serial monitor
- Optional connected modules listed above

Install PlatformIO Core if needed:

```bash
pip install platformio
```

## Secrets And Local Configuration

Do not put real WiFi passwords, API tokens, inverter passwords, or private keys in tracked files.

The firmware reads optional local defaults from:

```text
include/secrets_local.h
```

That file is ignored by Git. A safe template is tracked at:

```text
include/secrets_local.example.h
```

To configure a new clone:

1. Copy `include/secrets_local.example.h` to `include/secrets_local.h`.
2. Edit `include/secrets_local.h` with local WiFi and API defaults.
3. Build and upload.

Example:

```cpp
#define HA_DEFAULT_WIFI_SSID "your-wifi-ssid"
#define HA_DEFAULT_WIFI_PASSWORD "your-wifi-password"
#define HA_DEFAULT_MDNS_HOSTNAME "home-automation"
#define HA_FALLBACK_AP_SSID "HomeAutomation-Setup"

#define HA_DEFAULT_SOLAX_ENABLED false
#define HA_DEFAULT_SOLAX_ADDRESS "http://solax.local/"
#define HA_DEFAULT_SOLAX_PASSWORD "your-solax-password"

#define HA_DEFAULT_NITROX_ENABLED false
#define HA_DEFAULT_NITROX_HOST "nitrox.local"

#define HA_DEFAULT_GROWATT_ENABLED false
#define HA_DEFAULT_GROWATT_TOKEN "your-growatt-token"
```

If `include/secrets_local.h` is missing, the project still builds with empty/generic defaults.

## Build

Build firmware:

```bash
pio run
```

After changing PlatformIO versions or pulling UART/build fixes, clean and rebuild:

```bash
pio run -t clean
pio run
```

The project pins pioarduino `platform-espressif32` `54.03.20` so different machines use the same Arduino-ESP32 3.2.0 based PlatformIO package instead of whatever the latest registry version is.

Upload firmware:

```bash
pio run --target upload
```

Open serial monitor:

```bash
pio device monitor -b 115200
```

## First Boot And Dashboard Access

On boot, the firmware starts the HTTP server immediately.

If WiFi connects:

- Dashboard is available at the printed local IP address.
- If mDNS starts, it is also available at `http://home-automation.local/` or your configured hostname.

If WiFi does not connect:

- The device starts fallback AP `HomeAutomation-Setup`.
- Connect to that AP and open the fallback AP IP printed in serial monitor.
- The default AP IP is usually `192.168.4.1`.

From the dashboard settings page, configure:

- WiFi SSID and password
- mDNS hostname
- OTA enable flag
- Login authentication
- Relay automation thresholds and durations
- TDS endpoint
- SolaX, Nitrox, and Growatt monitoring
- FM225 radar-triggered verification
- RFID door unlock behavior
- Event log categories

## Dashboard Files And Web Storage

The dashboard files are stored in `data/`:

- `index.html`
- `styles.css`
- `app.js`
- `techpanda.png`

Normal development flow:

```bash
pio run
pio run --target upload
```

At build time, `scripts/generate_web_assets.py` packs the files in `data/` into a generated firmware header. After flashing firmware, seed those packed dashboard assets into the external Winbond flash from the dashboard Settings page or through:

```text
POST /api/web-storage/seed
```

The HTTP server serves dashboard files directly from raw Winbond records. If Winbond web storage is missing or unseeded, the dashboard returns a seed instruction instead of falling back to an ESP32 filesystem.

## Persistent Storage

The project uses W25Q128 flash near the end of the chip address space for settings and logs.

Stored data includes:

- Relay automation settings
- RFID tags and RFID options
- FM225 radar settings
- TDS monitor settings
- Inverter monitor settings
- MP3 sound settings
- Network settings
- Security/login settings
- Event log settings
- Event log records
- Optional dashboard web storage cache

If the storage IC is not detected, the firmware continues with defaults and reports storage offline in the dashboard.

## Security Notes

- `include/secrets_local.h` is ignored by Git and should remain private.
- `include/secrets_local.example.h` contains placeholders only.
- Web login is disabled by default in code. Enable and set a strong username/password from the dashboard before exposing the device to an untrusted network.
- Do not expose the device directly to the public internet.
- OTA only starts when enabled and WiFi is connected.

## API Overview

The dashboard uses local HTTP APIs. Most commands are POST requests.

Common endpoints:

| Endpoint | Method | Purpose |
| --- | --- | --- |
| `/api/login` | POST | Login when authentication is enabled |
| `/api/status` | GET | Full dashboard status snapshot |
| `/api/settings` | GET | Current configuration |
| `/api/settings` | POST | Save configuration |
| `/api/logs` | GET | Event logs |
| `/api/restart` | POST | Restart ESP32 |
| `/api/web-storage/seed` | POST | Copy firmware-packed dashboard assets into Winbond storage |
| `/api/relay` | POST | Set relay state |
| `/api/relay/inching` | POST | Pulse relay for a duration |
| `/api/automation/status` | GET | Relay automation status |
| `/api/automation/mode` | POST | Set automation/manual mode |
| `/api/automation/manual` | POST | Set manual automation device state |
| `/api/mp3/play` | POST | Play default MP3 |
| `/api/mp3/file` | POST | Play selected MP3 file |
| `/api/fm225/*` | POST | FM225 face module commands |
| `/api/rfid/*` | POST | RFID tag management |

## Module Notes

### Relays

The relay expander uses an MCP23017 over I2C with all 16 pins configured as active-high outputs. Channels 0-7 map to GPA0-GPA7 and channels 8-15 map to GPB0-GPB7. At startup, both output banks are cleared before the pins become outputs so all relays remain OFF. Relay automation maps:

| Device | Relay channel |
| --- | ---: |
| Door lock | 0 |
| Garage lock | 1 |
| Outdoor light | 2 |
| Exhaust fan | 5 |
| Motion light 1 | 6 |
| Motion light 2 | 7 |

### RFID

The RDM6300 reader is RX-only at 9600 baud. Authorized tags are saved in Winbond storage. If legacy Preferences data exists, it can be migrated into Winbond storage.

### FM225 Face Module

The FM225 module supports:

- Port open/close
- Status, version, serial queries
- Verification
- Enrollment
- User listing
- User delete/delete all
- USB UVC parameter read/write
- Encryption key commands
- Firmware upgrade command dispatch

Radar-triggered verification can be enabled from settings. When LD2410B presence meets configured distance/energy thresholds, FM225 verification starts and successful verification can pulse the door lock.

FM225 uses a local `HardwareSerial(0)` instance in `src/fm225.cpp` instead of the `Serial0` global. This keeps ESP32-S3 builds portable across Arduino-ESP32 cores where `Serial0` is not declared, while leaving UART1 for the MP3 module and UART2 for the LD2410B radar.

### Inverter Monitoring

Inverter monitors are disabled by default. Enable only the monitors you use.

- SolaX: HTTP POST to local inverter endpoint
- Nitrox/Deye: Solarman TCP frame with Modbus payload
- Growatt: HTTPS Growatt OpenAPI token

Failed polling marks the source offline and schedules a delayed retry.

## Git And GitHub Notes

Ignored files include:

```text
.pio/
.vscode/
.env
*.env
include/secrets_local.h
include/*_local.h
include/web_assets_generated.h
```

Before committing, verify secrets are ignored:

```bash
git check-ignore -v include/secrets_local.h
git status --short
```

Push changes:

```bash
git add .
git commit -m "Describe change"
git push
```

## Troubleshooting

### Dashboard does not load

Open `/api/web-storage/seed` from the Settings page Seed Web Storage action after flashing new firmware. The board serves dashboard files from raw Winbond storage, so the web storage area must be seeded at least once after changing dashboard assets.

### WiFi does not connect

- Check `include/secrets_local.h` or dashboard network settings.
- Connect to fallback AP `HomeAutomation-Setup`.
- Reconfigure WiFi from the dashboard.

### Storage shows offline

- Check W25Q128 wiring and power.
- The firmware will keep running without it, but saved settings/logs may not persist.

### Relays do not respond

- Check MCP23017 SDA on GPIO 8, SCL on GPIO 9, and address `0x20` (A0-A2 to GND).
- Confirm the MCP23017, both ULN2803A drivers, relay supply, ESP32, and 5 V supply share a common GND.
- Confirm each ULN2803A COM pin connects to +5 V and each MCP23017 output has its 100 kOhm pull-down to GND.

### External API data is missing

- Confirm the monitor is enabled in settings.
- Confirm device IP/host/token/password.
- Offline API sources are retried with backoff, so the dashboard should stay responsive.

## Development Checklist

Before pushing changes:

```bash
pio run
git status --short
git check-ignore -v include/secrets_local.h
```

Use `include/secrets_local.example.h` for documentation and placeholders. Keep real secrets only in ignored local files.
