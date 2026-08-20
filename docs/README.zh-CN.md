# Desk Gateway 文档索引

**语言：** [English](./README.md) · 简体中文

按读者需要选入口。实现状态以
[当前状态与任务优先级](./status/current-status-and-priorities.md) 为准；
V1 是否能发布以 [V1 版本验收](./status/v1-release-acceptance.md) 为准。

Phase 1（模拟面板 + 平台骨架 + 多入口控桌）已经完成。现在可以用 Web、REST、串口、
BLE、手机、Watch、键盘、旋钮、语音和 Stream Deck 类按键操作同一张 Mxtark 升降桌。
Phase 2 原厂面板透传、断线 STOP、仲裁和童锁真屏蔽已在真桌通过。

## 目录怎么分

| 目录 | 放什么 |
|---|---|
| [`status/`](./status/current-status-and-priorities.md) | 活的事实：已完成 / 待验收 / V1 门禁 / 产品需求 |
| [`guides/`](./guides/control-methods.md) | 用法、部署、接线 |
| [`architecture/`](./architecture/overview.md) | **当前**系统怎么构成 |
| [`hardware/`](./hardware/esp32-s3-n16r8.md) | 开发板、抓包、I²C 排查 |
| [`future/`](./future/mqtt-home-assistant.md) | 已有方案、**尚未交付** |
| [`history/`](./history/mobile-app-technology-selection.md) | 冻结的设计稿和旧实施计划 |
| [`standards/`](./standards/README.zh-CN.md) | Agent / 提交规范 |
| [`images/`](./images/) | 协议和硬件笔记用的照片 |

架构图（只用 PNG）在 [`architecture/images/`](./architecture/images/)。

## 先看这些

| 你想做什么 | 文档 |
|---|---|
| 了解项目能做什么、怎么接线烧录 | 仓库根目录 [README.zh-CN.md](../README.zh-CN.md) |
| 在自己的局域网上把多端控桌跑起来 | [本地多端部署清单](./guides/local-multi-client-setup.md) |
| 用已有入口控桌 | [多种方式控制升降桌](./guides/control-methods.md) |
| 写脚本或接第三方工具 | [REST API](./guides/rest-api.md) |
| 接到 Home Assistant | [用 Home Assistant 控制升降桌](./guides/home-assistant-mqtt.md) |
| 接线、上电、排障 | [真机验收清单](./guides/bringup-checklist.md) |
| 看还剩哪些真机门禁 | [当前状态与任务优先级](./status/current-status-and-priorities.md) |

## 状态

| 文档 | 说明 |
|---|---|
| [当前状态与任务优先级](./status/current-status-and-priorities.md) | 已完成与待验收 |
| [V1 版本验收](./status/v1-release-acceptance.md) | 发布门禁（当前 **NO-GO**） |
| [需求](./status/requirements.md) | 范围、阶段门禁、已确认决策 |

## 使用说明

| 文档 | 说明 |
|---|---|
| [多种方式控制升降桌](./guides/control-methods.md) | Web、REST、串口、手机、Watch、键盘、旋钮、语音、Ulanzi、Home Assistant |
| [本地多端部署清单](./guides/local-multi-client-setup.md) | 改 IP、密码、仓库路径 |
| [REST API](./guides/rest-api.md) | 鉴权、运动、档位、童锁、Bond、番茄时钟 |
| [用 Home Assistant 控制升降桌](./guides/home-assistant-mqtt.md) | Mosquitto Discovery，Cover 请坐/起立/停止 |
| [真机验收清单](./guides/bringup-checklist.md) | 接线、供电、真机检查 |
| [键盘、旋钮与语音控制](./guides/keyboard-voice-control.md) | GoatRemote、Karabiner、旋钮 jog |
| [小智 AI 控桌](./guides/xiaozhi-ai-desk-control.md) | 语音到 MCP 再到 REST |
| [小智固件与本地 Server](./guides/xiaozhi-firmware-and-local-server.md) | 小智硬件侧部署 |
| [iOS 真机部署](./guides/mobile-ios-device-deployment.md) | iPhone Development Build |
| [Android 真机部署](./guides/mobile-android-device-deployment.md) | Android SDK / ADB |
| [三端正式发布](./guides/mobile-watch-production-release.md) | iPhone、Android、Watch 签名与内测 |
| [手机 App](../mobile/app/README.zh-CN.md) | React Native 能力与开发命令 |
| [Apple Watch](../mobile/watch/README.zh-CN.md) | watchOS 构建、签名、真机门禁 |
| [集成总览](../integrations/README.zh-CN.md) | 第三方入口与边界 |

## 架构

| 文档 | 说明 |
|---|---|
| [架构总览](./architecture/overview.md) | 分层、硬件拓扑、童锁、仲裁、当前边界 |
| [平台设计定稿](./architecture/platform-design.md) | 实现依据 |
| [协议逆向笔记](./architecture/protocol-reverse-notes.md) | mxtark I²C 键码与时序 |
| [BLE Accessory Profile](./architecture/ble-accessory-profile.md) | UUID、字节协议、LightBlue |
| [BLE 三客户端与 Bond](./architecture/ble-multi-client-bond-management.md) | 所有权、配对窗口、删除 Bond |
| [移动端双通道](./architecture/mobile-connection-transport.md) | BLE 优先、REST 回退 |
| [Apple Watch 控制方案](./architecture/apple-watch-control.md) | Crown 安全停止与双通道 |
| [双 ToF 安全策略](./architecture/tof-safety.md) | 高度、侧距、档位闭环 |
| [OLED 状态屏](./architecture/oled-status-display.md) | SSD1306 接线与页面 |
| [番茄语音提醒](./architecture/pomodoro-reminder.md) | 本地计时与 I2S 语音 |

## 硬件

| 文档 | 说明 |
|---|---|
| [主控选型](./hardware/esp32-s3-n16r8.md) | YD-ESP32-S3 N16R8 |
| [固件 README](../firmware/desk-gateway/README.zh-CN.md) | 编译、接线、面板代理、高度策略 |
| [逻辑分析仪抓包](./hardware/protocol-capture.md) | Phase 0 流程 |
| [高度数据解析与通信方案调整](./hardware/i2c-restoration.md) | 为何离开软件多地址、回到硬件 `@0x24`，高度改走 ToF |
| [原厂面板继电器旁路](./hardware/panel-bypass-relay.md) | 不拔网线切回原厂直连；方案已选定，真机未接线 |

## 后续生态

这些能力尚未纳入 V1。MQTT 固件和局域网 Home Assistant 操作说明已经有了；完整真桌安全矩阵仍见状态文档：

| 文档 | 说明 |
|---|---|
| [MQTT / Home Assistant](./future/mqtt-home-assistant.md) | Topic 契约与安全边界；操作说明见 [HA 接入指南](./guides/home-assistant-mqtt.md) |
| [小米 / 华为生态](./future/ecosystem-xiaomi-huawei.md) | 原生模组 vs Matter |

## 历史稿

不是用户手册，只保留设计上下文：

| 文档 | 说明 |
|---|---|
| [移动端技术选型](./history/mobile-app-technology-selection.md) | React Native / Expo 决策 |
| [M1/M2 实施计划](./history/plans/2026-08-06-desk-gateway-m1-m2.md) | 2026-08-06 任务计划 |
| [继电器旁路实施计划](./history/plans/2026-08-20-panel-bypass-relay.md) | 2026-08-20 硬件第一期；固件第二期未开工 |

## 工程规范

| 文档 | 说明 |
|---|---|
| [CHANGELOG](../CHANGELOG.md) | 未发布快照；V1 尚未打 tag |
| [参与贡献](../CONTRIBUTING.zh-CN.md) | IDF v6.0.2 安装、分层检查、PR 期望 |
| [安全政策](../SECURITY.zh-CN.md) | 如何私下报告漏洞 |
| [支持](../SUPPORT.zh-CN.md) | Issue 渠道和真机证据 |
| [开发规范](./standards/README.zh-CN.md) | Agent 强制规范入口 |
| [代码提交规范](./standards/git-commit-convention.md) | Conventional Commits 与暂存边界 |
