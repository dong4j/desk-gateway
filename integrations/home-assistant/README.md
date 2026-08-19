# Desk Gateway Home Assistant

**Language:** English · [简体中文](README.zh-CN.md)

There is no extra plugin or custom integration in this folder. Desk Gateway firmware is an MQTT client on your LAN broker. Home Assistant creates Cover / height / child-lock entities through MQTT Discovery.

How-to: [`docs/guides/home-assistant-mqtt.en.md`](../../docs/guides/home-assistant-mqtt.en.md). Topic contract: [`docs/future/mqtt-home-assistant.md`](../../docs/future/mqtt-home-assistant.md) (Chinese).

```text
HA → Mosquitto → Desk Gateway MQTT client → desk_core
```

| Cover action | Firmware command | Meaning |
|---|---|---|
| Open | `STAND` | Preset 4 |
| Close | `SIT` | Preset 1 |
| Stop | `STOP` | Global stop |

Do not add an MQTT device by hand. Do not port-forward 1883 or 8123. Do not reuse the Web password as the MQTT user. MQTT cannot unlock child-lock and does not accept arbitrary percent position.

Keep a person next to the desk when testing motion. V1 is still NO-GO. Parent index: [`../README.md`](../README.md).
