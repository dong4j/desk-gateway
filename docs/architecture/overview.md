# Desk Gateway 架构总览

> 精简给人读的版本。完整定稿见  
> [`docs/architecture/platform-design.md`](./platform-design.md)。

## 一句话

Desk Gateway 是一套跑在 ESP32-S3 上的**升降桌智能平台**：厂商差异收进 **Desk Driver**，Web / 手机 App（BLE 或 Wi-Fi）/ Watch / 串口 / 键盘旋钮 / 语音 / D200H 共用 **desk_core**。

## 分层

所有控桌入口只调 `desk_core`，厂商 I²C 留在 Driver 里。

![软件分层：客户端经 desk_core、desk_driver、mxtark 到达控制盒](images/software-architecture.png)

## 硬件拓扑

Phase 2 是主动中间人：原厂面板走 GPIO6/7 软件代理，控制盒走 GPIO4/5 硬件 I²C Slave `@0x24`。两组 CLK/DAT 不得短接。ToF 与 OLED 共用 GPIO10/11。

![硬件拓扑：原厂面板、ESP32-S3 网关、控制盒与双 ToF](images/hardware-topology.png)

整机 GPIO 飞线总图如下。针脚细节仍以各专项接线图为准，见 [真机接线清单](../guides/bringup-checklist.md)。

![YD-ESP32-S3 整机飞线](images/full-wiring.png)

面板按键和多端指令都进 `desk_core`，只有左口 GPIO4/5 向控制盒发令：

![双口 RJ45 透传数据流：面板 GPIO6/7 与多端汇合后从左口出站](images/dual-rj45-passthrough-flow.png)

不拔网线切回原厂直连的继电器旁路见
[硬件方案](../hardware/panel-bypass-relay.md)。方案已选定，真机未接线，拓扑图仍是当前中间人。

## 当前状态

> 截至 2026-08-17：Phase 1 已完成。Phase 2 透传、异常停止、三客户端并发和双 ToF 安全矩阵
> 已在真桌通过。主固件可在 ESP-IDF 6.0.2 下编译。硬件结论以
> [`guides/bringup-checklist.md`](../guides/bringup-checklist.md) 为准。完成度和后续优先级见
> [`status/current-status-and-priorities.md`](../status/current-status-and-priorities.md)。
> 用法见 [`guides/control-methods.md`](../guides/control-methods.md)。

| 层 | 状态 |
|---|---|
| `mxtark`（硬件 I²C Slave `@0x24`） | 稳定返回升降键码；控制盒 digit 高度解析停用，TOF400C 作为产品高度源 |
| `desk_core` + Driver 框架 | 已实现；含统一停止、运动超时、档位、全局童锁和来源权限 |
| WiFi + Web（局域网、密码、UI、升降动效） | 基础真机路径已完成；实时显示 TOF400C 高度和右侧 TOF050C 间距；ToF 已参与档位和上升保护 |
| 手机 App 双通道 | iPhone 与 Android BLE 真机控制已完成；Client Info、Desk Busy 和配对设备 REST 管理 UI 已实现；超距自动回退矩阵待验收 |
| 高度 | TOF400C 处理后距离直接作为产品高度；档位、最高高度和右侧障碍物真机安全矩阵已通过 |
| BLE / Loctek / Jiecang | BLE 三连接、运动所有权、配对窗口和 Bond 管理已实现；iPhone、Watch、Android 真机并发已通过；Loctek / Jiecang 为 stub |
| Apple Watch | SwiftUI/CoreBluetooth App 真机扫描、配对、Crown 与三客户端并发已通过 |
| 双 RJ45 中间人、面板仲裁、童锁真屏蔽 | GPIO6/7 主动事务代理、短行程、断线 STOP、仲裁和真屏蔽已在真桌通过 |
| HA / Matter / Siri / OTA | 本机 HA MQTT Discovery 已接通，操作说明见 [HA 接入指南](../guides/home-assistant-mqtt.md)；完整 MQTT 安全矩阵仍见状态文档；Matter / Siri / OTA 未实现 |
| 米家 / 华为智慧生活 | Phase 3+；见 [生态调研](../future/ecosystem-xiaomi-huawei.md) |

## 仓库形状（目标）

```text
firmware/desk-gateway/
  components/
    desk_core/
    desk_driver/
    drivers/mxtark|loctek|jiecang
    connectivity/wifi|web|ble|desk_mqtt
docs/architecture/          ← 本目录与平台设计定稿
docs/status/                ← 完成度与 V1 门禁
docs/guides/                ← 用法与接线
```

## Web（本阶段）

- **仅局域网**；简单 Bearer 密码登录  
- 控制：升 / 降 / 停 / 坐姿 / 站姿 / **童锁**；高度有效且满足 ToF 上升策略时执行闭环档位  
- UI：品牌 + 升降桌示意图；`moving_up/down` 时示意图实时升降；有高度则按 mm 映射，无高度则按命令做相对动效  
- 状态：鉴权后的 `GET /api/v1/desk/status` 空闲约 1 s、运动中 250 ms 短轮询；返回 ToF 高度、右侧间距、`upward_blocked`、`child_lock`、`control_sources` 和已保存的高度设置  

## 手机 App 双通道

- UI 只依赖统一 `DeskClient`，不直接感知 BLE GATT 与 REST 的差异。
- 自动模式优先 BLE；BLE 首次连接失败或连接后断开时回退局域网 REST。
- REST 使用现有 `X-Desk-Key`，默认访问 `desk-gateway.local`，也可手工填写 DHCP IP。
- 通道切换不重放运动命令；长按续期、松手 STOP 和固件 `desk_core` 安全裁决保持不变。
- 设置页通过独立 REST 通道管理 120 秒配对窗口、Bond 列表、单删和全删；BLE 控制链路不转发管理请求。
- 详情：[移动端 BLE / Wi-Fi 双通道方案](mobile-connection-transport.md)

## 童锁与仲裁

- 童锁 ON：除 `STOP` 和解除童锁外，REST、串口、蓝牙及原厂面板都不能启动或维持运动  
- 童锁 OFF：每个来源还需通过自己的 NVS 权限开关；当前 Web 可配置 REST、Bluetooth 和 Panel  
- 关闭任一来源会先停止当前运动；Panel 重新开放后必须先观察到物理按键松开，禁止解锁即误动作  
- 优先级：急停 > 全局童锁 > 来源权限 >（未锁且允许时）面板优先 > 其他入口  

## 待逆向（协议）

- 原厂面板：**同时按住上+下约 5 秒 → 重置**（已确认有此操作，`DR` 未知；见协议笔记）

## BLE 外设总线（摘要）

- Gateway = **NimBLE GATT Server**；LightBlue、OLED + 无限旋钮等 = Client  
- 加密 Write：续期升 / 续期降 / 停 / 档位；高度未知或上升安全条件不满足时由固件拒绝或停止。Read + Notify 返回 ToF 高度  
- HOLD 使用 `750ms` 短租约；松手停止续期或连接断开都会自动停止  
- 最多三个 Central 同时在线；单一 BLE 运动所有者，非所有者收到 Desk Busy `0x80`，任意 STOP 始终有效  
- 与 Web 同走 `desk_core`；**板载**仍无旋钮无屏  
- 详情：[ble-accessory-profile.md](ble-accessory-profile.md)

## 小米 / 华为生态（摘要）

| 目标 | 额外硬件？ |
|---|---|
| 米家 / 智慧生活 **原生上架** | **通常要** 各自认证 Wi‑Fi 模组 + 合作认证 |
| 手机 App 当 Matter 控制器添加本设备 | **一般不要**换主控（ESP32 上跑 Matter） |
| 详情 | [ecosystem-xiaomi-huawei.md](../future/ecosystem-xiaomi-huawei.md) |

## 相关文档

| 文档 | 用途 |
|---|---|
| [平台设计定稿](platform-design.md) | 实现依据 |
| [当前状态与优先级](../status/current-status-and-priorities.md) | 已完成、待验收和剩余任务的统一清单 |
| [多种方式控制升降桌](../guides/control-methods.md) | Web、REST、BLE、手机、Watch、键盘、语音、D200H |
| [REST API](../guides/rest-api.md) | 局域网 HTTP 契约 |
| [小米/华为生态调研](../future/ecosystem-xiaomi-huawei.md) | 模组 vs Matter |
| [BLE 外设 Profile](ble-accessory-profile.md) | 旋钮/OLED 配件总线 |
| [BLE 三客户端与配对管理](ble-multi-client-bond-management.md) | iPhone、Apple Watch、Android 并发连接、运动所有权与 Bond 删除 |
| [移动端技术选型](../history/mobile-app-technology-selection.md) | React Native 手机端与 BLE 验证门禁 |
| [移动端双通道方案](mobile-connection-transport.md) | BLE 优先、REST 回退、mDNS 与安全边界 |
| [Apple Watch 控制方案](apple-watch-control.md) | SwiftUI Watch App 与直连 BLE 安全边界 |
| [需求](../status/requirements.md) | 做什么、阶段门禁 |
| [主控板](../hardware/esp32-s3-n16r8.md) | YD-ESP32-S3 N16R8 |
| [协议逆向](protocol-reverse-notes.md) | mxtark 契约 |
| [Upsy Desky](https://github.com/tjhorner/upsy-desky) | 参考产品，非协议照搬 |
