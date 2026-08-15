# Desk Gateway

**语言：** [English](./README.md) · 简体中文

开源的 **升降桌智能网关**（ESP32-S3）。厂商协议收进可插拔 **Desk Driver**；Web / 串口 / BLE（以及后续 Matter / Home Assistant）共用控制面 `desk_core`。

> **安全：** 升降时请有人在旁。当前 TOF400C 高度和 TOF050C 右侧间距已进入上升安全裁决：高度未知、达到最高高度，或高度低于 80 cm 且右侧间距未知/小于 8 cm 时禁止上升；下降和 STOP 始终可用。完整真机安全矩阵仍待验收。Web **仅限局域网**，不要做公网端口映射。

## 当前能力

- **Phase 1 — 模拟面板：** ESP32-S3 硬件 I²C Slave 只处理键地址 `0x24`
- **可插拔驱动：** `mxtark` 已实现；Loctek / Jiecang 为 stub
- **desk_core：** 按住升/降与停止；全局童锁与 REST/蓝牙/面板来源权限使用 NVS 保存
- **双 ToF 高度与侧距：** TOF400C 直接提供产品高度，TOF050C 提供桌面右侧间距；两路均完成防抖和失效检测
- **高度闭环：** 坐姿、站姿和最高安全高度跨 Web/App 同步并持久化，档位运动与上升限高已接入统一安全裁决
- **Wi‑Fi + SoftAP 配网** 与带密码的 **局域网 Web**
- **BLE Accessory Profile：** 最多三个 Central 同时在线、单一运动所有者、加密 Client Info、显式配对窗口和 Bond 管理，并保留长按租约、断连停止、档位命令与状态 Notify
- **高度状态一致：** Web/REST/BLE/OLED/原厂面板统一使用处理后的 TOF400C 距离；关闭 SIM 和控制盒 digit 高度解析

> 当前主固件已通过 ESP-IDF 6.0.2 编译。2026-08-10 补回原厂面板上的两只外部上拉后，
> Web 按住升/降与松手停止已通过真机验证。后续软件多地址 I²C 高度方案造成连续运动回归，
> 因此默认固件已回到提交 `3269faa` 验证过的硬件 `0x24` 路径。LightBlue 和 iPhone App
> 核心控制路径保留；当前双 ToF 已接入高度闭环和上升保护，完整真机安全矩阵仍待验收。
> 三客户端固件、Web/手机 Bond 管理及 Watch/手机 Client Info 已通过自动化，但 iPhone、
> Apple Watch、Android 三台真机矩阵仍未执行。还需完成异常停止矩阵、Android 验收，
> 以及双 RJ45 原面板透传、仲裁和童锁真屏蔽；
> 统一顺序见下方“当前状态与剩余任务优先级”文档。

## 硬件

| 项 | 建议 |
|----|------|
| 开发板 | YD-ESP32-S3 N16R8（或兼容 ESP32-S3） |
| 供电 | ESP32 用 USB-C；**不要**用桌子 3.3V 给 MCU 供电 |
| 地 | 与主机共地 |
| I²C（mxtark） | RJ45 pin 2 / 白线 CLK → GPIO4；pin 4 / 黑线 DAT → GPIO5 |
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
| [docs/5-current-status-and-priorities.md](./docs/5-current-status-and-priorities.md) | 当前状态与剩余任务优先级 |
| [docs/7-hardware-i2c-restoration-investigation.md](./docs/7-hardware-i2c-restoration-investigation.md) | 从软件 I²C 回退到硬件 I²C 的排查记录 |
| [docs/4-tof-distance-sensor-plan.md](./docs/4-tof-distance-sensor-plan.md) | 双 ToF 接线、防抖、档位与上升安全策略 |
| [docs/architecture/overview.md](./docs/architecture/overview.md) | 架构总览 |
| [docs/architecture/ble-accessory-profile.md](./docs/architecture/ble-accessory-profile.md) | BLE UUID、字节协议与 LightBlue 测试步骤 |
| [docs/architecture/ble-multi-client-bond-management.md](./docs/architecture/ble-multi-client-bond-management.md) | 三客户端所有权、配对窗口与 Bond 管理 |
| [docs/bringup-checklist.md](./docs/bringup-checklist.md) | 到货 / 真机验收 |
| [docs/ui-demos/](./docs/ui-demos/) | Web 风格静态 Demo |
| [docs/3-protocol-reverse-notes.md](./docs/3-protocol-reverse-notes.md) | 协议逆向笔记 |

## 路线图

- [x] 恢复硬件 I²C Slave `@0x24` 作为稳定运动链路
- [x] LightBlue 和 iPhone App 已在真桌完成 BLE 核心控制
- [x] 手机 App 已实现 BLE 优先、REST 回退和设备设置同步
- [x] 实现 BLE 三连接所有权、Client Info、配对窗口和 Web/手机 Bond 管理
- [ ] Phase 2 双 RJ45 MITM 透传恢复 + 面板权限/童锁真机验收
- [ ] 完成异常停止矩阵和 Android 真机验收
- [x] 接入 TOF400C 高度与 TOF050C 右侧间距，恢复档位闭环和最高安全高度
- [x] 实现 Apple Watch App 与多客户端握手（真机验收仍开放）
- [ ] 完成 iPhone、Apple Watch、Android 三台真机并发矩阵
- [ ] 完成双 ToF 档位、最高高度和右侧障碍物真机安全矩阵
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
