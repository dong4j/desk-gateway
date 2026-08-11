# Desk Gateway Mobile

Desk Gateway 的跨平台移动端工程，采用 React Native、Expo Development Build 和 TypeScript。

当前状态是 **Phase 1 iOS 真机控制 UI**：已按确认原型实现 Home / Settings，并通过统一
客户端支持 BLE GATT 与局域网 REST。技术决策和真机门禁见
[`docs/architecture/mobile-app-technology-selection.md`](../../docs/architecture/mobile-app-technology-selection.md)。

BLE 优先、Wi-Fi 回退、mDNS 和安全边界见
[`docs/architecture/mobile-connection-transport.md`](../../docs/architecture/mobile-connection-transport.md)。

iOS 真机的首次部署、命令职责、重新构建条件和故障排查见
[`docs/guides/mobile-ios-device-deployment.md`](../../docs/guides/mobile-ios-device-deployment.md)。

## 当前能力

- 扫描并连接广播名为 `DeskGateway` 的 ESP32。
- 自动模式优先 BLE，BLE 失败或断开后回退 `desk-gateway.local` 的 REST 接口。
- 可在设置页选择自动、仅 BLE、仅 Wi-Fi，并配置 REST 地址和 `X-Desk-Key` 密码。
- 发现 Desk Accessory Service。
- 读取并订阅固定 8 字节 State Characteristic。
- 读取标准 Device Information `180A/2A26` 并显示固件构建时间。
- 通过加密 Command Characteristic 验证 STOP、HOLD 和两个档位。
- App 失去前台时停止 HOLD 续期。
- 对协议版本、长度和未知状态采取 fail-closed。
- Home 页面实时展示高度、桌面动画、长按控制、STOP、档位和童锁状态。
- Home 与 Settings 均可写入童锁，且只展示 ESP32 回读状态。
- Settings 页面可设置最高安全高度、REST / Bluetooth / Panel 来源权限和重启网关。
- Settings 页面提供连接方式、本地自动连接和触感偏好；开关整行可点击，不使用嵌套触摸区。
- 旧固件未提供 Config 时仍可控制桌子，但设备设置会明确禁用。

## 开发命令

BLE 使用原生模块，**不能使用 Expo Go**。

```bash
npm install
npm start
```

`npm start` 只启动 Metro；首次安装或原生依赖变化后，还需要执行对应平台的原生构建命令。
iOS 27 Beta 真机必须使用下一节的 `npm run ios:device`。

### iOS 27 Beta 真机

Expo SDK 57 / React Native 0.86 的原生模板尚未采用 iOS 27 SDK 强制要求的
UIScene 生命周期。直接使用 Xcode 27 执行 `npm run ios` 会成功安装，但 App 会在
React Native 启动前退出。

当前开发机同时保留 Xcode 26.6（`/Applications/Xcode.app`）和 Xcode 27 Beta
（`/Applications/Xcode-beta.app`）。iOS 27 真机使用：

```bash
npm run ios:device
npm start
```

`ios:device` 使用 Xcode 26.6 / iOS 26 SDK 编译，再通过 Xcode 27 的 device support
安装和启动。可选地把设备 ID 作为参数传入；缺省时脚本选择第一台已连接的 iPhone：

```bash
npm run ios:device -- 00008101-0000000000000000
```

这是一条有边界的上游兼容措施，不伪造 UIScene 适配。Expo / React Native 正式支持
iOS 27 后应删除脚本并恢复统一的 `npm run ios`。

运行已安装的 Development Build：

```bash
npm start
```

静态检查：

```bash
npm run typecheck
npm test
npm run doctor
```

## 真机门禁

本地 TypeScript、Metro bundle 或模拟器通过都不能证明 BLE 可用。冻结 BLE 库前，必须在 iPhone 和 Android 真机完成：

1. 扫描、连接和服务发现。
2. 首次加密 Write 配对。
3. State Notify。
4. HOLD 续期和松手停止。
5. 断连、App 退后台和关闭蓝牙后的停止行为。
6. ESP32 与 App 重启后的 bond 恢复。

Command / State v1 保持不变；新增 Config / System characteristic 承载设备设置和重启。
iOS 已完成真实 BLE 运动控制和两张正式页面，Android 真机以及新增设置写入仍是开放门禁。
