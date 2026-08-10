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
