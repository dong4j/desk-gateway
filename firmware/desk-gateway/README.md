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

## Wiring (yourdesk_v1)

| Desk host | ESP32 |
|-----------|--------|
| GND | GND |
| CLK | GPIO4 |
| DAT | GPIO5 |
| 3.3V | **Do not connect** (USB power) |

Acceptance checklist: [docs/bringup-checklist.md](../../docs/bringup-checklist.md)

## License

MIT — see repository root [LICENSE](../../LICENSE).
