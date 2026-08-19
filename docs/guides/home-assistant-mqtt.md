# 用 Home Assistant 控制升降桌

| 项 | 内容 |
|---|---|
| 日期 | 2026-08-19 |
| 适用 | 已烧录带 MQTT Client 的 Desk Gateway 固件，局域网已有 Home Assistant + Mosquitto |
| 协议契约 | [MQTT / Home Assistant 方案](../future/mqtt-home-assistant.md) |
| REST 配置 | [REST API](rest-api.md) |

Desk Gateway 作为 **MQTT Client** 连你已经在跑的局域网 Broker，不在 ESP32 上开 Broker，也不做 Matter。Home Assistant 通过 MQTT Discovery 自动出现一台 **Desk Gateway** 设备，无需手写 YAML、也不要再点「添加 MQTT 设备」。

V1 仍是 **NO-GO**。本页是局域网接入步骤，不是发布说明。完整安全矩阵（童锁、面板抢占、断线不补执行）仍以 [当前状态](../status/current-status-and-priorities.md) 为准。

```text
Home Assistant  ←→  局域网 Mosquitto (1883)
                         ↑
                  Desk Gateway MQTT Client
                         ↓
                      desk_core
```

## 1. 会得到什么

MQTT 集成里会出现一台设备 **Desk Gateway**，三个实体：

| HA 实体 | 作用 | 约束 |
|---|---|---|
| Cover：Desk | 打开 = 起立（档位 4），关闭 = 请坐（档位 1），停止 = 全局 STOP | 没有长按升降，也没有任意百分比定位 |
| Sensor：Height | 产品高度，单位 mm | 来源是 TOF400C，不是控制盒数码管 |
| Binary Sensor：Child lock | 童锁只读 | MQTT **不能**解除童锁 |

Cover 打开/关闭走的是档位闭环，不是窗帘行程。高度条可能没有，这是预期：固件不提供 `set_position`。

## 2. 前置条件

- Desk Gateway 已加入 STA，浏览器能打开 Web 设置页。
- Home Assistant 已安装 **Mosquitto broker** 附加组件（或等价局域网 Broker），端口 **1883**（明文）或 **8883**（TLS）。
- Home Assistant 已添加 **MQTT** 集成，并能发现其他 MQTT 设备（例如涂鸦开关）。
- 不要把 1883 / 8883 / 8123 做公网端口映射。
- 测运动时必须有人在桌旁，随时可以按 Cover 停止或切断控制盒电源。

网页 `http://<ha-ip>:8123/` 不是 Broker。Broker 主机填 HA 的局域网 IP 或 hostname，端口填 **1883**。

## 3. 在 Mosquitto 建独立用户

不要复用 Desk Gateway 的 Web 密码，也不要用 HA 登录账号。

Home Assistant OS 上常见做法：

1. 设置 → 附加组件 → **Mosquitto broker** → 配置。
2. 增加一个只给这台桌子用的用户，例如 `desk-gateway`，密码单独设置。
3. 保存并重启 Mosquitto。
4. 确认 HA 的 MQTT 集成仍指向这个 Broker（通常是 `core-mosquitto`）。

若 Mosquitto 开了 ACL，至少允许该用户：

```text
topic read  desk-gateway/<id>/command
topic read  homeassistant/status
topic write desk-gateway/<id>/availability
topic write desk-gateway/<id>/state
topic write desk-gateway/<id>/result
topic write homeassistant/cover/<id>/config
topic write homeassistant/sensor/<id>_height/config
topic write homeassistant/binary_sensor/<id>_child_lock/config
```

把 `<id>` 换成 12 位小写 MAC，例如 `907069ef299c`。设备 ID 在 Web MQTT 页或 `GET /api/v1/mqtt` 里可以看到，不能手改。

## 4. 在 Desk Gateway 填写 MQTT

打开网关 Web → 设置，或调用 `PUT /api/v1/mqtt`。

| 字段 | 填什么 |
|---|---|
| Broker 主机 | HA 的局域网 IP，例如 `192.168.21.28`，**不要**填 `:8123` |
| 端口 | `1883`（TLS 选证书包时用 `8883`） |
| TLS | 受信任局域网用 `none` |
| 用户名 / 密码 | 上一节的 Mosquitto 用户，不是 Web 密码 |
| Discovery 前缀 | 默认 `homeassistant`，须与 HA MQTT 集成一致 |
| 连接 Broker 并上报状态 | 打开后才会连 Broker、发 Discovery |
| 允许 MQTT 控制桌子 | 另开。只开连接、关掉控制时，HA 只能看高度和童锁 |

保存后串口应出现 `desk_mqtt: connected`，随后三条：

```text
discovery published homeassistant/cover/<id>/config
discovery published homeassistant/sensor/<id>_height/config
discovery published homeassistant/binary_sensor/<id>_child_lock/config
```

STA 才连 Broker。SoftAP 配网状态下不会连。

## 5. 在 Home Assistant 确认设备

1. 设置 → 设备与服务 → **MQTT**。
2. 看左边 **设备** 列表，应多出 **Desk Gateway**。不要点进已有的涂鸦开关那一张卡。
3. 不要再「添加 MQTT 条目」或手工加 Cover。Discovery 已经发过了。
4. 打开 Cover 前，先打开 Desk Gateway 上的「允许 MQTT 控制桌子」。
5. Cover：**打开 = 起立，关闭 = 请坐，停止 = STOP**。命令已接受不等于桌子已经到位。

HA 重启后，固件订阅了 `homeassistant/status`，收到 `online` 会重发 Discovery 和状态。Discovery **不 retain**，所以设备离线很久再开 HA 时，等网关重新连上 Broker 即可，不必手写配置。

## 6. 自动化示例

实体 ID 以 MQTT 设备页为准。下面假设 Cover 是 `cover.desk`：

```yaml
automation:
  - alias: Morning stand
    trigger:
      - platform: time
        at: "09:30:00"
    action:
      - service: cover.open_cover
        target:
          entity_id: cover.desk

  - alias: Evening sit
    trigger:
      - platform: time
        at: "18:30:00"
    action:
      - service: cover.close_cover
        target:
          entity_id: cover.desk
```

不要用 `cover.set_cover_position` 去任意毫米。固件不会执行百分比定位。紧急停止用 `cover.stop_cover`。

## 7. 排障

| 现象 | 先查什么 |
|---|---|
| MQTT 监听能看到 JSON，设备列表没有 Desk Gateway | 旧固件发的是 `homeassistant/device/<id>/config`，HA 只订 `cover/sensor/.../config`。烧当前固件，改听 `homeassistant/cover/<id>/config` |
| 只有涂鸦开关，没有桌子 | 回到 MQTT 集成的设备列表，不要停在涂鸦设备页；确认串口有 `discovery published homeassistant/cover/` |
| 连不上 Broker | 主机不要填 8123；用户必须是 Mosquitto 用户；STA 已拿到 IP |
| Cover 点了没动 | 打开「允许 MQTT 控制桌子」；童锁必须关；有人在桌旁；看 `desk-gateway/<id>/result` |
| 高度不对 | 产品高度是 TOF400C，不是控制盒数码管 |
| 想用 MQTT 解锁 | 不支持。去 Web / 手机解童锁 |

## 8. 不要做的事

- 不要在 ESP32 上再跑一套 Broker。
- 不要把 Broker 或 Desk Gateway 映射到公网。
- 不要用 Web 密码当 MQTT 密码。
- 不要给 command Topic 开 retain；固件收到 retained `SIT`/`STAND`/`STOP` 会拒绝。
- 不要把 Cover 当成窗帘百分比或长按升降。
- 不要把 Matter / 米家 / 华为智慧生活写成已经接好。
