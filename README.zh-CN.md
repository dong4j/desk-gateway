# Desk Gateway

**语言：** [English](./README.md) · 简体中文

开源的 **升降桌智能网关**（ESP32-S3）。厂商协议收进可插拔 **Desk Driver**；Web / 串口（以及后续 BLE / Matter / Home Assistant）共用控制面 `desk_core`。

> **安全：** 升降时请有人在旁。默认运动超时 15s。Web **仅限局域网**，不要做公网端口映射。

## 当前能力

- **Phase 1 — 模拟面板：** ESP32 作为 I²C Slave（地址 `0x24`）实现 `yourdesk_v1`
- **可插拔驱动：** `yourdesk_v1` 已实现；Loctek / Jiecang 为 stub
- **desk_core：** 按住升/降、停止、档位 1/4、童锁（NVS）、运动超时
- **Wi‑Fi + SoftAP 配网** 与带密码的 **局域网 Web**
- **SIM 高度：** 尚无 digit 嗅探时供界面演示

> 当前主固件已通过 ESP-IDF 6.0.2 编译，但真机升降、停止和板端 Web 仍需按验收清单验证；
> Phase 2 双 RJ45 中间人、原面板仲裁和童锁真屏蔽尚未实现。

## 硬件

| 项 | 建议 |
|----|------|
| 开发板 | YD-ESP32-S3 N16R8（或兼容 ESP32-S3） |
| 供电 | ESP32 用 USB-C；**不要**用桌子 3.3V 给 MCU 供电 |
| 地 | 与主机共地 |
| I²C（yourdesk_v1） | SCL → GPIO4，SDA → GPIO5 |

接线与验收：[docs/bringup-checklist.md](./docs/bringup-checklist.md)

## 快速开始

### 环境

- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) **5.2+**（开发环境为 **6.0.x**）
- 目标芯片：`esp32s3`

### 编译烧录

```bash
cd firmware/desk-gateway
idf.py set-target esp32s3
idf.py build
idf.py -p 串口 flash monitor
```

macOS 若用 Espressif Install Manager，先激活 IDF（例如 alias：`get-idf`）。

只做可重复编译检查时，可使用独立临时构建目录，避免旧 `build/` 缓存影响：

```bash
./scripts/check-firmware.sh
```

### Wi‑Fi（推荐 SoftAP）

无凭证时设备会开热点：

| | |
|--|--|
| SSID | `DeskGateway` |
| 密码 | `desk-gateway` |
| 配网页 | http://192.168.4.1/ |

配置家里的 **2.4GHz** Wi‑Fi 后，浏览器打开 `http://<设备IP>/`，默认 Web 密码：`desk-gateway`。

普通 Flash **不会**清 NVS；`idf.py erase-flash` 才会清空。

### 串口命令（调试）

`wifi <ssid> <pass>` · `up` / `down` / `stop` · `p1` / `p4` · `lock` / `unlock`

Web：升/降为 **按住运动、松手停止**（保持 DR，不连发）。

## 目录结构

```text
firmware/desk-gateway/        主固件（ESP-IDF）
firmware/phase1-panel-slave/  早期 Phase1 原型
docs/                         需求、架构、协议、UI Demo
LICENSE                       MIT
NOTICE                        第三方声明
```

## 文档

| 文档 | 说明 |
|------|------|
| [docs/0-requirements.md](./docs/0-requirements.md) | 需求 |
| [docs/architecture/overview.md](./docs/architecture/overview.md) | 架构总览 |
| [docs/bringup-checklist.md](./docs/bringup-checklist.md) | 到货 / 真机验收 |
| [docs/ui-demos/](./docs/ui-demos/) | Web 风格静态 Demo |
| [docs/3-protocol-reverse-notes.md](./docs/3-protocol-reverse-notes.md) | 协议逆向笔记 |

## 路线图

- [ ] 真实高度 digit 嗅探（关闭 `CONFIG_DESK_SIM_HEIGHT`）
- [ ] Phase 2 双 RJ45 MITM + 童锁真正屏蔽面板
- [ ] BLE 配件（OLED / 旋钮）
- [ ] Matter / Home Assistant
- [ ] 更多厂商 Driver

## 参与贡献

提交 Issue 或 PR 前，请阅读 [CONTRIBUTING.md](./CONTRIBUTING.md) 和
[CODE_OF_CONDUCT.md](./CODE_OF_CONDUCT.md)，并通过
[SUPPORT.md](./SUPPORT.md) 确认问题渠道和所需证据。

## 安全说明

见 [SECURITY.md](./SECURITY.md)。请修改默认 Web 密码，勿映射公网。

## 许可证

本项目采用 **MIT License**，见 [LICENSE](./LICENSE)。

ESP-IDF、cJSON 等第三方组件遵循其各自许可证，见 [NOTICE](./NOTICE)。

## 免责声明

与任何升降桌厂商无关联。协议笔记仅用于你拥有的设备上的互通。使用风险自负；家具运动可能导致人身或财物损伤。
