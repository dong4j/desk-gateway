# Apple Watch 控制方案

| 项 | 内容 |
|---|---|
| 文档编号 | DG-ARCH-WATCH-001 |
| 版本 | 0.1 |
| 日期 | 2026-08-11 |
| 状态 | 方案已确认；尚未创建 watchOS 工程 |
| 关联协议 | [BLE 外设扩展 Profile v1](./ble-accessory-profile.md) |
| 移动端方案 | [移动端技术选型与 Phase 0 方案](./mobile-app-technology-selection.md) |

本文定义 Desk Gateway 的 Apple Watch 控制方案。当前只冻结架构、功能边界和
安全约束，不创建 watchOS Target，不修改 ESP32 GATT 协议。

---

## 1. 结论

Apple Watch 首版采用原生 **SwiftUI + CoreBluetooth**，由 Watch 直接连接
ESP32 暴露的 Desk Accessory Service：

```text
Apple Watch App（SwiftUI）
           │
    CoreBluetooth Central
           │  BLE GATT v1
           ▼
ESP32 DeskGateway（NimBLE Peripheral）
           │
       desk_core
           │
        升降桌主机
```

首版不把 WatchConnectivity 或 REST 作为实时控制主链路：

- WatchConnectivity 只适合以后同步设置和最近设备；即时消息依赖 iPhone App
  可达，异步消息又不允许承载可能延迟执行的运动命令。
- REST 可作为诊断或降级通道，但依赖局域网、网关地址和 Watch 网络状态，不能
  替代本地 BLE 主链路。
- Expo / React Native 继续服务 iOS 和 Android 手机端；watchOS 使用原生 Target，
  不为复用少量 UI 而引入非正式 watchOS 运行层。

---

## 2. 与手机端的工程关系

正式工程保持一个产品、两个客户端实现：

```text
mobile/
├── app/                         # React Native + Expo 手机端
│   ├── src/
│   └── ios/                     # 生成并承载 Xcode workspace
├── watch/                       # SwiftUI watchOS 源码
│   ├── App/
│   ├── BLE/
│   │   ├── DeskBLECentral.swift
│   │   └── DeskProtocol.swift
│   ├── Control/
│   │   └── DeskHoldController.swift
│   └── Views/
└── prototypes/
```

Watch App 最终作为现有 iOS App 的 **Watch App for Existing iOS App** Target。
因为 watchOS Target 属于原生 Xcode 工程配置，进入实施阶段前必须先决定以下二选一：

1. 将 `mobile/app/ios/` 纳入版本管理，禁止用 `expo prebuild --clean` 覆盖手工 Target；
2. 编写本地 Expo config plugin，以可重复方式生成 watchOS Target。

首版优先选择方案 1，代码和维护成本更低。只有确认需要频繁重新生成 iOS 工程时，
才值得实现 config plugin。

---

## 3. 复用现有 GATT v1

Watch 不定义新协议，直接消费已冻结的 Service：

| Attribute | UUID | 用途 |
|---|---|---|
| Service | `7f4e0001-6d4c-4f4b-9f7a-3c1d2e5a9b10` | Desk Accessory Service |
| Command | `7f4e0002-6d4c-4f4b-9f7a-3c1d2e5a9b10` | 加密 Write |
| State | `7f4e0003-6d4c-4f4b-9f7a-3c1d2e5a9b10` | Read + Notify，固定 8 字节 |

| Hex | Watch 操作 |
|---|---|
| `00` | STOP |
| `01` | HOLD_UP |
| `02` | HOLD_DOWN |
| `11` | PRESET_1 |
| `14` | PRESET_4 |

State Notify 用于显示真实高度、运动状态、童锁、Bluetooth 来源权限和安全上限。
未知高度 `FF FF` 必须显示为未知，不能自行估算。

协议文档继续作为 TypeScript 与 Swift 两端的唯一事实来源。首版常量数量很少，暂不
引入代码生成；为 Swift 编解码器补充与 TypeScript 相同的固定字节测试，防止漂移。

---

## 4. 首版功能边界

### 4.1 实现

- 扫描并连接 `DeskGateway`；
- 首次加密 Write 触发系统配对；
- 订阅高度与状态 Notify；
- 按住上升、按住下降、独立 STOP；
- 档位 1（64 cm）和档位 4（102 cm）；
- 显示童锁、Bluetooth 来源权限和安全高度上限；
- 连接失败、设备忙、蓝牙关闭和权限拒绝的明确状态；
- 前台退出、页面消失和断连时执行安全停止。

### 4.2 暂不实现

- 在 Watch 修改童锁、安全高度或来源权限；
- Digital Crown 直接控制运动；
- Complication 直接触发运动；
- Siri / App Intent；
- 后台持续控制；
- 多网关管理；
- 手机 BLE 连接与 Watch BLE 连接同时在线。

Complication 后续可以显示高度或作为启动入口，但不能提供无需确认的升降动作。

---

## 5. 长按与停止安全规则

Watch 端与手机端使用同一控制语义：

1. 手指按下立即发送一次 `HOLD_UP` 或 `HOLD_DOWN`；
2. 保持按住期间约每 `300 ms` 续期；
3. 任一时刻最多保留一个未完成的 GATT Write，禁止请求堆积；
4. 松手、手势取消和方向切换时立即发送 `STOP`；
5. 页面消失或 App 失去前台时取消续期并尽力发送 `STOP`；
6. BLE 断连立即清除本地运动状态；
7. 即使 Watch 的 STOP 丢失，ESP32 现有 `750 ms` HOLD 租约仍必须兜底停止；
8. 全局童锁和 Bluetooth 来源权限继续由 ESP32 `desk_core` 最终裁决。

档位命令是由真实高度闭环停止的离散动作，不通过 Watch 伪装成长按。Watch 断连时，
ESP32 仍按现有协议执行停止。

---

## 6. 单连接约束

当前固件配置为：

```text
CONFIG_BT_NIMBLE_MAX_BONDS=3
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
```

因此可以保存多个客户端的 bond，但同一时刻只能连接一个 BLE Central。首版采用明确
的单连接行为：

- Watch 打开后尝试直接连接；
- 若 iPhone 或 LightBlue 已占用连接，Watch 显示“设备正被其他控制端使用”；
- 不自动通过 REST 或 WatchConnectivity 绕行，避免同一次操作走向不可预测的链路；
- 手机 App 进入后台时应按手机端既有规则 STOP 并释放 BLE 连接；
- 只有真机证明日常确实需要手机和 Watch 同时在线，才评估把最大连接数提高到 2，
  并补充连接所有权与并发命令仲裁。

提高连接数属于后续固件变更，不包含在 Watch 首版技术验证中。

---

## 7. 首版交互

Watch 页面只保留高频、可快速完成的操作：

```text
┌──────────────────┐
│   Desk Gateway   │
│      已连接       │
│                  │
│     99.0 cm      │
│       上升        │
│                  │
│   [ 按住升 ]      │
│   [   停止  ]      │
│   [ 按住降 ]      │
│                  │
│ [请坐]     [起立] │
│ 🔓 童锁已关闭     │
└──────────────────┘
```

- STOP 必须始终可见，并且不能因为高度未知而置灰；
- 上升按钮不能仅因高度暂时未知而禁用，最终上限由 ESP32 保护；
- 童锁或 Bluetooth 来源关闭时，运动按钮禁用并显示准确原因；
- HOLD 开始、停止和连接成功使用轻量触感反馈；
- 不使用需要精细滚动或多层导航的设置页面。

---

## 8. 分阶段实施与验收

### W0：原生 BLE 技术验证

- 创建 SwiftUI watchOS Target；
- 实现 CoreBluetooth 扫描、连接、服务发现、配对和 Notify；
- 先验证 Read/Notify 和加密 `STOP`，再开放运动命令；
- 使用 Apple Watch 真机，不以 Simulator 结果代替 BLE 验收。

### W1：安全控制闭环

- 档位 1、档位 4；
- HOLD 续期与松手 STOP；
- 页面离开、锁屏、失联和杀进程测试；
- 童锁和来源权限拒绝测试。

### W2：正式 UI 与辅助能力

- 完成 Watch 尺寸适配、触感和无障碍；
- 仅在主控制稳定后评估 WidgetKit complication、App Intent 和设置同步；
- WatchConnectivity 只同步非实时数据，不承载运动命令。

### GO 门槛

- Apple Watch 真机可稳定扫描、配对、重连和订阅状态；
- 连续 20 次连接/断开不需要重启 Watch 或 ESP32；
- 每次松手都立即停止，异常路径不超过固件租约窗口；
- 童锁和 Bluetooth 来源权限无法被 Watch 绕过；
- Watch 与 iPhone 抢占单连接时有明确提示，不出现隐式切换控制链路。

---

## 9. 参考

- Apple watchOS Apps：https://developer.apple.com/documentation/watchos-apps/
- Apple 设置 watchOS 工程：https://developer.apple.com/documentation/watchos-apps/setting-up-a-watchos-project
- Apple 独立 watchOS App：https://developer.apple.com/documentation/watchos-apps/creating-independent-watchos-apps
- Apple CoreBluetooth：https://developer.apple.com/documentation/corebluetooth/cbcentralmanager
- Apple WatchConnectivity：https://developer.apple.com/documentation/watchconnectivity
- Expo 额外平台支持：https://docs.expo.dev/modules/additional-platform-support/

---

## 10. 决策记录

| 决策 | 结论 | 日期 |
|---|---|---|
| Watch UI | 原生 SwiftUI | 2026-08-11 |
| 实时控制链路 | Watch 通过 CoreBluetooth 直连 ESP32 | 2026-08-11 |
| WatchConnectivity | 仅作非实时设置同步，不承载运动命令 | 2026-08-11 |
| REST | 仅作后续诊断或显式降级方案 | 2026-08-11 |
| GATT | 复用 v1，不为 Watch 修改固件协议 | 2026-08-11 |
| 首版连接模型 | 保持单 BLE 连接，冲突时明确提示 | 2026-08-11 |
