# Agent Instructions

## Project Overview

This is an ESP32 Arduino firmware project built with PlatformIO for the `esp32dev` board. The firmware provides Wi-Fi setup and fallback AP recovery, HTTPS status/setup pages, JSON/form APIs, an optional SSD1306 OLED status display, optional ADC1 power sensing, heartbeat LED behavior, regional time/date settings, and on-demand BLE support through NimBLE.

Use `README.md` for operator-facing setup notes and `memory-bank/` for durable project context.

## Read First

Before changing firmware lifecycle or broad behavior, read:

- `platformio.ini`
- `include/AppConfig.h`
- `src/main.cpp`
- `include/Application.h`
- `src/Application.cpp`

Then read the subsystem pair that owns the requested behavior:

- Wi-Fi, fallback AP, static IP, saved credentials: `include/Connectivity.h`, `src/Connectivity.cpp`
- HTTPS/HTTP dashboard, setup page, APIs: `include/StatusServer.h`, `src/StatusServer.cpp`
- OLED display: `include/StatusDisplay.h`, `src/StatusDisplay.cpp`
- BLE scanning/connection: `include/BluetoothManager.h`, `src/BluetoothManager.cpp`
- Regional timezone/date formatting: `include/RegionalSettings.h`, `src/RegionalSettings.cpp`
- Power sensing: `include/PowerMonitor.h`, `src/PowerMonitor.cpp`
- Heartbeat LED: `include/Heartbeat.h`, `src/Heartbeat.cpp`

## Architecture Notes

- Keep `src/main.cpp` thin: `setup()` calls `setupApplication()`, and `loop()` calls `runApplication()`.
- Treat `Application` as the subsystem orchestration layer.
- Keep behavior inside the existing subsystem module when possible.
- Prefer focused edits over broad refactors.
- Be careful when editing `src/StatusServer.cpp`; it is large because it embeds the browser UI and route handlers.
- Update `memory-bank/` after meaningful architecture, workflow, or project-direction changes.

## Configuration And Secrets

- Build-time settings are loaded by `scripts/load_build_secrets.py`.
- Local Wi-Fi and power-sense values may come from `.env.local` or shell environment variables.
- Do not print, inspect, or commit local secret values unless the user explicitly asks and understands the risk.
- Keep these generated/local files out of source control:
  - `.env.local` and other `.env.*` files except `.env.example`
  - `certs/*.pem`
  - `include/generated/*.h`
  - `.pio/`
  - local SSH key files such as `id_ed25519_ai_agent`
- Self-signed TLS assets are generated with `scripts/generate_self_signed_cert.sh`.

## Common Commands

Run commands from the project root:

```bash
pio run
pio run --target upload
pio device monitor
./scripts/generate_self_signed_cert.sh
```

Because builds can include local credentials through compiler defines, avoid pasting full compiler command lines, generated cert/key contents, or secret-derived output into chat.

## Validation Checklist

For code changes, prefer this order when practical:

1. Run `pio run`.
2. Confirm firmware size still fits the configured `no_ota.csv` partition scheme.
3. If networking/server behavior changed, check route registration in `src/StatusServer.cpp`.
4. If Wi-Fi changed, reason through fallback AP recovery.
5. If BLE changed, preserve the on-demand/release pattern so HTTPS stays responsive.
6. If display changed, check the SSD1306 I2C assumptions and 16-column U8x8 layout.
7. If behavior changed meaningfully, update the relevant `memory-bank/` file.

## Hardware Assumptions

- Target board: `esp32dev`
- Framework: Arduino
- Serial monitor speed: `115200`
- OLED: optional SSD1306 128x64 I2C display
- OLED pins: SDA `GPIO21`, SCL `GPIO22`
- OLED addresses scanned: `0x3C`, `0x3D`
- Heartbeat LED: `LED_BUILTIN` if available, otherwise GPIO `2`
- Optional power sensing must use an ADC1-capable pin such as GPIO `32`, `33`, `34`, `35`, `36`, or `39`

## Runtime URLs

- HTTPS dashboard: `/`
- HTTPS setup page: `/setup`
- Status API: `/api/status`
- Health check: `/healthz`
- HTTP bootstrap/recovery page: port `80`
- Preferred station URL when mDNS is available: `https://esp32-status.local/`
- Fallback AP direct URL: `https://192.168.4.1/`
