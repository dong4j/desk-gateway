# Local multi-client setup checklist

| Item | Value |
|---|---|
| Date | 2026-08-19 |
| Goal | Flash Desk Gateway, join your LAN, and point Web / scripts / phone / Watch / keyboard / voice / D200H at the same desk |
| How to move the desk | [Control methods](control-methods.md) (Chinese) |
| REST contract | [REST API](rest-api.md) (Chinese) |
| Wiring and debug | [Bring-up checklist](bringup-checklist.md) (Chinese) |

This page answers one question: **after you clone the repo, which IP, password, and path values must you change before anything will move the desk on your LAN.**  
Wiring, BLE byte protocol, and per-client UI live in the links above.

Stay next to the desk when testing motion. Be ready to send STOP or cut controller power. Keep HTTP on the LAN. Do not port-forward the gateway.

**Language:** 中文见 [local-multi-client-setup.md](local-multi-client-setup.md).

---

## 1. Write down three values

Every client below uses these three. Fill them in first:

| Name | What you put | Example currently in this repo (do not copy blindly) |
|---|---|---|
| Device URL | `http://<device-ip>` or a working `http://desk-gateway.local` | Script uses `http://192.168.21.65` |
| REST / Web password | Current Web login password, also `X-Desk-Key` | Factory default `desk-gateway`; script currently has `1024` |
| Repo root | **Absolute** path to this checkout on this computer | `/Users/dong4j/Developer/1.AI/ai-incubator/desk-gateway` |

How to get the device IP:

1. Serial log after flash, or the router DHCP list.
2. Firmware advertises mDNS at `http://desk-gateway.local/`. If the computer or phone cannot resolve `.local`, use the DHCP IP.
3. DHCP can change the IP after a router reboot. When it does, update every REST client in section 3.

Password rules:

- SoftAP and first Web login both default to `desk-gateway`.
- Change it in Web settings after login.
- **After you change it**, the script, `X-Desk-Key`, phone, Watch, XiaoZhi `.env`, and D200H must all use **that same new password**.
- `scripts/desk-preset.sh` does not follow Web password changes by itself. It only uses the `DESK_KEY` written in the file.

---

## 2. Firmware once

1. Wire per the [firmware README](../../firmware/desk-gateway/README.md). Phase 1 at least: controller CLK/DAT on GPIO4/5. Do **not** tie RJ45 3.3V to ESP32 `3V3`.
2. Install [ESP-IDF v6.0.2](https://docs.espressif.com/projects/esp-idf/). Target `esp32s3`. Do not mix IDF versions.
3. Full flash (includes the voice partition, keeps NVS):

```bash
./scripts/flash-firmware.sh /dev/cu.usbmodemXXXX
```

If you are not using that wrapper, activate IDF then `cd firmware/desk-gateway && idf.py -p PORT flash monitor`. `app-flash` alone does not update `audio.bin`.

4. With no Wi-Fi credentials the device opens SoftAP:

| Item | Value |
|---|---|
| SSID | `DeskGateway` |
| Password | `desk-gateway` |
| Setup page | http://192.168.4.1/ |

Join a **2.4 GHz** home network. Then open `http://<device-ip>/`, log in with `desk-gateway`, and change the password.

5. On serial, confirm stop / up / down (full line, then Enter). Web up/down is **hold to move, release to stop**.
6. In Web settings: child-lock off; enable `REST` / `Bluetooth` / `Panel` for the clients you will use. Disabling a source stops current motion first.

LAN Web can move the desk at this point. Next, point every other client at **the same device and the same password**.

---

## 3. What to change (do this table)

Put the three values from section 1 into the matching rows. Skip clients you will not use. **Karabiner, the knob, and GoatRemote all call the shell script**, so that file is required if you use any of them.

| Client | Where | What to edit | Must equal |
|---|---|---|---|
| Shared script | [`scripts/desk-preset.sh`](../../scripts/desk-preset.sh) lines 8–9 | `DESK_BASE_URL`, `DESK_KEY` | `http://<device-ip>`, current Web password |
| Karabiner | [`integrations/karabiner/desk-gateway.json`](../../integrations/karabiner/desk-gateway.json) | All six `shell_command` repo paths | `<repo-root>/scripts/desk-preset.sh …` |
| GoatRemote commands | GoatRemote custom commands | Two `shell` command paths | Same script with `1` / `4` |
| GoatRemote prompt | [`integrations/goatremote/prompt_template_completion_xml_desk.txt`](../../integrations/goatremote/prompt_template_completion_xml_desk.txt) | Example absolute paths in the file | Your repo path before import |
| Phone app | App → Settings | REST URL, `X-Desk-Key` | IP or `desk-gateway.local`, Web password |
| Apple Watch | Connection settings → Wi-Fi | Gateway URL, REST password | Same; password goes in Watch Keychain |
| XiaoZhi MCP | [`integrations/xiaozhi-mcp/.env`](../../integrations/xiaozhi-mcp/.env.example) | `DESK_GATEWAY_URL`, `DESK_GATEWAY_KEY`, MCP pipe paths | URL/Key match Web; do not commit `.env` |
| XiaoZhi keep-alive | `launchd` plist | `__PROJECT_ROOT__`, `__HOME__` | Absolute paths on this Mac |
| Ulanzi D200H | Any key’s property inspector | Gateway URL, `X-Desk-Key` | Shared across the three keys; not stored in source |
| BLE phone / Watch / LightBlue | Web or App settings | Open the 120 s pairing window | Advertises as `DeskGateway`; no IP needed to move the desk |
| curl / your own scripts | Request header | `X-Desk-Key` | Current Web password |

IP, password, and absolute paths in the tree are whatever works on the maintainer’s LAN. **Change them to yours after clone.** This is not a sanitization issue; DHCP and checkout paths differ on every machine.

---

## 4. Bring each client up

### 4.1 Shared script (required for keyboard / knob / voice)

Edit `scripts/desk-preset.sh`:

```sh
DESK_BASE_URL='http://<device-ip>'
DESK_KEY='<current Web password>'
```

Prove REST before Karabiner:

```bash
curl -s -H "X-Desk-Key: <current Web password>" "http://<device-ip>/api/v1/desk/status"
./scripts/desk-preset.sh stop
```

`status` should show `height_known` / `tof_height_known` true, `child_lock` false, `control_sources.rest` true. Then presets (someone at the desk):

```bash
./scripts/desk-preset.sh 1    # sit 550 mm
./scripts/desk-preset.sh 4    # stand 870 mm
./scripts/desk-preset.sh stop
```

`up` / `down` are knob jogs, not Web holds. A single call often only arms; repeated ticks produce visible motion.

### 4.2 Karabiner shortcuts and knob

1. Finish 4.1. Karabiner only fires keys; IP and password live in the script.
2. Replace all six `/Users/dong4j/Developer/1.AI/ai-incubator/desk-gateway/scripts/desk-preset.sh` strings with `<repo-root>/scripts/desk-preset.sh`.
3. Install:

```bash
cp integrations/karabiner/desk-gateway.json \
  ~/.config/karabiner/assets/complex_modifications/desk-gateway.json
```

4. Enable both rules in **Complex Modifications**: presets `⌃⌥⇧ + 1/2/3`, and knob `F18`/`F17`/`F16`.
5. Configure the knob to repeat those keys. Do not emulate hold.

Details: [keyboard-voice-control.md](keyboard-voice-control.md), [Karabiner README](../../integrations/karabiner/README.md).

### 4.3 GoatRemote

1. Finish 4.1.
2. Two custom commands, action type `shell`:

| When I say | Command |
|---|---|
| 桌子坐姿 | `<repo-root>/scripts/desk-preset.sh 1` |
| 桌子站姿 | `<repo-root>/scripts/desk-preset.sh 4` |

3. If you import the prompt template, replace the absolute paths in that file first.

### 4.4 LAN Web

Open `http://<device-ip>/` or `http://desk-gateway.local/`. Hold to move, release to stop. Settings change height, child-lock, source ACL, and bonds. The login password **is** the REST key for every other HTTP client.

### 4.5 iPhone / Android

1. Do not use Expo Go. Install a Development Build: [iOS](mobile-ios-device-deployment.md) or [Android](mobile-android-device-deployment.md).
2. Phone and gateway on the same LAN (REST fallback and bond management need it).
3. Settings: REST URL and `X-Desk-Key` (current Web password). Auto prefers BLE and falls back to REST.
4. First BLE pair: open the **120 s pairing window** on authenticated Web or an already-paired app, scan `DeskGateway`, accept system pairing. iPhone writes Client Info `01 02`, Android `01 03`. Do not handshake with STOP.
5. Hold to move, release STOP. Bond delete, pairing window, and Pomodoro duration still go over REST, so the URL and password in Settings must be correct even when you control the desk over BLE.

### 4.6 Apple Watch

1. Sign and install per the [Watch README](../../mobile/watch/README.md). Simulator mock is not hardware acceptance.
2. BLE: open the 120 s window; Watch writes Client Info `01 01`.
3. Wi-Fi: Watch must reach the gateway LAN; store URL and REST password in connection settings. Crown uses the jog lease.
4. With three clients online, a non-owner sees “another device is controlling”. STOP still works.

### 4.7 XiaoZhi AI (optional)

Bridge code: `integrations/xiaozhi-mcp/`. Desk Gateway does **not** need a public URL.

```bash
cp integrations/xiaozhi-mcp/.env.example integrations/xiaozhi-mcp/.env
chmod 600 integrations/xiaozhi-mcp/.env
```

Set at least:

| Variable | Value |
|---|---|
| `MCP_ENDPOINT` | Full XiaoZhi agent WebSocket URL including token |
| `MCP_PIPE_DIR` / `MCP_PYTHON` | Local `78/mcp-calculator` checkout and venv |
| `DESK_GATEWAY_URL` | `http://<device-ip>` |
| `DESK_GATEWAY_KEY` | Current Web password |

Before motion, `GET /api/v1/desk/status` must show known ToF height, `raise_to_max_supported` true, child-lock off, REST source on. The five tools are fixed. Do not add “move to N mm”.

For keep-alive, copy the `launchd` template and replace `__PROJECT_ROOT__` and `__HOME__`. See the [XiaoZhi MCP README](../../integrations/xiaozhi-mcp/README.md).

### 4.8 Ulanzi D200H (optional)

Do not drop source into UlanziStudio. Build with the official SDK per the [plugin README](../../integrations/ulanzi-d200h/README.md). Then set gateway URL and `X-Desk-Key` in the property inspector. The three keys share that config. Computer, UlanziStudio, and the gateway must reach each other; leave UlanziStudio running.

### 4.9 Original panel (Phase 2)

Wiring is in the firmware README dual-RJ45 table: controller GPIO4/5, panel GPIO6/7. Do **not** jumper CLK/DAT across the two sockets. Panel keys use the same child-lock and arbiter. Preset keys 2 / 3 are still not an accepted safe-height path.

### 4.10 Home Assistant / MQTT (optional)

How-to: [Control the desk from Home Assistant](home-assistant-mqtt.en.md). Fill in a LAN broker (HA Mosquitto is the intended target) on the Web settings page. Use a dedicated MQTT user, not the Web password. Do not port-forward the broker or 1883/8883. The client and MQTT motion source both default to off. After the client is on, HA should Discovery a Cover; after MQTT control is on, Cover open/close/stop map to stand/sit/STOP.

---

## 5. One-pass acceptance

Someone at the desk:

- [ ] `GET /api/v1/desk/status` returns live height, `child_lock=false`, `control_sources.rest=true`
- [ ] Web hold up/down, release stops immediately
- [ ] `./scripts/desk-preset.sh 1` / `4` / `stop` matches Web height
- [ ] After rewriting Karabiner paths, a shortcut or knob can stop and hit a preset (if enabled)
- [ ] Phone or Watch BLE sees `DeskGateway`; after pairing, hold/Crown can stop
- [ ] Child-lock on: nothing except STOP can start motion
- [ ] No public port forward

401 on any client: that client’s password is not the current Web password. 403: child-lock or source ACL.

---

## 6. Still not moving

| Symptom | Check first |
|---|---|
| Script or curl `401` | `DESK_KEY` / `X-Desk-Key` is not the current Web password |
| `403` `child_lock` / `source_disabled` | Unlock child-lock; enable that source |
| `.local` does not open | Use the DHCP IP everywhere: script, App, Watch, `.env`, D200H |
| Karabiner does nothing | JSON still points at someone else’s checkout; rules not enabled; script REST already failing |
| Phone never finds the desk | Pairing window closed, three Centrals already up, permission/location, stale Bond |
| “Another device is controlling” | This client is not the motion owner; STOP still works |
| Will not go up | ToF height unknown, at ceiling, or below 800 mm with right gap unknown/too small; down and STOP should still work |
| Everything died after DHCP | Update every REST URL in section 3 |

Integrations index: [integrations/README.md](../../integrations/README.md).
