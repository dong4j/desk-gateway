# Desk Gateway

**Language:** English · [简体中文](./README.zh-CN.md)

Open-source **standing-desk smart gateway** for ESP32-S3. Vendor-specific protocols live behind pluggable **Desk Drivers**; Web / UART (and later BLE / Matter / Home Assistant) share one control plane (`desk_core`).

> **Safety:** Keep a person nearby when moving the desk. Default motion timeout is 15s. Use **LAN only** — do not expose the Web UI to the public Internet.

## Features (current)

- **Phase 1 — panel emulation:** software I²C slave serves key address `0x24` and height digits `0x34–0x37`
- **Pluggable drivers:** `yourdesk_v1` implemented; Loctek / Jiecang stubs
- **desk_core:** hold up/down and stop; presets 1 (64 cm) and 4 (102 cm) use real-height closed-loop control; global child-lock and per-source REST/Bluetooth/panel permissions are persisted in NVS
- **Maximum safe height:** configurable from the Web UI and persisted in NVS; a fail-closed watchdog projects the worst-case upward position between sparse height frames
- **Wi‑Fi + SoftAP provisioning** and password-protected **LAN Web UI**
- **Real height:** strictly ordered TM1650 display frames are filtered and exposed by UART/Web; the last trusted value survives normal sparse refreshes
- **Automatic resynchronisation:** the first complete controller frame of each motion becomes the new baseline; no user-entered or fabricated height can overwrite it

> The main firmware builds with ESP-IDF 6.0.2. On 2026-08-10, Web hold-to-move
> UP/DOWN and release-to-stop passed on the real desk after restoring the panel's
> two external pull-ups. The unified multi-address software slave then passed
> real-desk movement and `64–80 cm` height tracking. Closed-loop presets,
> predictive maximum-height enforcement, timeout/power-cycle safety, and the global child-lock flow still require hardware acceptance. Panel arbitration and
> lockout are implemented in firmware, but original-panel pass-through still awaits the blocked breadboard capture and real-hardware acceptance.

## Hardware

| Item | Recommendation |
|------|----------------|
| Board | YD-ESP32-S3 N16R8 (or compatible ESP32-S3) |
| Power | USB-C to the ESP32 — **do not** power the MCU from the desk 3.3V rail |
| Ground | Shared GND with the desk host |
| I²C (yourdesk_v1) | RJ45 pin 2 / white CLK → GPIO4; pin 4 / black DAT → GPIO5 |
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
| [docs/architecture/overview.md](./docs/architecture/overview.md) | Architecture overview |
| [docs/bringup-checklist.md](./docs/bringup-checklist.md) | Bring-up / acceptance checklist |
| [docs/ui-demos/](./docs/ui-demos/) | Static Web UI style demos |
| [docs/3-protocol-reverse-notes.md](./docs/3-protocol-reverse-notes.md) | Protocol reverse notes |

## Roadmap

- [x] Replace the rejected GPIO sniffer with a multi-address I²C slave for real height
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
