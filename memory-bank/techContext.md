# Technical Context

## PlatformIO Configuration

Source: `platformio.ini`

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
board_build.partitions = no_ota.csv
monitor_speed = 115200
extra_scripts = pre:scripts/load_build_secrets.py
lib_deps =
  olikraus/U8g2 @ ^2.36.5
  h2zero/NimBLE-Arduino @ ^2.5.0
```

## Toolchain

- PlatformIO project
- ESP32 / Espressif32 platform
- Arduino framework
- Serial monitor: `115200` baud
- Partition scheme: `no_ota.csv`

## Libraries

- `olikraus/U8g2 @ ^2.36.5`
  - Likely used for display/status UI.
- `h2zero/NimBLE-Arduino @ ^2.5.0`
  - Likely used for BLE functionality.

## Build Hooks

- `extra_scripts = pre:scripts/load_build_secrets.py`
  - Runs before build.
  - Future changes should inspect this script before altering build flags, generated headers, or secret/config workflows.

## Repository Context

- Current workspace: `test-esp32`
- Git remote shown by workspace metadata: `git@github.com-ai-agent:ai-ml-kiosk/esp32_wroom.git`
- Latest commit at task start: `2e1681dc172f431cf5c3baea3b016a127c640235`
