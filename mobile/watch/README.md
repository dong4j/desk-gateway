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
- Digital Crown 可以模拟持续上升/下降，并保留 `400 ms` 无输入 STOP；
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

## 真机顺序

1. 在 Xcode 中选择 `DeskGatewayWatch` scheme 和已配对 Apple Watch。
2. 首次只确认扫描、连接、State/Config Read + Notify 和加密 STOP。
3. 确认停止链路后，再测试 Crown 上升、下降以及 400 ms 无输入 STOP。
4. 最后测试“请坐”档位 1、“站立”档位 4、童锁和 Bluetooth 来源拒绝。
