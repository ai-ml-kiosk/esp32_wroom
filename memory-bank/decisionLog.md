# Decision Log

This file records architectural and workflow decisions as the ESP32 firmware evolves.

## 2026-05-06 — Initialize Memory Bank

### Decision

Create `memory-bank/` with core context files for project brief, product context, active context, system patterns, technical context, progress, and decisions.

### Rationale

The project already has an ESP32 PlatformIO structure and multiple subsystem modules. A Memory Bank provides durable context for future agent sessions and helps ensure architectural changes are made deliberately.

### Consequences

- Future work should update Memory Bank files when architecture, tooling, or project direction changes.
- `src/main.cpp` is documented as a thin Arduino adapter delegating to the `Application` layer.
- Build-time secret loading is called out as an area requiring careful handling.
