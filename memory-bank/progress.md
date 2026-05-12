# Progress

## 2026-05-12 — SD Card Manager Added

### Completed

- Added `ManageSDCard` as a dedicated SPI SD card subsystem.
- Added SD card mount/status, mounted-card removal checks, directory listing, and file open helpers.
- Added `/sd` as a dedicated HTTPS SD card management page with a file explorer and download links.
- Added SD card APIs under `/api/sd/status`, `/api/sd/list`, `/api/sd/download`, `POST /api/sd/mount`, and `POST /api/sd/eject`.
- Added ESP32 WROOM VSPI defaults: CS `GPIO5`, SCK `GPIO18`, MISO `GPIO19`, MOSI `GPIO23`.
- Changed SD card behavior so the card is not checked at boot; users mount it with `Read Card` and release it with `Eject Card`.

### Validation

- `pio run` completed successfully on 2026-05-12.
- Build size after the SD card feature: RAM 18.8%, flash 70.2% of the configured `no_ota.csv` app space.
- Flashed successfully to `/dev/cu.usbserial-0001` on 2026-05-12.

## 2026-05-12 — Board-Specific Network Identity

### Completed

- Changed the station DHCP hostname, mDNS hostname, and BLE device name to use `esp32-status-<last-6-mac>` by default.
- Added `.env.local` overrides for `ESP32_STATUS_HOST_NAME` and `ESP32_STATUS_HOST_NAME_APPEND_MAC`.
- Added `stationHostName` and `mdnsHostName` to `/api/status`.

### Validation

- `pio run` completed successfully on 2026-05-12.
- Flashed successfully to `/dev/cu.usbserial-0001` on 2026-05-12; detected MAC `04:83:08:57:71:80`, so the expected hostname is `esp32-status-577180.local`.

## 2026-05-12 — SD Card Eject Lifecycle Hardened

### Completed

- Added explicit SD card mount lifecycle states: `Not checked`, `Mounted`, `Ejected`, `Removed`, `Error`, and `Disabled`.
- Changed eject to fully unmount SD state, release the SPI bus, and remain safe when pressed repeatedly.
- Added SD card mount state to `/api/sd/status`, `/api/status`, and the `/sd` page.

### Validation

- `pio run` completed successfully on 2026-05-12.
- Build size after the eject lifecycle change: RAM 18.8%, flash 70.3% of the configured `no_ota.csv` app space.
- Flashed successfully to `/dev/cu.usbserial-0001` on 2026-05-12.

## 2026-05-06 — Memory Bank Initialized

### Completed

- Read `platformio.ini`.
- Read `src/main.cpp`.
- Created initial Memory Bank files under `memory-bank/`.
- Captured confirmed PlatformIO configuration and entry-point architecture.

### Current Status

The project has an initialized Memory Bank suitable for tracking future firmware work, architectural decisions, and progress.

### Not Yet Done

- Detailed review of `Application` implementation.
- Detailed review of each subsystem module.
- Build validation after any future firmware changes.

### Suggested Next Milestone

Review and document the application lifecycle in:

- `include/Application.h`
- `src/Application.cpp`
