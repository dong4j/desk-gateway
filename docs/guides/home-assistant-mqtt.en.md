# Control the desk from Home Assistant

| Item | Value |
|---|---|
| Date | 2026-08-19 |
| For | Desk Gateway firmware with the MQTT client, plus a LAN Home Assistant + Mosquitto |
| Protocol contract | [MQTT / Home Assistant design](../future/mqtt-home-assistant.md) (Chinese) |
| REST config | [REST API](rest-api.md) (Chinese) |

**Language:** 中文见 [home-assistant-mqtt.md](home-assistant-mqtt.md).

Desk Gateway is an **MQTT client** on your existing LAN broker. It does not run a broker on the ESP32 and it does not speak Matter. Home Assistant should create one **Desk Gateway** device through MQTT Discovery. Do not add an MQTT device by hand, and do not write YAML for discovery.

V1 is still **NO-GO**. This page is a LAN how-to, not a release note. The remaining safety matrix (child-lock, panel preemption, no replay after disconnect) lives in [current status](../status/current-status-and-priorities.md).

```text
Home Assistant  ←→  LAN Mosquitto (1883)
                         ↑
                  Desk Gateway MQTT client
                         ↓
                      desk_core
```

## 1. What you get

The MQTT integration shows one device **Desk Gateway** with three entities:

| HA entity | Role | Constraint |
|---|---|---|
| Cover: Desk | Open = stand (preset 4), close = sit (preset 1), stop = global STOP | No hold-to-move, no arbitrary percent position |
| Sensor: Height | Product height in mm | TOF400C, not the controller’s digit display |
| Binary sensor: Child lock | Read-only | MQTT **cannot** unlock the desk |

Cover open/close is a preset closed loop, not a curtain travel. A missing position slider is expected: firmware does not implement `set_position`.

## 2. Prerequisites

- Desk Gateway is on STA and the Web settings page loads.
- Home Assistant has the **Mosquitto broker** add-on (or an equivalent LAN broker) on **1883** (plain) or **8883** (TLS).
- The **MQTT** integration is already added and can discover other MQTT devices.
- Do not port-forward 1883, 8883, or 8123.
- Keep a person next to the desk when testing motion. Be ready to hit Cover stop or cut controller power.

`http://<ha-ip>:8123/` is the UI, not the broker. Broker host is the HA LAN IP or hostname; port is **1883**.

## 3. Dedicated Mosquitto user

Do not reuse the Desk Gateway Web password or the Home Assistant login.

On Home Assistant OS:

1. Settings → Add-ons → **Mosquitto broker** → Configuration.
2. Add a user used only by this desk, for example `desk-gateway`.
3. Save and restart Mosquitto.
4. Confirm the MQTT integration still points at this broker (often `core-mosquitto`).

If ACL is on, allow at least:

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

Replace `<id>` with the 12-character lowercase MAC, for example `907069ef299c`. The device ID is on the Web MQTT page or `GET /api/v1/mqtt`. It is not user-editable.

## 4. Fill MQTT on Desk Gateway

Open the gateway Web settings, or `PUT /api/v1/mqtt`.

| Field | Value |
|---|---|
| Broker host | HA LAN IP, for example `192.168.21.28`. **Do not** append `:8123` |
| Port | `1883` (`8883` when TLS uses the certificate bundle) |
| TLS | `none` on a trusted LAN |
| Username / password | The Mosquitto user from the previous section, not the Web password |
| Discovery prefix | Default `homeassistant`; must match the HA MQTT integration |
| Connect to broker | On → connect, publish state, publish Discovery |
| Allow MQTT to move the desk | Separate switch. Client-on / control-off is read-only monitoring |

After save, serial should show `desk_mqtt: connected`, then:

```text
discovery published homeassistant/cover/<id>/config
discovery published homeassistant/sensor/<id>_height/config
discovery published homeassistant/binary_sensor/<id>_child_lock/config
```

The client connects only in STA. SoftAP provisioning does not connect to the broker.

## 5. Confirm the device in Home Assistant

1. Settings → Devices & services → **MQTT**.
2. Open the **Devices** list. **Desk Gateway** should appear. Do not stay on an existing Tuya device card.
3. Do not “add MQTT entry” or create a Cover by hand.
4. Turn on “Allow MQTT to move the desk” on the gateway before using Cover.
5. Cover: **open = stand, close = sit, stop = STOP**. Accepted command ≠ desk arrived.

Firmware subscribes to `homeassistant/status` and republishes Discovery after HA birth. Discovery is **not** retained. If HA starts after a long outage, wait for the gateway to reconnect; do not write YAML.

## 6. Automation example

Use the entity ID from the MQTT device page. This assumes `cover.desk`:

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

Do not use `cover.set_cover_position` for an arbitrary millimetre target. Emergency stop is `cover.stop_cover`.

## 7. Troubleshooting

| Symptom | Check |
|---|---|
| Listen shows JSON, MQTT device list has no Desk Gateway | Older firmware published `homeassistant/device/<id>/config`. Flash current firmware and listen to `homeassistant/cover/<id>/config` |
| Only a Tuya switch appears | Go back to the MQTT integration device list; confirm serial `discovery published homeassistant/cover/` |
| Cannot connect | Host is not port 8123; user is a Mosquitto user; STA has an IP |
| Cover does nothing | Enable MQTT control; child-lock off; person at the desk; read `desk-gateway/<id>/result` |
| Height looks wrong | Product height is TOF400C |
| Unlock via MQTT | Not supported. Unlock from Web or the phone app |

## 8. Do not

- Run a broker on the ESP32.
- Port-forward the broker or Desk Gateway.
- Reuse the Web password as the MQTT password.
- Retain the command topic; retained `SIT` / `STAND` / `STOP` is rejected.
- Treat Cover as a percent curtain or hold-to-move control.
- Claim Matter / Xiaomi / Huawei as done.
