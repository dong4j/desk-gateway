# Desk Gateway

**Language:** English · [简体中文](./README.zh-CN.md)

Open-source **standing-desk smart gateway** for ESP32-S3. Vendor-specific protocols live behind pluggable **Desk Drivers**; Web / UART (and later BLE / Matter / Home Assistant) share one control plane (`desk_core`).

> **Safety:** Keep a person nearby when moving the desk. Default motion timeout is 15s. Use **LAN only** — do not expose the Web UI to the public Internet.

## Features (current)

- **Phase 1 — panel emulation:** ESP32 acts as I²C slave (panel @ `0x24`) for `yourdesk_v1`
- **Pluggable drivers:** `yourdesk_v1` implemented; Loctek / Jiecang stubs
- **desk_core:** hold up/down, stop, presets 1/4, child-lock flag (NVS), motion timeout
- **Wi‑Fi + SoftAP provisioning** and password-protected **LAN Web UI**
- **SIM height** for UI demo when digit sniffing is not available yet

> The main firmware builds with ESP-IDF 6.0.2, but real-desk motion, stop safety,
> and on-device Web behavior still require the hardware checklist. Phase 2 MITM,
> panel arbitration, and effective panel lockout are not implemented.

## Hardware

| Item | Recommendation |
|------|----------------|
| Board | YD-ESP32-S3 N16R8 (or compatible ESP32-S3) |
| Power | USB-C to the ESP32 — **do not** power the MCU from the desk 3.3V rail |
| Ground | Shared GND with the desk host |
| I²C (yourdesk_v1) | SCL → GPIO4, SDA → GPIO5 |

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
| [docs/architecture/overview.md](./docs/architecture/overview.md) | Architecture overview |
| [docs/bringup-checklist.md](./docs/bringup-checklist.md) | Bring-up / acceptance checklist |
| [docs/ui-demos/](./docs/ui-demos/) | Static Web UI style demos |
| [docs/3-protocol-reverse-notes.md](./docs/3-protocol-reverse-notes.md) | Protocol reverse notes |

## Roadmap

- [ ] Real height digit sniffing (disable `CONFIG_DESK_SIM_HEIGHT`)
- [ ] Phase 2 dual‑RJ45 MITM + true panel blocking under child lock
- [ ] BLE accessory profile (OLED / knob)
- [ ] Matter / Home Assistant integrations
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
