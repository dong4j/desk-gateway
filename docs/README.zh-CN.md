# Desk Gateway 文档索引

**语言：** [English](./README.md) · 简体中文

按读者需要选入口。实现状态以 [当前状态与任务优先级](./5-current-status-and-priorities.md) 为准；V1 是否能发布以 [V1 版本验收](./12-v1-release-acceptance.md) 为准。

Phase 1（模拟面板 + 平台骨架 + 多入口控桌）已经完成。现在可以用 Web、REST、串口、BLE、手机、Watch、键盘、旋钮、语音和 Stream Deck 类按键操作同一张 Mxtark 升降桌。Phase 2 原厂面板透传、断线 STOP、仲裁和童锁真屏蔽已在真桌通过。

## 先看这些

| 你想做什么 | 文档 |
|---|---|
| 了解项目能做什么、怎么接线烧录 | 仓库根目录 [README.zh-CN.md](../README.zh-CN.md) |
| 在自己的局域网上把多端控桌跑起来 | [本地多端部署清单](./guides/local-multi-client-setup.md) |
| 用已有入口控桌 | [多种方式控制升降桌](./guides/control-methods.md) |
| 写脚本或接第三方工具 | [REST API](./guides/rest-api.md) |
| 接线、上电、排障 | [真机验收清单](./bringup-checklist.md) |
| 看还剩哪些真机门禁 | [当前状态与任务优先级](./5-current-status-and-priorities.md) |

## 使用与接入

| 文档 | 说明 |
|---|---|
| [多种方式控制升降桌](./guides/control-methods.md) | Web、REST、串口、手机、Watch、键盘、旋钮、语音、Ulanzi |
| [本地多端部署清单](./guides/local-multi-client-setup.md) | 改 IP、密码、仓库路径，把各端指到同一台网关 |
| [REST API](./guides/rest-api.md) | 鉴权、运动、档位、童锁、Bond、番茄时钟 |
| [键盘、旋钮与语音控制](./keyboard-voice-control.md) | GoatRemote、Karabiner、旋钮 jog |
| [小智 AI 控桌](./11-xiaozhi-ai-desk-control.md) | 语音到 MCP 再到 REST |
| [小智固件与本地 Server](./10-xiaozhi-ai-firmware-and-local-server.md) | 小智硬件侧部署 |
| [Ulanzi D200H 插件](../integrations/ulanzi-d200h/README.zh-CN.md) | 请坐 / 站立 / 番茄时刻 |
| [小智 MCP 桥接](../integrations/xiaozhi-mcp/README.zh-CN.md) | 云端 Endpoint 到本机桥接 |
| [键盘 / 旋钮 Karabiner](../integrations/karabiner/README.zh-CN.md) | Complex Modifications |
| [GoatRemote](../integrations/goatremote/README.zh-CN.md) | 语音坐姿 / 站姿 |
| [集成总览](../integrations/README.zh-CN.md) | 第三方入口与边界 |

## 移动端

| 文档 | 说明 |
|---|---|
| [手机 App](../mobile/app/README.zh-CN.md) | React Native 能力与开发命令 |
| [iOS 真机部署](./guides/mobile-ios-device-deployment.md) | iPhone Development Build |
| [Android 真机部署](./guides/mobile-android-device-deployment.md) | Android SDK / ADB |
| [三端正式发布](./guides/mobile-watch-production-release.md) | iPhone、Android、Watch 签名与内测 |
| [Apple Watch](../mobile/watch/README.zh-CN.md) | watchOS 构建、签名、真机门禁 |

## 架构与协议

| 文档 | 说明 |
|---|---|
| [架构总览](./architecture/overview.md) | 分层、硬件拓扑、童锁、仲裁、当前边界 |
| [平台设计定稿](./superpowers/specs/2026-08-06-desk-gateway-platform-design.md) | 实现依据 |
| [协议逆向笔记](./3-protocol-reverse-notes.md) | mxtark I²C 键码与时序 |
| [BLE Accessory Profile](./architecture/ble-accessory-profile.md) | UUID、字节协议、LightBlue |
| [BLE 三客户端与 Bond](./architecture/ble-multi-client-bond-management.md) | 所有权、配对窗口、删除 Bond |
| [移动端双通道](./architecture/mobile-connection-transport.md) | BLE 优先、REST 回退 |
| [Apple Watch 控制方案](./architecture/apple-watch-control.md) | Crown 安全停止与双通道 |
| [双 ToF 安全策略](./4-tof-distance-sensor-plan.md) | 高度、侧距、档位闭环 |

## 硬件与固件

| 文档 | 说明 |
|---|---|
| [需求](./0-requirements.md) | 范围、阶段门禁、已确认决策 |
| [主控选型](./2-esp32-s3-n16r8-platform.md) | YD-ESP32-S3 N16R8 |
| [固件 README](../firmware/desk-gateway/README.zh-CN.md) | 编译、接线、面板代理、高度策略 |
| [逻辑分析仪抓包](./1-protocol-capture-with-logic-analyzer.md) | Phase 0 流程 |
| [硬件 I²C 恢复排查](./7-hardware-i2c-restoration-investigation.md) | 为何默认回到 `@0x24` |
| [OLED 状态屏](./9-oled-status-display.md) | SSD1306 接线与页面 |
| [番茄语音提醒](./6-pomodoro-reminder-plan.md) | 本地计时与 I2S 语音 |

## 后续生态

这些能力有方案，尚未作为已交付功能：

| 文档 | 说明 |
|---|---|
| [MQTT / Home Assistant](./architecture/mqtt-home-assistant.md) | Cover / Number 实体方案 |
| [小米 / 华为生态](./architecture/ecosystem-xiaomi-huawei.md) | 原生模组 vs Matter |

## 工程规范

| 文档 | 说明 |
|---|---|
| [CHANGELOG](../CHANGELOG.md) | 未发布快照；V1 尚未打 tag |
| [参与贡献](../CONTRIBUTING.zh-CN.md) | IDF v6.0.2 安装、分层检查、PR 期望 |
| [安全政策](../SECURITY.zh-CN.md) | 如何私下报告漏洞 |
| [支持](../SUPPORT.zh-CN.md) | Issue 渠道和真机证据 |
| [开发规范](./standards/README.zh-CN.md) | Agent 强制规范入口 |
| [代码提交规范](./standards/git-commit-convention.md) | Conventional Commits 与暂存边界 |
