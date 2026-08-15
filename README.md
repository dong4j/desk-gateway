# Desk Gateway

**Language:** English · [简体中文](./README.zh-CN.md)

Open-source **standing-desk smart gateway** for ESP32-S3. Vendor-specific protocols live behind pluggable **Desk Drivers**; Web / UART / BLE (and later Matter / Home Assistant) share one control plane (`desk_core`).

> **Safety:** Keep a person nearby when moving the desk. TOF400C height and TOF050C right-side clearance now participate in upward safety decisions: upward motion is blocked when height is unknown, the ceiling is reached, or height is below 80 cm while right-side clearance is unknown or below 8 cm. DOWN and STOP remain available. The full hardware safety matrix is still open. Use **LAN only** — do not expose the Web UI to the public Internet.

## Features (current)

- **Phase 1 — panel emulation:** ESP32-S3 hardware I²C Slave serves the key address `0x24`
- **Pluggable drivers:** `mxtark` implemented; Loctek / Jiecang stubs
- **desk_core:** hold up/down and stop; global child-lock and per-source REST/Bluetooth/panel permissions are persisted in NVS
- **Dual-ToF sensing:** TOF400C provides the product height directly; TOF050C measures right-side clearance; both paths include stabilization and stale-data detection
- **Closed-loop height control:** the configurable preset floor, seated, standing, and ceiling settings are synchronized and persisted; current defaults are 550 / 550 / 870 / 940 mm, and the preset floor does not trigger a downward STOP
- **Wi‑Fi + SoftAP provisioning** and password-protected **LAN Web UI**
- **BLE Accessory Profile:** up to three connected Centrals, one motion owner, encrypted Client Info, explicit pairing windows, Bond management, hold leases, disconnect-stop, preset commands, and status notifications
- **Consistent height state:** Web/REST/BLE/OLED/original panel use the stabilized TOF400C distance; SIM and control-box digit parsing are disabled

> The main firmware builds with ESP-IDF 6.0.2. The current `23f1c7d1` image was fully
> flashed and its Mxtark `0x24`, panel proxy, BLE, Wi-Fi, Web, ToF, OLED, and audio
> startup paths were observed on 2026-08-15. This is startup evidence, not motion
> acceptance. On 2026-08-10, Web hold-to-move
> UP/DOWN and release-to-stop passed on the real desk after restoring the panel's
> two external pull-ups. Later multi-address software-I²C height work regressed
> continuous movement, so the default firmware has returned to the hardware `0x24`
> path validated by commit `3269faa`. LightBlue and an earlier iPhone App build passed
> their core control paths, but the current App BLE long-press fix and firmware combination
> still need a real-desk regression. Dual-ToF closed-loop presets and upward protection are now implemented;
> the complete real-desk safety matrix remains open.
> The consolidated abnormal-stop matrix, Android acceptance, and original-panel
> pass-through / lockout remain open. The three-client firmware, Web/mobile Bond
> management, and Watch/mobile Client Info code pass automated gates, but the
> iPhone + Apple Watch + Android hardware matrix remains open; see the status below.

## Hardware

| Item | Recommendation |
|------|----------------|
| Board | YD-ESP32-S3 N16R8 (or compatible ESP32-S3) |
| Power | USB-C to the ESP32 — **do not** power the MCU from the desk 3.3V rail |
| Ground | Shared GND with the desk host |
| I²C (mxtark) | RJ45 pin 2 / white CLK → GPIO4; pin 4 / black DAT → GPIO5 |
| Pull-ups | RJ45 pin 1 / red 3.3V → **2 kΩ (2.2 kΩ acceptable)** → CLK, and another → DAT |

The red 3.3V wire is only the pull-up source. **Never connect it directly to the
ESP32 3V3 pin.** Removing the original panel also removes its two measured
`1.99 kΩ` pull-ups; a replacement ESP32 setup must restore them.

Wiring checklist: [docs/bringup-checklist.md](./docs/bringup-checklist.md)

## Quick start

### Prerequisites

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) **5.2+** (developed on **6.0.x**)
- Target: `esp32s3`

### Build & flash

```bash
cd firmware/desk-gateway
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

On macOS with Espressif Install Manager, activate your IDF env first (example alias: `get-idf`).

For a repeatable compile-only check that avoids stale local `build/` caches:

```bash
./scripts/check-firmware.sh
```

### Wi‑Fi (SoftAP — recommended)

If no credentials are stored, the device opens:

| | |
|--|--|
| SSID | `DeskGateway` |
| Password | `desk-gateway` |
| Setup page | http://192.168.4.1/ |

Then join your home **2.4 GHz** Wi‑Fi. Open `http://<device-ip>/` — default Web password: `desk-gateway`.

Normal flash keeps NVS (Wi‑Fi usually survives). `idf.py erase-flash` clears it.

### Serial commands (debug)

`wifi <ssid> <pass>` · `up` / `down` / `stop` · `p1` / `p4` · `lock` / `unlock`

Web hold buttons: **press = move**, **release = stop** (DR held, not spammed).

## Repository layout

```text
firmware/desk-gateway/     Main ESP-IDF firmware
firmware/phase1-panel-slave/  Earlier Phase‑1 prototype (legacy)
docs/                      Requirements, architecture, protocol notes, UI demos
LICENSE                    MIT
NOTICE                     Third-party attributions
```

## Documentation

| Doc | Description |
|-----|-------------|
| [docs/0-requirements.md](./docs/0-requirements.md) | Requirements (Chinese) |
| [docs/5-current-status-and-priorities.md](./docs/5-current-status-and-priorities.md) | Current status and prioritized backlog (Chinese) |
| [docs/12-v1-release-acceptance.md](./docs/12-v1-release-acceptance.md) | V1 release acceptance gates and sign-off record (Chinese) |
| [docs/7-hardware-i2c-restoration-investigation.md](./docs/7-hardware-i2c-restoration-investigation.md) | Why the default firmware returned from software to hardware I²C (Chinese) |
| [docs/4-tof-distance-sensor-plan.md](./docs/4-tof-distance-sensor-plan.md) | Dual-ToF wiring, filtering, presets, and upward safety policy (Chinese) |
| [docs/architecture/overview.md](./docs/architecture/overview.md) | Architecture overview |
| [docs/architecture/mqtt-home-assistant.md](./docs/architecture/mqtt-home-assistant.md) | MQTT / Home Assistant integration design (Chinese) |
| [docs/architecture/ble-accessory-profile.md](./docs/architecture/ble-accessory-profile.md) | BLE UUIDs, byte protocol, and LightBlue test flow |
| [docs/architecture/ble-multi-client-bond-management.md](./docs/architecture/ble-multi-client-bond-management.md) | Three-client ownership, pairing windows, and Bond management |
| [docs/bringup-checklist.md](./docs/bringup-checklist.md) | Bring-up / acceptance checklist |
| [docs/ui-demos/](./docs/ui-demos/) | Static Web UI style demos |
| [docs/3-protocol-reverse-notes.md](./docs/3-protocol-reverse-notes.md) | Protocol reverse notes |

## Roadmap

- [x] Restore hardware I²C Slave `@0x24` as the stable movement transport
- [x] Validate BLE control with LightBlue and the iPhone App on the real desk
- [x] Deliver BLE-first / REST-fallback mobile control and synchronized device settings
- [x] Implement the configurable 550 mm preset floor across firmware, Web, App, Watch, REST, and BLE Config v3
- [x] Implement three-client BLE ownership, Client Info, pairing windows, and Web/mobile Bond management
- [x] Implement the Phase 2 dual-RJ45 panel proxy, arbitration, and panel control gates
- [ ] Complete Phase 2 real-desk pass-through, disconnect-STOP, arbitration, and true lockout acceptance
- [ ] Revalidate current App BLE long-press UP/DOWN and release-to-STOP on the real desk
- [ ] Complete the abnormal-stop matrix and Android hardware acceptance
- [x] Integrate TOF400C height and TOF050C right-side clearance, restoring closed-loop presets and the safe ceiling
- [x] Implement the Apple Watch app and multi-client handshake (hardware acceptance remains open)
- [ ] Complete the iPhone + Apple Watch + Android three-client hardware matrix
- [ ] Complete the dual-ToF preset, ceiling, and right-side obstacle hardware safety matrix
- [ ] Matter / Home Assistant integrations
- [ ] OTA firmware updates
- [ ] Additional desk drivers (Loctek, Jiecang, …)

## Contributing

See [CONTRIBUTING.md](./CONTRIBUTING.md) and
[CODE_OF_CONDUCT.md](./CODE_OF_CONDUCT.md). Before opening an issue, use the
[support guide](./SUPPORT.md) to choose the correct channel and collect the
required evidence.

## Security

See [SECURITY.md](./SECURITY.md). Do not port-forward the Web UI. Change the default password after first login.

## License

This project is licensed under the **MIT License** — see [LICENSE](./LICENSE).

Third-party components (ESP-IDF, cJSON, etc.) retain their own licenses — see [NOTICE](./NOTICE).

## Disclaimer

Not affiliated with any desk manufacturer. Reverse‑engineered protocol notes are for interoperability on hardware you own. Use at your own risk; moving furniture can cause injury or damage.
