# Desk Gateway firmware

**Language:** English · [简体中文](README.zh-CN.md)

ESP-IDF project for the standing-desk gateway. Parent docs: [../../README.md](../../README.md) · [中文](../../README.zh-CN.md). How to move the desk from Web, REST, BLE, phone, Watch, keyboard, voice, D200H, or LAN MQTT: [control methods](../../docs/guides/control-methods.md). REST contract: [REST API](../../docs/guides/rest-api.md).

## Build

```bash
# Activate the project's fixed ESP-IDF v6.0.2 environment first, then:
cd firmware/desk-gateway
# Run set-target once after pulling the audio-partition change so an old local
# sdkconfig cannot keep the former 1500 KiB partition table.
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

Requires ESP-IDF v6.0.2. Clones must install the official toolchain and set
`IDF_PATH`; do not copy another machine's Espressif paths. Component Manager
pulls `espressif/cjson` on first build.

Full flash (bootloader, partition table, app, `audio.bin`, NVS preserved):
from the repo root, `./scripts/flash-firmware.sh PORT`. The script prefers
`IDF_PATH` / `IDF_PYTHON_ENV_PATH` when set.

The default configuration also enables the native NimBLE peripheral. It
advertises as `DeskGateway`; BLE commands require a Just Works encrypted/bonded
connection. UUIDs and LightBlue steps are documented in
[ble-accessory-profile.md](../../docs/architecture/ble-accessory-profile.md).

## Provisioning

| SoftAP | Value |
|--------|--------|
| SSID | `DeskGateway` |
| Password | `desk-gateway` |
| URL | http://192.168.4.1/ |

LAN Web default password: `desk-gateway`.

## Wiring (mxtark, Phase 1 panel replacement)

Full GPIO flying-lead map. The sections below keep the per-bus pin detail.

![YD-ESP32-S3 full wiring](../../docs/architecture/images/full-wiring.png)

![YD-ESP32-S3 to dual-RJ45 left jack](../../docs/architecture/images/dual-rj45-left-wiring.png)

| RJ45 | Signal | Connection |
|-------|--------|------------|
| pin 1 / red | desk 3.3V | Pull-up source only; **do not connect to ESP32 3V3** |
| pin 2 / white | CLK | GPIO4 |
| pin 3 / green | GND | ESP32 GND |
| pin 4 / black | DAT | GPIO5 |

Removing the original TM1650 panel also removes its two measured `1.99 kΩ`
pull-ups. Add two non-polarized resistors before testing:

```text
red 3.3V ── 2 kΩ (2.2 kΩ acceptable) ── white CLK / GPIO4
red 3.3V ── 2 kΩ (2.2 kΩ acceptable) ── black DAT / GPIO5
green GND ───────────────────────────── ESP32 GND
```

The resistors are pull-ups, not series resistors: white and black still connect
directly to their GPIOs. Power the ESP32 independently over USB.

Acceptance checklist: [docs/guides/bringup-checklist.md](../../docs/guides/bringup-checklist.md)

## Pomodoro voice reminder (MAX98357A)

The default firmware includes an ESP-local Pomodoro timer and real Chinese WAV
voice prompts. Wire the I2S amplifier as follows:

![YD-ESP32-S3 to MAX98357A I2S wiring](../../docs/architecture/images/max98357a-wiring.png)

| ESP32-S3 | MAX98357A |
|----------|-----------|
| `3V3`, or `5V` after bridging `IN-OUT` | VIN |
| GND | GND |
| GPIO14 | BCLK |
| GPIO15 | LRC / WS |
| GPIO16 | DIN |

Connect a 4 Ω / 3 W speaker between `SPK+` and `SPK-`; neither speaker terminal
goes to GND. The YD-ESP32-S3 header `5V` pin is not live from USB until the
board `IN-OUT` pads are bridged; use `3V3` for bring-up. Do not power the
amplifier from the desk RJ45 3.3V rail.

The 16 kHz / 16-bit / Mono WAV pack lives in a separate 4 MiB `audio` SPIFFS
partition. Use a full `idf.py flash` after building so `audio.bin` is flashed at
`0x310000`; `idf.py app-flash` alone does not install or update the voice pack.
Start at 20% volume for first hardware bring-up. The firmware and automated
tests are complete, but audio quality, brownout, click/pop and desk-bus EMI stay
open until the physical amplifier and speaker are tested.

## Status LEDs

Three discrete LEDs are at-a-glance indicators. Default firmware drives them
on GPIO1 / GPIO2 / GPIO8; GPIO init failure does not block desk control.
GPIO48 onboard WS2812 stays unused, and GPIO17 stays reserved for MAX98357A
`SD`. Hardware lighting is still unaccepted.

![YD-ESP32-S3 to red / yellow / blue status LEDs](../../docs/architecture/images/status-leds-wiring.png)

| LED | GPIO | Resistor | Meaning |
|-----|------|----------|---------|
| Red | GPIO1 | 220 Ω–330 Ω | Child lock, fault, or upward blocked |
| Yellow | GPIO2 | 220 Ω–330 Ω | SoftAP, or STA not connected |
| Blue | GPIO8 | 68 Ω–100 Ω (82 Ω in the diagram) | Desk is moving |

Active-high, common-cathode: GPIO → series resistor → LED anode, cathode to
ESP32 `GND`. The long LED lead is the anode. Drive from 3.3 V GPIOs only; do
**not** put the anode on 5 V, and do **not** use the desk RJ45 3.3 V rail. Keep
the fly-wires away from GPIO4–7 and GPIO14–16.

## Wiring (mxtark, Phase 2 original-panel proxy)

![Dual RJ45 pass-through data flow](../../docs/architecture/images/dual-rj45-passthrough-flow.png)

The left jack's Ethernet cable goes to the controller (GPIO4/5). The right
jack's cable goes to the original panel (GPIO6/7). Panel keys and multi-client
commands both enter `desk_core`; only the left jack talks to the controller.

![YD-ESP32-S3 to dual-RJ45 right jack pass-through](../../docs/architecture/images/dual-rj45-right-wiring.png)

The two RJ45 sockets on the breakout are independent. Keep controller CLK/DAT
on the stable ESP32-S3 hardware-slave bus, and connect the original panel
CLK/DAT to the isolated GPIO software-master bus:

| Signal | Left socket / controller side | Right socket / original panel side |
|--------|-------------------------------|------------------------------------|
| pin 1 / red / 3.3V | controller red; existing 2 kΩ pull-ups remain here | jumper directly to left pin 1 |
| pin 2 / white / CLK | GPIO4 | GPIO6 |
| pin 3 / green / GND | ESP32 GND | jumper directly to left pin 3 |
| pin 4 / black / DAT | GPIO5 | GPIO7 |

Do **not** jumper left pin 2 to right pin 2 or left pin 4 to right pin 4: that
would bypass the ESP32 transaction proxy. Do not add another pair of pull-ups on
the panel side; the original panel already measured approximately `1.99 kΩ` from
3.3V to CLK and DAT. The red jumper only carries the controller's 3.3V to power
the original panel and must still not connect to ESP32 `3V3`.

With `CONFIG_DESK_MXTARK_PANEL_PROXY=y`, the controller side remains the
hardware I2C Slave `@0x24` on GPIO4/5. GPIO6/7 reproduce the captured TM1650
transactions as a 9.6 kHz open-drain software Master, publish original-panel
keys into the existing arbiter, and display the calibrated TOF400C height on the
original panel. A physical panel key takes priority over Web movement; Panel
permission and child lock use the same `desk_core` policy. A panel timeout or
disconnect is immediately published as idle so motion cannot stay latched.

The original `idle_12mhz_full.sr` capture uses two STOP-separated transactions
at about `9.6 kHz`: write `0x48/0x01`, wait about `29 us`, then read `0x49/DR`
and finish with controller `ACK + STOP`; the next write begins about `95 us`
later. The ESP-IDF 6 Master API combines write/read and enforces standard
`NACK + STOP`, so it cannot reproduce that sequence. The panel-side proxy uses
an isolated open-drain GPIO implementation and always attempts STOP on NACK or
timeout. GPIO6/7 must not be shared with another peripheral.

The original panel preset keys 2 / 3 are still **not** treated as a validated
safe-height path. UP, DOWN, release, TOF400C height display, disconnect-stop,
arbitration, and child-lock lockout have been accepted on the real desk.

## Height status

![YD-ESP32-S3 to TOF050C / TOF400C](../../docs/architecture/images/dual-tof-wiring.png)

The product firmware deliberately does not parse the controller's TM1650 digit
writes. Real-desk diagnostics found that the software multi-address slave often
aborted the `0x24` key response, so the controller intermittently missed `0x47`
or `0x4F` even though the application state still showed movement.

The default build therefore uses the ESP32-S3 hardware I²C Slave only at `0x24`
and logs:

```text
I (...) mxtark: control-box height input disabled; waiting for external TOF source
I (...) mxtark: I2C slave @0x24 SCL=4 SDA=5
```

The historical controller-digit decoder and software-I²C Slave stay available
only behind explicit experimental Kconfig switches; they are not compiled into
the default component source list. The separate TOF400C on GPIO10/11 now
supplies the raw, filtered control height as well as the Web/OLED/original-panel
display. TOF050C blocks upward motion only while height is below `800 mm` and
the right gap is below `80 mm`; the `940 mm` ceiling is always enforced. See
`docs/hardware/i2c-restoration.md` for the controller-side
rollback evidence.

## Maximum safe height

The authenticated Web settings panel stores `min_height_mm` and `max_height_mm`
in NVS. They default to `550 mm` and `940 mm`; the physical input range is
`550–940 mm`. The minimum only validates preset input because the control box
owns the noisy lower stop. Presets 1/4 default to `550 mm` and `870 mm`. These
values use the filtered TOF400C distance directly, without converting it to
tape-measure desktop height. They are persisted and synchronized across Web,
BLE, and the mobile App. Upward motion is blocked when
TOF400C is unavailable; below `800 mm`, it is also blocked when TOF050C is
unavailable.

Authenticated automation that explicitly needs the highest safe position uses
`POST /api/v1/desk/raise-to-max`. The endpoint is available only when the active
driver advertises a device-side bounded action backed by real height feedback;
it never falls back to the manual `/api/v1/desk/up` hold behavior. Query
`raise_to_max_supported` from `GET /api/v1/desk/status` before exposing the
action to an external voice or MCP tool.

## License

MIT — see repository root [LICENSE](../../LICENSE).
