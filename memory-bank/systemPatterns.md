# System Patterns

## Entry-Point Pattern

The Arduino entry point is deliberately small:

- `setup()` delegates to `setupApplication()`.
- `loop()` delegates to `runApplication()`.

This keeps platform boilerplate separate from firmware composition and runtime behavior.

## Application Facade Pattern

`Application` should be treated as the primary orchestration boundary. Future work should inspect `include/Application.h` and `src/Application.cpp` before making lifecycle changes.

Expected responsibilities for this layer include:

- Constructing or initializing subsystem modules
- Sequencing setup operations
- Running recurring loop work
- Coordinating status/health reporting

## Subsystem-Oriented Layout

The existing source/header layout suggests one subsystem per module pair:

- `Connectivity` — network/Wi-Fi or related connectivity behavior
- `BluetoothManager` — Bluetooth Low Energy behavior using NimBLE-Arduino
- `StatusDisplay` — display output using U8g2
- `StatusServer` — setup/status HTTP(S) server behavior
- `PowerMonitor` — power/battery/voltage monitoring
- `Heartbeat` — liveness/status heartbeat behavior
- `RegionalSettings` — locale/regional configuration
- `AppConfig` — compile-time/runtime application configuration

These responsibilities are inferred from file names and should be refined after reading each module.

## Design Decisions Captured So Far

1. Keep `src/main.cpp` as a thin adapter to the application layer.
2. Track architecture and progress in `memory-bank/` before making larger changes.
3. Treat build-time secret loading as a project constraint requiring care during future modifications.
