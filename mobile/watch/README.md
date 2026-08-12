# Desk Gateway Watch

独立 watchOS App 用于验证 Apple Watch 直连 Desk Gateway 的 BLE 控制闭环。产品交互、
Digital Crown 停止时序和真机门禁见
[`docs/architecture/apple-watch-control.md`](../../docs/architecture/apple-watch-control.md)。

## 本地验证

```bash
swift test
xcodegen generate
xcodebuild -project DeskGatewayWatch.xcodeproj \
  -scheme DeskGatewayWatch \
  -destination 'generic/platform=watchOS' \
  CODE_SIGNING_ALLOWED=NO build
```

`swift test` 覆盖平台无关的 GATT 字节协议和 Crown 状态机。通用 watchOS 构建只能证明
SwiftUI / CoreBluetooth 代码可以编译，不能代替 Apple Watch 真机上的扫描、配对、
Digital Crown、触感和真实升降验收。

## Simulator Debug Mock

在 Xcode 中使用 Watch Simulator 运行 `DeskGatewayWatch` 的 Debug 构建时，App 会自动
使用本地 Mock，不再扫描 BLE。页面顶部的橙色“模拟”标识表示当前没有连接真实升降桌：

- 初始高度为 `72.4 cm`；
- Digital Crown 可以模拟持续上升/下降；watchdog 每 `250 ms` 续期，`500 ms` 无输入 STOP；
- “请坐”模拟移动到 `64 cm`，“站立”模拟移动到 `102 cm`；
- 运动期间显示的 STOP 可以立即中断模拟动作。

Mock 仅在 `DEBUG && targetEnvironment(simulator)` 条件下编译为活动控制器。Watch 真机
Debug 和所有 Release 构建始终使用 `DeskBLECentral`，不存在蓝牙失败后自动切换 Mock 的
运行时降级，避免把模拟成功误认为真实硬件验收。

模拟器建议按以下顺序测试：

1. 确认顶部显示“已连接”和橙色“模拟”，高度为 `72.4 cm`；
2. 旋转 Digital Crown，确认高度连续变化，停止旋转后状态回到“已连接”；
3. 点击“请坐”或“站立”，确认高度向对应档位变化且界面出现 STOP；
4. 在运动过程中点击 STOP，确认高度立即停止变化。

## Apple Watch 真机安装

### 1. 前置条件

- Apple Watch 运行 watchOS 11 或更高版本，并已与 iPhone 配对；
- Mac 上的 Xcode 必须支持 Watch 当前安装的 watchOS 版本；
- 在 Xcode → Settings → Accounts 中登录 Apple ID；
- iPhone 和 Apple Watch 都开启“设置 → 隐私与安全性 → 开发者模式”。首次开启需要
  按系统提示重启；Watch 重启后选择 Turn On，并在出现提示时选择 Trust；
- Watch 保持解锁，并与配对 iPhone、Mac 放在附近。

Apple 官方说明：

- [Enabling Developer Mode on a device](https://developer.apple.com/documentation/xcode/enabling-developer-mode-on-a-device)
- [Managing devices in Device Hub](https://developer.apple.com/documentation/xcode/pairing-your-devices-with-your-mac)

### 2. 生成并打开 Xcode 工程

本目录不提交生成的 `.xcodeproj`。每次 `project.yml` 变化后重新生成：

```bash
cd mobile/watch
xcodegen generate
open DeskGatewayWatch.xcodeproj
```

这是使用 `WKWatchOnly` 的独立 Watch-only App，不需要先安装 iPhone companion App。

### 3. 配置真机签名

在 Xcode 中选择 `DeskGatewayWatch` Target → Signing & Capabilities：

1. 勾选 Automatically manage signing；
2. 确认 Team 是当前 Apple ID 可用的开发团队；
3. 确认 Bundle Identifier 在该团队下唯一。

仓库默认值位于 `project.yml`：

```text
DEVELOPMENT_TEAM = 8WCUMGCWMB
PRODUCT_BUNDLE_IDENTIFIER = com.dong4j.deskgateway.watch
```

如果 Team 不匹配，应修改 `project.yml` 后重新执行 `xcodegen generate`。不要只在生成的
Xcode 工程中修改，否则下次生成会覆盖手工设置。

### 4. 在 Device Hub 准备 Apple Watch

1. 打开 Xcode → Open Developer Tool → Device Hub，或从运行目标菜单选择
   Manage Devices…；
2. 必要时先用数据线连接配对 iPhone，并在 iPhone 上确认信任此电脑；
3. 在 Physical Devices 中选择 Apple Watch；
4. 根据右侧提示完成 Developer Mode、信任或设备注册；
5. 等待 Preparing 结束并显示 Ready。

Apple 官方真机运行流程见
[Running your app on simulated or physical devices](https://developer.apple.com/documentation/xcode/running-your-app-on-simulated-or-physical-devices)。

### 5. 安装并运行

在 Xcode 顶部工具栏：

1. Scheme 选择 `DeskGatewayWatch`；
2. Run Destination 选择 Physical Devices 下的物理 Apple Watch，不选择 Simulator
   或 Any watchOS Device；
3. 点击 Run 或按 `⌘R`；
4. 等待 Xcode 完成签名、安装并启动 App。

真机 Debug 构建始终使用 `DeskBLECentral`，不会启用 Simulator Mock，也不会显示橙色
“模拟”标识。

### 6. 首次 BLE 验收顺序

首次绑定前先在已认证 Web 或手机设置页开启 120 秒配对窗口，并确认网关已通电和广播：

1. 首次启动时允许蓝牙权限；
2. 等待顶部显示“已连接”，并确认高度来自真实桌面；
3. Watch 写入 `01 01` Client Info 触发系统配对提示时选择允许；正常握手不会发送 STOP；
4. 让手保持在桌面原控制器旁，极短旋转 Crown 后立即点击 STOP；
5. 确认停止链路后，再分别测试上升、下降、250 ms watchdog 续期和 500 ms 无输入 STOP；
6. 最后测试“请坐”档位 1、“站立”档位 4、童锁和 Bluetooth 来源拒绝。
7. 与 iPhone、Android 同时在线，验证非所有者显示“另一台设备正在控制”但不掉线，
   任意客户端 STOP 都能立即停止。

自动化构建、Simulator 和 UI 截图都不能替代这一真机安全门禁。

### 7. 常见问题

| 现象 | 检查项 |
|---|---|
| Xcode 看不到 Watch | 确认 iPhone 和 Watch 都开启 Developer Mode；Watch 已解锁并靠近 iPhone；在 Device Hub 查看具体提示 |
| Preparing 长时间不结束 | 保持 iPhone 有线连接、Watch 解锁，确认 Xcode 支持当前 watchOS 版本 |
| Signing 或 provisioning 失败 | 检查 Xcode 登录账号、Team、自动签名和 Bundle ID；持久修改必须写回 `project.yml` |
| Watch 显示蓝牙不可用 | 在 Watch 的隐私与安全性设置中检查蓝牙授权；确认当前运行目标确实是物理 Watch |
| 一直扫描不到 Desk Gateway | 确认网关正在广播，并断开手机 App、LightBlue 等其他 BLE Central |
| 显示另一台设备正在控制 | 当前 Watch 不是 BLE 运动所有者；可继续查看状态或发送 STOP，等待所有者释放后再控制 |
| 仍提示同时定义 `WKWatchOnly` 和 `WKRunsIndependentlyOfCompanionApp` | 删除 Watch 上的旧 App，确认生成配置只保留 `WKWatchOnly`，再执行 Product → Clean Build Folder 后重装 |
