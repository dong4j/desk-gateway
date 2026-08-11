# Desk Gateway

**语言：** [English](./README.md) · 简体中文

开源的 **升降桌智能网关**（ESP32-S3）。厂商协议收进可插拔 **Desk Driver**；Web / 串口（以及后续 BLE / Matter / Home Assistant）共用控制面 `desk_core`。

> **安全：** 升降时请有人在旁。默认运动超时 15s。Web **仅限局域网**，不要做公网端口映射。

## 当前能力

- **Phase 1 — 模拟面板：** 软件 I²C Slave 同时处理键地址 `0x24` 与高度 digit `0x34–0x37`
- **可插拔驱动：** `yourdesk_v1` 已实现；Loctek / Jiecang 为 stub
- **desk_core：** 按住升/降与停止已真机通过；档位 1（64 cm）/4（102 cm）已按真实高度闭环实现；全局童锁与 REST/蓝牙/面板来源权限使用 NVS 保存
- **最高安全高度：** Web 可配置并保存到 NVS，默认 `102 cm`；独立看门狗会在高度帧稀疏时按最坏上升速度提前停止
- **Wi‑Fi + SoftAP 配网** 与带密码的 **局域网 Web**
- **真实高度：** 仅接受严格有序的 TM1650 完整显示帧；正常的稀疏刷新期间保留最后可信高度
- **自动重新同步：** 每次运动的第一帧完整控制盒高度自动成为新基准，不再允许人工或模拟高度覆盖

> 当前主固件已通过 ESP-IDF 6.0.2 编译。2026-08-10 补回原厂面板上的两只外部上拉后，
> Web 按住升/降与松手停止已通过真机验证；统一软件多地址 Slave 随后通过升降与 `64–80 cm`
> 真实高度跟踪。档位闭环、预测限高、超时/重启安全和全局童锁仍待真机验收。
> 原面板仲裁与权限屏蔽代码已经实现，但双 RJ45 透传仍需面包板抓包和真机验收。

## 硬件

| 项 | 建议 |
|----|------|
| 开发板 | YD-ESP32-S3 N16R8（或兼容 ESP32-S3） |
| 供电 | ESP32 用 USB-C；**不要**用桌子 3.3V 给 MCU 供电 |
| 地 | 与主机共地 |
| I²C（yourdesk_v1） | RJ45 pin 2 / 白线 CLK → GPIO4；pin 4 / 黑线 DAT → GPIO5 |
| 上拉 | RJ45 pin 1 / 红线 3.3V 分别经 **2 kΩ（可用 2.2 kΩ）** 接 CLK、DAT |

红线只作为两个上拉电阻的电源端，**不得直接连接 ESP32 的 `3V3`**。拔掉原厂面板也会移除面板上
实测为 `1.99 kΩ` 的两只上拉，因此 ESP32 替代面板时必须补回。

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

- [x] 用多地址 I²C Slave 替换已否决的 GPIO 嗅探，实现真实高度
- [ ] Phase 2 双 RJ45 MITM 透传恢复 + 面板权限/童锁真机验收
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
