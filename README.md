# ESP32 Arduino Project

Professional ESP32 starter project using the Arduino framework and a modular C++ layout.

## Features

- Serial initialization at `115200`
- Wi-Fi connection helper with build-time secrets from `.env.local` or exported environment variables
- Non-blocking heartbeat LED using `millis()`
- I2C OLED status display support for a 4-pin screen
- OLED header icons plus Wi-Fi-synced time and date
- Built-in HTTPS status dashboard on `/`
- JSON status endpoint on `/api/status`
- Self-signed TLS certificate for local development
- HTTP port 80 bootstrap page for HTTPS onboarding
- Manual SPI SD card reading with a browser file explorer, download API, and safe eject
- Fixed station IP support for the normal Wi-Fi network
- Fallback access point if Wi-Fi connection fails
- Separated application, connectivity, and heartbeat modules

## Structure

- `platformio.ini`: PlatformIO project configuration
- `include/AppConfig.h`: connectivity and timing constants
- `include/Connectivity.h`: Wi-Fi interface
- `include/Heartbeat.h`: heartbeat LED interface
- `include/StatusDisplay.h`: local OLED status display interface
- `include/StatusServer.h`: HTTP dashboard interface
- `include/ManageSDCard.h`: SPI SD card detection and file access interface
- `include/generated/*.h`: locally generated embedded certificate/key headers
- `include/Application.h`: setup and loop orchestration
- `certs/`: local self-signed certificate and key
- `docs/`: reference screenshots for the browser UI
- `.env.example`: sample local environment-variable file for Wi-Fi secrets
- `scripts/load_build_secrets.py`: loads selected PlatformIO build settings from `.env.local` and exported environment variables
- `scripts/generate_self_signed_cert.sh`: regenerates the local TLS assets
- `src/*.cpp`: implementation files

## Getting Started

1. Create a local `.env.local` file based on `.env.example` and set your Wi-Fi values there.
2. Adjust the timezone or NTP servers in `include/AppConfig.h` if you want the OLED time/date to use a different locale.
3. Generate local TLS assets with `./scripts/generate_self_signed_cert.sh`.
4. Build the project with `pio run`.
5. Flash the board with `pio run --target upload`.
6. Open the serial monitor with `pio device monitor`.
7. Open the printed HTTPS dashboard URL in a browser to view live board status.

Exporting the same variables in your shell is still supported, and shell values override `.env.local` during the build.

## OLED Wiring

This firmware assumes a common 4-pin I2C OLED, typically an `SSD1306` `128x64` module.

- Display `GND` -> ESP32 `GND`
- Display `VDD` -> ESP32 `3V3`
- Display `SCK` -> ESP32 `GPIO22` (I2C clock / `SCL`)
- Display `SDA` -> ESP32 `GPIO21` (I2C data / `SDA`)

Notes:
- On many small OLEDs, the pin labeled `SCK` really means the I2C clock line, not SPI.
- The ESP32 GPIOs are `3.3V`, so `3V3` is the safe default supply unless your module documentation explicitly says otherwise.
- The code scans the common OLED I2C addresses `0x3C` and `0x3D`.
- If the screen stays blank, the module may use a different controller such as `SH1106`; this implementation currently defaults to `SSD1306`.

## SD Card Wiring

This firmware assumes a common SPI microSD card reader on the ESP32 VSPI pins by default. The firmware does not touch the SD card at boot; open `/sd` and choose `Read Card` when you want to mount and browse it. Choose `Eject Card` before removing the card; eject unmounts the SD filesystem, releases the SPI bus, and leaves the page ready for a later `Read Card` remount.

- Reader `GND` -> ESP32 `GND`
- Reader `VCC` -> ESP32 `3V3`
- Reader `CS` -> ESP32 `GPIO5`
- Reader `SCK` -> ESP32 `GPIO18`
- Reader `MISO` -> ESP32 `GPIO19`
- Reader `MOSI` -> ESP32 `GPIO23`

The pins can be overridden in `.env.local` using:

- `ESP32_SD_CARD_CS_PIN`
- `ESP32_SD_CARD_SCK_PIN`
- `ESP32_SD_CARD_MISO_PIN`
- `ESP32_SD_CARD_MOSI_PIN`
- `ESP32_SD_CARD_SPI_FREQUENCY_HZ`
- `ESP32_SD_CARD_ENABLED=false` to disable SD card management

## Wi-Fi Fallback

If the ESP32 cannot join your normal Wi-Fi at boot, it now:

- Waits briefly for power to stabilize before bringing up Wi-Fi
- Starts a fallback access point immediately
- Shows the fallback SSID on the OLED
- Keeps retrying your configured Wi-Fi in the background every few seconds
- Turns the fallback AP back off after station Wi-Fi has been healthy for a short time

If you see `192.168.4.1`, you must first join the fallback SSID shown on the display or serial log before that address will be reachable from your phone or laptop.

## Fixed Station IP

The normal Wi-Fi client interface can now use a fixed IP address instead of DHCP.

- Static-IP settings live in `include/AppConfig.h`
- Set `kUseStaticStationIp` to `true` for a fixed IP or `false` for DHCP / dynamic IP
- The current defaults are `192.168.1.176` with gateway `192.168.1.1`
- Fallback AP mode still uses `192.168.4.1`
- The dashboard API and OLED now show whether the board is using `Fixed`, `Dynamic`, or `AP local`
- The HTTPS dashboard now includes a toggle switch that saves the chosen mode and reconnects Wi-Fi to apply it

Choose a fixed IP that is free on your LAN or reserved for the ESP32 in your router, otherwise you may hit an IP conflict.

## HTTPS Dashboard

- `/`: live board status dashboard over HTTPS
- `/sd`: SD card management page and file explorer
- `/api/status`: machine-readable JSON status
- `/api/sd/status`: SD card status JSON
- `/api/sd/list?path=/`: SD card directory listing JSON
- `/api/sd/download?path=/file.txt`: SD card file download
- `/api/sd/mount`: SD card read/mount endpoint, using `POST`
- `/api/sd/eject`: SD card safe-eject endpoint, using `POST`
- `/healthz`: simple text health check

## Browser UI Preview

These screenshots are cropped to the app window only and use representative board data so the layout stays readable in the documentation. Your SSID, IP address, uptime, scan results, and signal values will vary on a real device.

### HTTPS status page

Main live status page served over HTTPS on `/`:

![HTTPS status page preview](docs/https-status-page.png)

### HTTPS setup page

Configuration page served over HTTPS on `/setup` for Wi-Fi, IP mode, regional settings, and Bluetooth controls:

![HTTPS setup page preview](docs/https-setup-page.png)

The firmware serves on port `443` with a project-local self-signed certificate. Browsers will warn until you trust `certs/status-server-cert.pem`.

When the ESP32 joins your normal Wi-Fi network, the preferred URL is board-specific by default, such as `https://esp32-status-577180.local/`. The suffix comes from the last 6 hex digits of the ESP32 MAC address so multiple boards can share one Wi-Fi network without fighting over the same hostname. Override `ESP32_STATUS_HOST_NAME` or set `ESP32_STATUS_HOST_NAME_APPEND_MAC=false` in `.env.local` if you need a fixed name.

If the board cannot join the configured Wi-Fi network, it automatically starts a fallback access point using the `kFallbackApSsidPrefix` and `kFallbackApPassword` values in `include/AppConfig.h`. In that mode, use `https://192.168.4.1/`.

If you open `http://` or only the bare IP address, port `80` now serves a bootstrap page. It links to both HTTPS pages, shows the preferred hostname and direct fixed-IP option when available, and lets you download the certificate at `/cert.pem`.

In fixed-IP station mode, choose a unique address for each board. In dynamic-IP station mode, use the board-specific `.local` name or the HTTP bootstrap page to find the current DHCP address.

The private key is stored in `certs/status-server-key.pem` for development convenience. The local `.env` files, generated certificate PEM files, and generated key headers are now git-ignored so they do not get added to a future GitHub repo by default.
