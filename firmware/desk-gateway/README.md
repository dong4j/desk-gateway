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

With `CONFIG_DESK_YOURDESK_PANEL_PROXY=y`, the controller side remains the
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

The original panel preset keys are intentionally **not accepted as safe-height
validated yet**. Until their complete key/hold sequence has been captured on the
new topology, first hardware acceptance is limited to short UP, DOWN, release,
TOF400C height display, and disconnect-stop tests.

## Height status

The product firmware deliberately does not parse the controller's TM1650 digit
writes. Real-desk diagnostics found that the software multi-address slave often
aborted the `0x24` key response, so the controller intermittently missed `0x47`
or `0x4F` even though the application state still showed movement.

The default build therefore uses the ESP32-S3 hardware I²C Slave only at `0x24`
and logs:

```text
I (...) yourdesk_v1: control-box height input disabled; waiting for external TOF source
I (...) yourdesk_v1: I2C slave @0x24 SCL=4 SDA=5
```

The historical controller-digit decoder and software-I²C Slave stay available
only behind explicit experimental Kconfig switches; they are not compiled into
the default component source list. The separate TOF400C on GPIO10/11 now feeds
the Web/OLED/original-panel display path. It remains isolated from motion,
presets, and maximum-height enforcement until calibration and full-travel
hardware acceptance are complete. See
`docs/7-hardware-i2c-restoration-investigation.md` for the controller-side
rollback evidence.

## Maximum safe height

The authenticated Web settings panel stores `max_height_mm` in NVS. The default
is `1020 mm`, with an accepted range of `640–1290 mm`. The value remains
persisted and synchronized across Web, BLE, and the mobile App, but it is not a
motion safety input while height is unknown. Closed-loop presets 1/4 are also
unavailable in this state.

After TOF400C integration, its validated and calibrated height will feed the
existing driver/core contract. Only then may the configured ceiling and preset
targets be re-enabled. Until that hardware acceptance is complete, do not rely
on the stored value to prevent a collision.

## License

MIT — see repository root [LICENSE](../../LICENSE).
