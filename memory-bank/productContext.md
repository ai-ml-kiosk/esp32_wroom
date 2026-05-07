# Product Context

## What This Project Appears To Be

This is firmware for an ESP32-based device. From the existing project structure, the device appears to include connectivity, Bluetooth/NimBLE support, display/status output, power monitoring, heartbeat behavior, regional settings, and an HTTP(S) status/setup surface.

## User/Operator Goals

- Build and flash firmware through PlatformIO.
- Keep the Arduino entry point simple and delegate behavior to application modules.
- Maintain clear documentation of architectural decisions as the firmware evolves.
- Track project progress across sessions using this Memory Bank.

## Important Context

- The presence of `docs/http-bootstrap.png`, `docs/https-dashboard.png`, `docs/https-setup-page.png`, and `docs/https-status-page.png` suggests the firmware exposes setup/status web pages.
- The presence of `certs/` and `scripts/generate_self_signed_cert.sh` suggests HTTPS/self-signed certificate support may be involved.
- The presence of `scripts/load_build_secrets.py` indicates build-time secrets/configuration are loaded before compilation.

## Open Questions

- What exact product/device role should be documented once the firmware behavior is reviewed in detail?
- Which secrets are loaded by `scripts/load_build_secrets.py`, and how should they be managed safely?
- What runtime setup flow is expected for Wi-Fi, Bluetooth, display, and status server modules?
