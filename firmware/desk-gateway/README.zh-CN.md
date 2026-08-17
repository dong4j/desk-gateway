# Desk Gateway 固件

**语言：** [English](./README.md) · 简体中文

升降桌网关的 ESP-IDF 工程。上级文档：[../../README.md](../../README.md) · [中文](../../README.zh-CN.md)。Web / REST / BLE / 手机 / Watch / 键盘 / 语音 / D200H 的用法见 [多种方式控制升降桌](../../docs/guides/control-methods.md)。REST 契约见 [REST API](../../docs/guides/rest-api.md)。

## 构建

```bash
# 先在同一个 Shell 激活本项目固定的 ESP-IDF v6.0.2，然后：
cd firmware/desk-gateway
# 拉过 audio 分区变更后先跑一次 set-target，避免旧 sdkconfig 仍指向 1500 KiB 分区表
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

需要 ESP-IDF v6.0.2。Component Manager 首次构建会拉取 `espressif/cjson`。

默认配置启用原生 NimBLE 外设，广播名为 `DeskGateway`。BLE 命令需要 Just Works 加密/绑定连接。UUID 和 LightBlue 步骤见 [ble-accessory-profile.md](../../docs/architecture/ble-accessory-profile.md)。

日常完整烧录（含 `audio.bin`、保留 NVS）用仓库根目录的 `./scripts/flash-firmware.sh <串口>`。

## 配网

| SoftAP | 值 |
|--------|--------|
| SSID | `DeskGateway` |
| 密码 | `desk-gateway` |
| 配网页 | http://192.168.4.1/ |

局域网 Web 默认密码：`desk-gateway`。

## 接线（mxtark，Phase 1 模拟面板）

| RJ45 | 信号 | 连接 |
|-------|--------|------------|
| pin 1 / 红 | 桌子 3.3V | 只作为上拉电源；**不要接到 ESP32 3V3** |
| pin 2 / 白 | CLK | GPIO4 |
| pin 3 / 绿 | GND | ESP32 GND |
| pin 4 / 黑 | DAT | GPIO5 |

拔掉原厂 TM1650 面板也会拿掉实测约 `1.99 kΩ` 的两只上拉。测试前补两只无极性电阻：

```text
红线 3.3V ── 2 kΩ（可用 2.2 kΩ）── 白线 CLK / GPIO4
红线 3.3V ── 2 kΩ（可用 2.2 kΩ）── 黑线 DAT / GPIO5
绿线 GND ───────────────────────────── ESP32 GND
```

电阻是上拉，不是串联：白线和黑线仍直接接到对应 GPIO。ESP32 用 USB 独立供电。

验收清单：[docs/bringup-checklist.md](../../docs/bringup-checklist.md)

## 番茄语音提醒（MAX98357A）

默认固件带 ESP 本地番茄时钟和中文 WAV 语音。I2S 功放接线：

| ESP32-S3 | MAX98357A |
|----------|-----------|
| USB 侧 5V | VIN |
| GND | GND |
| GPIO14 | BCLK |
| GPIO15 | LRC / WS |
| GPIO16 | DIN |

4 Ω / 3 W 喇叭接 `SPK+` / `SPK-`，喇叭端子都不要接 GND。不要用桌子 RJ45 的 3.3V 给功放供电。

16 kHz / 16-bit / Mono WAV 包在独立的 4 MiB `audio` SPIFFS 分区。构建后必须完整 `idf.py flash`，把 `audio.bin` 烧到 `0x310000`；只跑 `idf.py app-flash` 不会更新语音包。第一次上电建议 20% 音量。固件和自动化测试已完成，音质、欠压、爆音和桌面总线 EMI 要等真机功放和喇叭验收。

## 接线（mxtark，Phase 2 原厂面板代理）

转接板上两个 RJ45 彼此独立。控制盒 CLK/DAT 走稳定的 ESP32-S3 硬件 Slave；原厂面板 CLK/DAT 走隔离的 GPIO 软件 Master：

| 信号 | 左插座 / 控制盒侧 | 右插座 / 原厂面板侧 |
|--------|-------------------------------|------------------------------------|
| pin 1 / 红 / 3.3V | 控制盒红线；原有 2 kΩ 上拉留在这边 | 直接跳到左侧 pin 1 |
| pin 2 / 白 / CLK | GPIO4 | GPIO6 |
| pin 3 / 绿 / GND | ESP32 GND | 直接跳到左侧 pin 3 |
| pin 4 / 黑 / DAT | GPIO5 | GPIO7 |

**不要**把左 pin 2 跳到右 pin 2，或左 pin 4 跳到右 pin 4：那会绕过 ESP32 事务代理。面板侧不要再补一对上拉；原厂面板 CLK/DAT 到 3.3V 已测约 `1.99 kΩ`。红线跳线只把控制盒 3.3V 送给原厂面板供电，仍然不得接到 ESP32 `3V3`。

`CONFIG_DESK_MXTARK_PANEL_PROXY=y` 时，控制盒侧仍是 GPIO4/5 上的硬件 I2C Slave `@0x24`。GPIO6/7 以约 9.6 kHz 开漏软件 Master 复放抓到的 TM1650 事务，把原厂按键送进现有仲裁器，并在原厂面板上显示校准后的 TOF400C 高度。物理面板键优先于 Web 运动；Panel 权限和童锁走同一套 `desk_core` 策略。面板超时或断线立即发布为空闲，避免运动被锁住。

原始 `idle_12mhz_full.sr` 抓包是两次 STOP 分隔的事务，约 `9.6 kHz`：写 `0x48/0x01`，等约 `29 us`，再读 `0x49/DR`，以控制器 `ACK + STOP` 结束；下一次写大约 `95 us` 后开始。ESP-IDF 6 的 Master API 会合并写/读并强制标准 `NACK + STOP`，无法复放该序列。面板侧代理用隔离开漏 GPIO 实现，NACK 或超时时总是尝试 STOP。GPIO6/7 不得与其他外设共用。

原厂面板档位键 2 / 3 **仍不是**已验收的安全高度路径。上升、下降、松开、TOF400C 高度显示、断线停止、仲裁和童锁真屏蔽已在真桌通过。

## 高度状态

产品固件故意不解析控制盒的 TM1650 digit 写入。真桌诊断发现软件多地址 Slave 经常打断 `0x24` 键应答，控制器会间歇丢 `0x47` 或 `0x4F`，尽管应用状态仍显示在运动。

因此默认构建只用 ESP32-S3 硬件 I²C Slave 的 `0x24`，并打印：

```text
I (...) mxtark: control-box height input disabled; waiting for external TOF source
I (...) mxtark: I2C slave @0x24 SCL=4 SDA=5
```

历史上的控制盒 digit 解码器和软件 I²C Slave 只在显式实验 Kconfig 后面，不会编进默认组件源列表。GPIO10/11 上的独立 TOF400C 现在提供滤波后的控制高度，并供给 Web / OLED / 原厂面板显示。TOF050C 只在高度低于 `800 mm` 且右侧间距低于 `80 mm` 时禁止上升；`940 mm` 上限始终生效。控制盒侧回退证据见 `docs/7-hardware-i2c-restoration-investigation.md`。

## 最高安全高度

已鉴权 Web 设置页把 `min_height_mm` 和 `max_height_mm` 存进 NVS。默认 `550 mm` 和 `940 mm`，物理输入范围 `550–940 mm`。最低高度只校验档位输入，因为下止点由控制盒自己处理。档位 1/4 默认 `550 mm` 和 `870 mm`。这些值直接使用滤波后的 TOF400C 距离，不换算成卷尺桌面高度。它们在 Web、BLE 和手机 App 之间持久化并同步。TOF400C 不可用时禁止上升；低于 `800 mm` 时 TOF050C 不可用也禁止上升。

需要明确升到最高安全位的已鉴权自动化使用 `POST /api/v1/desk/raise-to-max`。仅当当前 Driver 声明带真实高度反馈的设备侧有界动作时才开放；不会退化为手动 `/api/v1/desk/up` 长按。对外语音或 MCP 工具暴露该动作前，先查 `GET /api/v1/desk/status` 里的 `raise_to_max_supported`。

## 许可证

MIT — 见仓库根目录 [LICENSE](../../LICENSE)。
