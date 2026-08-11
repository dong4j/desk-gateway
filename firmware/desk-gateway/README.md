# Desk Gateway firmware

ESP-IDF project for the standing-desk gateway. Parent docs: [../../README.md](../../README.md) · [中文](../../README.zh-CN.md)

## Build

```bash
# Activate ESP-IDF first, then:
cd firmware/desk-gateway
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

Requires ESP-IDF ≥ 5.2 (tested on 6.0.x). Component Manager pulls `espressif/cjson` on first build.

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

## Wiring (yourdesk_v1, Phase 1 panel replacement)

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

Acceptance checklist: [docs/bringup-checklist.md](../../docs/bringup-checklist.md)

## Wiring (yourdesk_v1, Phase 2 original-panel proxy)

The two RJ45 sockets on the breakout are independent. Keep controller CLK/DAT
on the existing software-slave bus, and connect the original panel CLK/DAT to a
second ESP32 hardware-master bus:

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

With `CONFIG_DESK_YOURDESK_PANEL_PROXY=y`, the controller side remains the
multi-address slave on GPIO4/5. GPIO6/7 poll the original panel as an I2C master,
forward controller digit writes to its display, and cache panel key responses
for the next controller poll. A physical panel key takes priority over Web
movement; a panel timeout/disconnect is published as idle so motion cannot stay
latched. Child-lock filtering is not part of this first proxy version.

The original `idle_12mhz_full.sr` capture uses two STOP-separated transactions
at about `9.6 kHz`: write `0x48/0x01`, wait about `29 us`, then read `0x49/DR`
and finish with controller `ACK + STOP`; the next write begins about `95 us`
later. The ESP-IDF 6 new Master API instead combines write/read and enforces
standard `NACK + STOP`. The panel-side proxy therefore uses the isolated legacy
command-link API to reproduce the confirmed waveform exactly. This is an
intentional ESP-IDF 6 compatibility constraint; migration to ESP-IDF 7 requires
a new panel-specific Master implementation rather than silently changing the
transaction boundaries or ACK.

The original panel preset keys are intentionally **not accepted as safe-height
validated yet**. Until their complete key/hold sequence has been captured on the
new topology, first hardware acceptance is limited to short UP, DOWN, release,
display mirroring, and disconnect-stop tests.

## Height status

The complete panel-specific `0–9` segment map and its golden-vector decoder are
kept and tested. The first integration attempted to sample writes to 7-bit
addresses `0x34–0x37` with GPIO edge interrupts while the hardware I²C Slave
continued serving `0x24`.

Real-desk testing rejected that topology: the high-rate GPIO interrupts
interfered with movement, while the controller apparently stopped after digit
address NACK and provided no height data. Stable builds therefore leave
`CONFIG_DESK_YOURDESK_HEIGHT_SNIFFER_EXPERIMENTAL` disabled and log:

```text
I (...) yourdesk_v1: experimental GPIO height sniffer disabled
```

The replacement is one host-tested software I²C state machine that ACKs `0x24`
and `0x34–0x37`. Real-desk testing on 2026-08-10 confirmed that movement still
works while digit frames decode through `64–80 cm`; after stopping, the API kept
the last valid value (`height_mm=800`) without SIM fallback. This path is now
enabled by default with `CONFIG_DESK_YOURDESK_SOFT_I2C_MULTI_ADDRESS=y`.

## Maximum safe height

The authenticated Web settings panel stores `max_height_mm` in NVS. The default
is `1020 mm`, with an accepted range of `640–1290 mm`. Both `desk_core` and the
active driver reject upward travel when height is unknown or already at the
limit. During movement, `yourdesk_v1` checks strict, ordered display frames and
runs an independent 50 ms watchdog. Between sparse frames it projects a
worst-case upward position and sends idle before the configured ceiling. Normal
multi-second gaps between display frames do not interrupt movement; the same
projection keeps limiting upward travel without depending on another frame.
Lowering the limit below the current height never moves the desk automatically;
it only blocks further upward travel.

After boot, the controller may not emit a digit frame until the display changes.
While height is unknown, Web keeps manual UP and DOWN available so either action
can trigger the controller's first display frame; preset 4 remains disabled.
Unknown-height UP has a bounded 2-second acquisition window and stops if no real
frame arrives, while preset 1 may bootstrap toward its known `640 mm` target.
The first decoded frame resumes normal closed-loop control. The first complete frame
of every new motion is accepted as an authoritative resynchronisation baseline;
direction and speed filters apply only to later frames in that motion. Predictive
maximum-height stopping never clears or overwrites the last controller height.
Instead it latches further upward movement until DOWN produces a fresh height
inside the safe region. There is no manual height-calibration endpoint.

If the rejected experimental listener is enabled for protocol work,
`CONFIG_GPIO_CTRL_FUNC_IN_IRAM=y` remains mandatory and is enforced at compile
time.

## License

MIT — see repository root [LICENSE](../../LICENSE).
