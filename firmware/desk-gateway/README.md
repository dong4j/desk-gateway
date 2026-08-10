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

## Height status (GPIO approach rejected on hardware)

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

With the driver no longer claiming real-height support, `desk_core` may provide
the explicitly marked SIM value when `CONFIG_DESK_SIM_HEIGHT=y`. Real height
now requires one software I²C state machine that can ACK `0x24` and
`0x34–0x37`; ESP32-S3's hardware slave exposes only one address.

If the rejected experimental listener is enabled for protocol work,
`CONFIG_GPIO_CTRL_FUNC_IN_IRAM=y` remains mandatory and is enforced at compile
time.

## License

MIT — see repository root [LICENSE](../../LICENSE).
