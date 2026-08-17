# Changelog

All notable changes to Desk Gateway are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
This project is early-stage hardware. **V1 is not released** (see
[`docs/12-v1-release-acceptance.md`](docs/12-v1-release-acceptance.md)).

## [Unreleased]

Snapshot of the default branch as of 2026-08-17.

### Added

- ESP32-S3 firmware on ESP-IDF v6.0.2: `mxtark` I²C Slave `@0x24`, `desk_core`
  (STOP, timeout, child-lock, source ACL), LAN Web / REST, NimBLE accessory
  profile (three Centrals, motion owner, pairing window, Bond management).
- Dual ToF: TOF400C as product height, TOF050C as right-gap upward guard.
- Phase 2 original-panel proxy on GPIO6/7, disconnect STOP, arbitration.
- iPhone / Android Expo app (BLE first, REST fallback) and independent Apple Watch app.
- Keyboard / knob (Karabiner), GoatRemote sit/stand, XiaoZhi MCP bridge, Ulanzi D200H plugin.
- Local multi-client setup checklist:
  [`docs/guides/local-multi-client-setup.md`](docs/guides/local-multi-client-setup.md).

### Known gaps (not a release)

- BLE/Wi-Fi out-of-range fallback matrix, custom presets / B12 hardware acceptance,
  OLED 30-minute soak, TestFlight / Android beta, optional Pomodoro speaker and
  XiaoZhi end-to-end cloud path.
- Matter / Home Assistant / OTA / Loctek / Jiecang / original-panel keys 2 and 3.

[Unreleased]: https://github.com/dong4j/desk-gateway/compare/HEAD
