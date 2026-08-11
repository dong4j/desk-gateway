# Desk Gateway 移动端技术选型与分阶段方案

| 项 | 内容 |
|---|---|
| 文档编号 | DG-ARCH-MOBILE-001 |
| 版本 | 0.4 |
| 日期 | 2026-08-11 |
| 状态 | iPhone 主控制与设置已完成；BLE 和指定 IP REST 已使用；Android 与自动回退矩阵待验收 |
| 目标平台 | iOS + Android |
| 关联协议 | [BLE 外设扩展 Profile v1](./ble-accessory-profile.md) |
| 视觉依据 | [`mobile/prototypes/`](../../mobile/prototypes/) |

本文冻结 Desk Gateway 移动端的总体技术方向，并记录 BLE 技术验证和正式 UI 的分阶段
边界。iOS 已按原型实现 Home / Settings，继续消费现有 ESP32 GATT v1；设备设置写入、
震动反馈和 BLE 优先 / REST 回退代码均已落地，Android 真机和异常矩阵仍待完成。

---

## 1. 结论

移动端采用：

| 层 | 选择 |
|---|---|
| 跨平台框架 | React Native |
| 工程体系 | Expo Development Build |
| 语言 | TypeScript |
| 路由 | 当前仅 Home / Settings，本地页面状态切换；未引入 Expo Router |
| BLE 第一候选 | `react-native-ble-manager` |
| BLE 备选 | `react-native-ble-plx` |
| 状态 | React state + `DeskClient` 订阅；当前未引入 Zustand |
| 动画 | React Native `Animated` + `react-native-svg` |
| 触感 | `expo-haptics`（Phase 1 引入） |
| 本地存储 | 首版仅在确有持久化数据时使用 AsyncStorage |

当前工程冻结在 Expo SDK `57.0.x` / React Native `0.86.x`。按 Expo SDK 57
版本化文档，其最低工具和系统基线为 Node.js `22.13.x`、Android 7、iOS 16.4，
iOS 本地构建需要 Xcode 26.4 或更高版本。若后续必须兼容 iOS 15，需要单独评估
降低 Expo SDK，而不能只修改 deployment target。

明确不采用：

- Expo Go：BLE 库包含原生代码，必须使用 Development Build。
- WebView 型方案：BLE 生命周期、权限和前后台安全控制不是本项目可以弱化的能力。
- 当前阶段的 Flutter / Kotlin Multiplatform：都能实现，但无法抵消本项目已有 TypeScript/React 开发效率和 Expo 工具链优势。
- 当前阶段的 monorepo `apps/ + packages/` 重构：目前只有一个 TypeScript 客户端，没有值得提前抽取的第二个消费者。
- 首版后台持续控制：App 进入后台必须停止运动，不能继续维持 HOLD。

---

## 2. 为什么是 React Native + Expo Development Build

本 App 的 UI 主要是设备状态、实时高度、桌面动画、按住升降、档位和设置，不需要游戏渲染或平台独占 UI。React Native 可以共享主要 UI、协议和业务代码，同时保留接入 Swift / Kotlin 原生能力的路径。

Expo Development Build 允许项目使用自定义原生库和原生配置，并可直接通过本地 Xcode / Android Studio 构建；EAS 是可选服务，不是本项目的运行依赖。

BLE 原生模块不能在 Expo Go 中运行。工程从第一天就使用 Development Build，避免先按 Expo Go 开发、接入 BLE 时再重建工程边界。

参考：

- Expo Development Builds：https://docs.expo.dev/develop/development-builds/introduction/
- Expo 自定义原生代码：https://docs.expo.dev/workflow/customizing/
- Expo New Architecture：https://docs.expo.dev/guides/new-architecture/
- Expo SDK 57 版本化参考：https://docs.expo.dev/versions/v57.0.0/
- Expo SDK 57 Dev Client：https://docs.expo.dev/versions/v57.0.0/sdk/dev-client/

---

## 3. BLE 库选择

### 3.1 第一候选：react-native-ble-manager

选择它作为 Phase 0 第一候选的原因：

1. 支持扫描、连接、服务发现、Write、Notify 和断连事件。
2. 提供 Expo config plugin。
3. 已明确支持 React Native New Architecture。
4. Android 提供 `createBond()`、`removeBond()` 和配对完成事件。

第 4 点对本项目尤其重要：ESP32 的 Command Characteristic 强制 `WRITE_ENC`，首次控制需要完成配对和加密链路，不能只验证明文 Read / Notify。

参考：

- 项目仓库：https://github.com/innoveit/react-native-ble-manager
- 方法文档：https://innoveit.github.io/react-native-ble-manager/methods/
- 更新记录：https://innoveit.github.io/react-native-ble-manager/changelog/

### 3.2 备选：react-native-ble-plx

`react-native-ble-plx` 的对象式 API 很适合封装 GATT Client，且项目仍在维护；但官方能力说明明确列出不提供外围设备 bonding API。系统访问加密特征时可能自动触发配对，不等于 iOS、Android 和不同系统版本都已通过本项目验收。

此外，其公开兼容表更新慢于当前 Expo / React Native 版本，因此本项目不会仅凭安装或编译成功就冻结该库。

如果 `react-native-ble-manager` 在 Phase 0 出现阻塞，则在相同 `BleAdapter` 接口后接入 `react-native-ble-plx` 做 A/B 真机验证，上层协议和 UI 不随库切换而改变。

参考：

- 项目仓库：https://github.com/dotintent/react-native-ble-plx
- API 文档：https://dotintent.github.io/react-native-ble-plx/

### 3.3 冻结门槛

只有第一候选在 iOS、Android 真机上通过以下项目，才能从“第一候选”升级为“正式依赖”：

- 能按 Desk Accessory Service UUID 扫描到设备。
- 能完成连接、服务发现和 State Notify。
- 首次加密 Write 能完成配对。
- App 和 ESP32 重启后 bond 可恢复。
- 加密 Write 能稳定发送 STOP、HOLD 和档位命令。
- 断连、前后台切换和权限拒绝不会留下持续运动。

---

## 4. 当前 GATT v1 契约

移动端必须消费现有协议，Phase 0 不修改固件：

| Attribute | UUID | 用途 |
|---|---|---|
| Service | `7f4e0001-6d4c-4f4b-9f7a-3c1d2e5a9b10` | Desk Accessory Service |
| Command | `7f4e0002-6d4c-4f4b-9f7a-3c1d2e5a9b10` | 加密 Write，单字节命令 |
| State | `7f4e0003-6d4c-4f4b-9f7a-3c1d2e5a9b10` | Read + Notify，固定 8 字节 |
| Config | `7f4e0004-6d4c-4f4b-9f7a-3c1d2e5a9b10` | Read + Notify + 加密 Write，设备设置 |
| System | `7f4e0005-6d4c-4f4b-9f7a-3c1d2e5a9b10` | 加密 Write，当前仅软重启 |

命令：

| Hex | 语义 |
|---|---|
| `00` | STOP |
| `01` | HOLD_UP |
| `02` | HOLD_DOWN |
| `11` | PRESET_1 |
| `14` | PRESET_4 |

移动端不得重新解释这些命令，也不得把未知高度 `FF FF` 显示成数值。

---

## 5. 软件边界

```text
Home / Settings
       │
  observable state
       │
 DeskController
       │
  DeskBleClient
       │
   BleAdapter
       │
react-native-ble-manager
       │
 ESP32 GATT v1
```

| 模块 | 责任 |
|---|---|
| `protocol.ts` | UUID、命令编码、8 字节 State 解码；纯 TypeScript |
| `BleAdapter` | 隔离具体 BLE 库 API，便于 Phase 0 A/B 验证 |
| `DeskBleClient` | 扫描、连接、发现、配对、订阅、Write、断线恢复 |
| `DeskController` | HOLD 续期、STOP 优先级、App 生命周期安全策略 |
| Store | 向 UI 发布连接、状态、高度和错误；不直接操作原生 BLE |
| UI | 只调用业务动作，不接触 UUID、Base64 或原始字节 |

当前只有一个 TypeScript 客户端，协议先保留在移动端工程内。等出现第二个 TypeScript 消费者后，再评估提取 `packages/desk-protocol`。

---

## 6. 长按控制安全规则

App 必须与固件 `750ms` HOLD 租约共同构成双重保护：

1. `onPressIn` 立即发送 `HOLD_UP` 或 `HOLD_DOWN`。
2. 按住期间约每 `300ms` 续期一次。
3. 同一时间最多存在一个未完成的 GATT Write，禁止 Write 堆积。
4. `onPressOut`、触摸取消、页面离开时立即发送 STOP。
5. App 进入后台时立即发送 STOP，并清理续期任务。
6. BLE 断连时立即清理本地运动状态；ESP32 同时执行断连停止。
7. 即使 STOP 丢失或 JS 线程失去调度，ESP32 租约到期仍会停止。
8. 童锁和 Bluetooth 来源权限由 ESP32 最终裁决，App 不得绕过。

首版不声明 iOS / Android 后台 BLE 模式。后台唤醒和后台持续控制不属于 MVP。

---

## 7. 工程目录

```text
mobile/
├── prototypes/
│   ├── desk-control-home.png
│   └── desk-settings.png
└── app/
    ├── App.tsx
    ├── app.json
    ├── src/
    │   ├── ble/
    │   │   ├── BleAdapter.ts
    │   │   ├── DeskBleClient.ts
    │   │   └── ReactNativeBleManagerAdapter.ts
    │   └── desk/
    │       ├── commands.ts
    │       ├── protocol.ts
    │       └── types.ts
    └── tests/
```

Phase 0 保持单入口，没有提前铺 Expo Router或完整 Store。Phase 1 已使用轻量页面状态、
`react-native-svg` 和统一视觉 token 实现两张原型，未引入不必要的导航框架。

---

## 8. 分阶段实施

### Phase 0：工程与 BLE 技术验证

本阶段交付：

- 最小 Expo Development Build 工程。
- BLE config plugin 和平台权限配置。
- GATT v1 纯 TypeScript 编解码。
- BLE Adapter 和 Client 骨架。
- 最小诊断入口，只展示日志和连接状态，不实现正式 UI。
- 协议单元测试。

真机验收：

- iPhone 与一台 Android 真机分别完成扫描、配对、Notify 和 Write。
- HOLD 续期、STOP、档位 1、档位 4可执行。
- App 退后台、强制断开和关闭蓝牙时桌子停止。
- 连续连接 / 断开 20 次，无需要重启手机才能恢复的故障。

### Phase 1：主控制页

已按确认原型实现连接状态、高度、升降动画、长按控制、STOP、档位和童锁状态，并在
iOS 真机完成构建、安装和 BLE 连接验证。

### Phase 2：设置页与协议扩展

设置页视觉结构和设备写入已经实现。固件构建信息通过标准 Device Information Service
提供；最高高度、童锁、REST / Bluetooth / Panel 来源权限通过 Config 回读并按单字段
写入；重启通过独立 System characteristic 执行。App 不做设备设置的乐观更新，只展示
ESP32 回读值。旧固件缺少 Config 时仍可连接和运动控制，但相关设置保持禁用。

新增 Config / System 不改变现有 Command / State UUID、字节布局和 LightBlue v1 客户端。

### Phase 3：双平台交付

完成权限拒绝、蓝牙关闭、配对失效、设备重启、App 被杀死、前后台切换和系统版本矩阵验收后，再进入 TestFlight / Android 内测。

当前状态：尚未进入 Phase 3。iPhone 日常控制路径已完成，但 Android、连续连接/断开、
BLE 超距自动回退和发布签名仍是 P1 任务。统一任务顺序见
[`5-current-status-and-priorities.md`](../5-current-status-and-priorities.md)。

---

## 9. 成功标准

Phase 0 代码完成不等于移动端 BLE 已选型完成。最终 GO 必须同时满足：

- TypeScript 编译、协议测试和 Expo Doctor 通过。
- iOS 真机完成加密配对和完整控制闭环。
- Android 真机完成加密配对和完整控制闭环。
- 任意客户端异常退出不会让桌子持续运动超过固件租约窗口。
- 保留串口日志与 App 日志作为验收证据。

在 Android 真机门禁完成前，只能标记“工程骨架可构建 / iOS 已验证”，不能宣称跨平台 BLE 已完成。

---

## 10. 决策记录

| 决策 | 结论 | 日期 |
|---|---|---|
| 跨平台框架 | React Native + TypeScript | 2026-08-11 |
| 工程体系 | Expo Development Build，不使用 Expo Go | 2026-08-11 |
| BLE 实现 | react-native-ble-manager；iPhone 已验证，Android 真机验收后完成双平台冻结 | 2026-08-11 |
| BLE 备选 | react-native-ble-plx，通过 BleAdapter 隔离 | 2026-08-11 |
| 后台策略 | App 退后台立即停止，不做后台持续控制 | 2026-08-11 |
| 工程结构 | `mobile/app/`，暂不重构根仓库为 monorepo | 2026-08-11 |
| 固件信息 | 标准 Device Information `180A/2A26`；Desk Command / State v1 保持不变 | 2026-08-11 |
| 设备设置 | 独立 Config 单字段写入 + Notify；System 独立承载重启 | 2026-08-11 |
