# Desk Gateway Home Assistant

**语言：** [English](README.md) · 简体中文

本目录没有额外插件或自定义集成。Desk Gateway 固件自己当 MQTT Client，连局域网 Broker；Home Assistant 用 MQTT Discovery 建出 Cover / 高度 / 童锁。

完整操作步骤见 [`docs/guides/home-assistant-mqtt.md`](../../docs/guides/home-assistant-mqtt.md)。Topic 与安全边界见 [`docs/future/mqtt-home-assistant.md`](../../docs/future/mqtt-home-assistant.md)。

```text
HA → Mosquitto → Desk Gateway MQTT Client → desk_core
```

| Cover 动作 | 固件命令 | 含义 |
|---|---|---|
| 打开 | `STAND` | 档位 4 |
| 关闭 | `SIT` | 档位 1 |
| 停止 | `STOP` | 全局停止 |

不要在 HA 里再「添加 MQTT 设备」。不要把 1883/8123 映射到公网。MQTT 用户不要复用 Web 密码。MQTT **不能**解除童锁，也没有任意百分比定位。

测运动时必须有人在桌旁。V1 仍是 NO-GO。上级目录见 [`../README.zh-CN.md`](../README.zh-CN.md)。
