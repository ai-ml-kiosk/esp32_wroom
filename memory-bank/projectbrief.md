# Project Brief

## Project

ESP32 PlatformIO firmware project for an `esp32dev` board using the Arduino framework.

## Memory Bank Purpose

This directory is the durable project context for future agent sessions. It should capture:

- Current firmware architecture and entry points
- PlatformIO/toolchain configuration
- Active work and open questions
- Progress, decisions, and rationale over time

## Current Confirmed State

- `platformio.ini` defines one environment: `[env:esp32dev]`.
- Target platform is `espressif32`.
- Board is `esp32dev`.
- Framework is `arduino`.
- The firmware uses `board_build.partitions = no_ota.csv`.
- Serial monitor speed is `115200`.
- A pre-build script runs: `scripts/load_build_secrets.py`.
- External libraries configured:
  - `olikraus/U8g2 @ ^2.36.5`
  - `h2zero/NimBLE-Arduino @ ^2.5.0`
- `src/main.cpp` is intentionally thin and delegates to application-level functions:
  - `setup()` calls `setupApplication()`.
  - `loop()` calls `runApplication()`.

## Initial Scope

Initialize Memory Bank documentation only. No firmware behavior changes are part of this initialization.

## Source References Used for Initialization

- `platformio.ini`
- `src/main.cpp`
- Existing project file tree visible at task start
