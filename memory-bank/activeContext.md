# Active Context

## Current Focus

Memory Bank initialization for an existing ESP32 PlatformIO project.

## Recent Observations

- `platformio.ini` confirms an Arduino-based ESP32 development target.
- `src/main.cpp` confirms a clean application facade pattern:

```cpp
#include "Application.h"

void setup() {
  setupApplication();
}

void loop() {
  runApplication();
}
```

## Current Architectural Direction

- Keep `src/main.cpp` minimal.
- Treat `Application` as the firmware composition/root lifecycle layer.
- Preserve module boundaries implied by the project structure:
  - Connectivity management
  - Bluetooth/NimBLE management
  - Status display
  - Status server
  - Power monitoring
  - Heartbeat/runtime liveness
  - Regional settings
  - App configuration

## Immediate Next Steps for Future Work

1. Read `include/Application.h` and `src/Application.cpp` before changing lifecycle behavior.
2. Read module headers/source before modifying a subsystem.
3. Document each non-trivial design decision in `memory-bank/decisionLog.md` or the relevant Memory Bank file.
4. Validate changes with PlatformIO build/test commands where practical.

## Constraints

- Do not change firmware behavior as part of Memory Bank initialization.
- Avoid committing secrets or generated sensitive files.
- Preserve existing PlatformIO environment naming and dependency constraints unless there is a deliberate reason to change them.
