# Desk Gateway 移动端 iOS 真机部署说明

| 项目 | 当前值 |
|---|---|
| 移动端目录 | `mobile/app` |
| 技术栈 | React Native + Expo Development Build + TypeScript |
| iOS Bundle ID | `com.dong4j.deskgateway` |
| 当前包类型 | Debug Development Build |
| 适用阶段 | BLE 技术验证与真机调试 |

本文说明 Desk Gateway 移动端如何构建并部署到 iPhone，重点解释
`npm start`、`npm run ios:device` 的职责，以及什么情况下需要重新构建 App。

> 当前流程生成的是开发调试包，不是可以提交 TestFlight 或 App Store 的正式发布包。

## 1. 三个容易混淆的概念

### 1.1 Metro 开发服务器

Metro 负责读取项目中的 JavaScript / TypeScript 源码，生成 React Native Bundle，并把
Bundle 提供给手机上的 Development Build。

Metro 不是 iOS 编译器，不会生成 `.app` 或 `.ipa`，也不会在 iPhone 上安装应用。

### 1.2 Development Build

Development Build 是安装在 iPhone 上的原生调试 App。它包含：

- React Native 和 Expo 原生运行时；
- `react-native-ble-manager` 等原生模块；
- 蓝牙、本地网络权限和 Bundle ID；
- 从 Metro 加载业务代码的开发能力。

本项目使用了 BLE 原生模块，因此不能使用 Expo Go 代替 Development Build。

### 1.3 正式发布包

TestFlight 和 App Store 使用 Release Archive、Distribution 签名及发布用 Provisioning
Profile。它与本文的 Debug Development Build 是两条不同流程。

当前项目尚未配置正式发布流程，因此 `npm run ios:device` 不会生成可提交 App Store 的
`.ipa` 文件。

## 2. 两条命令分别做什么

### 2.1 `npm start`

该命令实际执行：

```bash
expo start --dev-client
```

它会：

1. 启动 Metro 开发服务器；
2. 监听 JavaScript / TypeScript 文件变化；
3. 为已安装的 Development Build 提供最新 Bundle；
4. 支持 Fast Refresh 和开发菜单。

它不会：

- 编译 iOS 原生工程；
- 执行代码签名；
- 安装或更新 iPhone 上的 App；
- 修改 BLE 原生模块。

因此，只运行 `npm start` 的前提是 iPhone 上已经安装过兼容的 Development Build。

### 2.2 `npm run ios:device`

该命令实际执行：

```bash
bash scripts/run-ios-device.sh
```

脚本会：

1. 检查本机的 Xcode 26 和 Xcode 27；
2. 自动寻找已连接并解锁的 iPhone；
3. 使用 CocoaPods 同步 npm 包中的 iOS 原生模块；
4. 使用 Xcode 26 / iOS 26 SDK 编译 Debug 原生 App；
5. 使用 Apple Development 身份和 Provisioning Profile 签名；
6. 通过 Xcode 27 的设备支持把 App 安装到 iPhone；
7. 安装完成后启动 `com.dong4j.deskgateway`。

生成的 App 位于临时 Derived Data 目录：

```text
${TMPDIR}/desk-gateway-xcode26/Build/Products/Debug-iphoneos/DeskGateway.app
```

该命令不会启动 Metro，因此日常调试时还需要保持 `npm start` 运行。

## 3. 首次真机部署前提

Mac 端需要：

- Node.js 与 npm；
- Xcode 26：`/Applications/Xcode.app`；
- Xcode 27：`/Applications/Xcode-beta.app`；
- CocoaPods，并确保 `pod` 可从终端执行；
- Xcode 已登录 Apple ID，并可使用 Team `8WCUMGCWMB`；
- 项目依赖已安装。

iPhone 端需要：

- 使用 USB 连接 Mac；
- 手机已解锁；
- 已点击“信任此电脑”；
- 已开启“设置 → 隐私与安全性 → 开发者模式”；
- Xcode 能识别设备并挂载 Developer Disk Image。

部署前可以执行以下只读检查：

```bash
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer xcodebuild -version
DEVELOPER_DIR=/Applications/Xcode-beta.app/Contents/Developer xcodebuild -version
pod --version
node --version
npm --version
```

`scripts/run-ios-device.sh` 会为编译和安装分别设置 `DEVELOPER_DIR`，因此不依赖当前
`xcode-select` 指向哪个 Xcode。两套 Xcode 的实际路径或版本不符合脚本预期时，应先修正
安装路径，或者显式设置脚本支持的 `DESK_XCODE_26_APP`、`DESK_XCODE_27_APP`，不要通过
全局切换 Xcode 掩盖路径问题。

Apple 官方操作入口：

- [在设备上开启 Developer Mode](https://developer.apple.com/documentation/xcode/enabling-developer-mode-on-a-device)
- [在 Device Hub 管理物理设备](https://developer.apple.com/documentation/xcode/managing-your-simulated-and-physical-devices-in-device-hub)
- [在模拟器或物理设备上运行 App](https://developer.apple.com/documentation/xcode/running-your-app-on-simulated-or-physical-devices)

## 4. 首次部署步骤

进入移动端目录：

```bash
cd /Users/dong4j/Developer/1.AI/ai-incubator/desk-gateway/mobile/app
```

安装依赖：

```bash
npm install
```

构建前执行项目检查：

```bash
npm run typecheck
npm test
npm run doctor
```

`expo-doctor` 的失败需要单独判断。依赖 patch 版本不一致时，先用
`npx expo install --check` 查看建议，不要在真机部署排障中顺带升级多项依赖并混淆
构建失败原因。

如果本机还没有生成 `mobile/app/ios` 目录，执行一次：

```bash
npx expo prebuild --platform ios
```

`ios` 是 Expo 生成的本地目录，当前被 `.gitignore` 忽略，不作为手工维护的业务源码。

打开第一个终端，启动 Metro：

```bash
npm start
```

保持该终端运行。然后打开第二个终端：

```bash
cd /Users/dong4j/Developer/1.AI/ai-incubator/desk-gateway/mobile/app
npm run ios:device
```

脚本完成后会自动安装并启动 App。第一次安装时，iOS 可能要求确认开发者 App；按照系统
提示完成信任后重新打开 Desk Gateway。

如果连接了多台设备，可以显式指定设备 ID：

```bash
npm run ios:device -- 00008101-0000000000000000
```

## 5. 日常开发流程

### 5.1 只修改 TypeScript、页面或业务逻辑

如果没有修改原生依赖和原生配置，并且手机上已经安装 Development Build，只需要：

```bash
npm start
```

然后打开手机上的 Desk Gateway。代码变更会通过 Metro 和 Fast Refresh 下发。

### 5.2 需要重新构建并安装

出现以下任一情况，需要再次执行：

```bash
npm run ios:device
```

- 第一次部署到这台 iPhone；
- 手机上的 App 被删除；
- 新增、删除或升级了包含原生代码的 npm 依赖；
- 修改了 `app.json` 中的 iOS 插件、权限、Bundle ID 或 Team；
- 修改了 `ios` 目录中的原生代码；
- Development Build 无法加载当前原生模块；
- 之前误用 Xcode 27 SDK 构建，导致 App 点击后立即退出。

判断表：

| 修改内容 | `npm start` | `npm run ios:device` |
|---|---:|---:|
| React 组件、样式、TypeScript 业务代码 | 需要 | 不需要 |
| BLE 协议编码、解析逻辑（纯 TypeScript） | 需要 | 不需要 |
| 新增原生 npm 包 | 需要 | 需要 |
| 修改蓝牙权限或 Expo Plugin 配置 | 需要 | 需要 |
| 修改本地网络、Bonjour 或 HTTP 配置 | 需要 | 需要 |
| 修改 Bundle ID、签名 Team | 需要 | 需要 |
| App 被删除或换了一台 iPhone | 需要 | 需要 |

## 6. 为什么当前不能直接使用 `npm run ios`

`npm run ios` 实际执行 `expo run:ios`，会使用当前选中的 Xcode 和 iOS SDK 构建。

Expo SDK 57 / React Native 0.86 的原生模板还没有完成 iOS 27 SDK 强制要求的
`UIScene` 生命周期迁移。直接使用 Xcode 27 SDK 编译后，App 虽然可以安装，但会在
React Native 启动前被系统终止，表现为点击图标后立即退出。

当前兼容策略是：

```text
Xcode 26 / iOS 26 SDK 编译和签名
                  ↓
Xcode 27 Device Support 安装到 iOS 27 真机
```

因此当前真机必须使用：

```bash
npm run ios:device
```

不要使用以下命令覆盖已经正常运行的 Development Build：

```bash
npm run ios
npx expo run:ios --device
```

等 Expo / React Native 正式支持 iOS 27 生命周期后，应删除临时脚本并恢复统一的
`npm run ios` 流程。

## 7. 常见问题

### 7.1 第一次使用 BLE

首次绑定前，在已认证 Web 或已有手机 App 中开启 120 秒配对窗口：

1. 启动 App 并允许蓝牙权限；
2. 扫描并连接 `DeskGateway`；
3. iOS 写入加密 Client Info `01 02`，出现系统配对提示时确认；
4. 确认收到 State Notify，且高度来自真实桌面；
5. 先验证 STOP，再在手靠近原控制器的情况下短按测试升降；
6. 验证松手 STOP、App 退后台、关闭蓝牙和断连后的停止行为；
7. 重启 App 和 ESP32，确认 bond 可以恢复。

安装并启动成功只证明 Development Build 链路可用，不能替代上述 BLE 和真实升降验收。

### 7.2 第一次使用 Wi-Fi REST

最新 Development Build 第一次访问 ESP32 时，iOS 会询问是否允许 Desk Gateway 查找并
连接本地网络设备。必须选择“允许”，否则 `desk-gateway.local` 和手工 IP 都无法访问。

如果之前拒绝过，可以在 iPhone 的“设置 → 隐私与安全性 → 本地网络”中重新允许
Desk Gateway。手机与 ESP32 还需要处于同一局域网；`.local` 解析失败时，在 App 设置页
填写 ESP32 启动日志中的 IP。

### 7.3 点击 App 后立即退出

先确认是否误用了 Xcode 27 SDK 构建。重新执行：

```bash
npm run ios:device
```

如果仍退出，再通过 Xcode Devices and Simulators 或系统 Crash Log 确认新的崩溃原因，
不要直接假定仍是 `UIScene` 问题。

### 7.4 App 能打开，但无法加载页面

确认 Metro 正在运行：

```bash
npm start
```

同时确认 Mac 与 iPhone 的连接可用。Development Build 本身只提供原生容器，开发阶段的
JavaScript Bundle 仍由 Metro 提供。若使用局域网连接，Mac 和 iPhone 必须能够互相访问，
并检查 macOS 防火墙是否阻止 Node.js 接收入站连接。

### 7.5 `No profiles for 'com.dong4j.deskgateway' were found`

检查：

1. Xcode 已登录正确的 Apple ID；
2. Team `8WCUMGCWMB` 可用；
3. Bundle ID 与 `app.json` 一致；
4. Mac 可以访问 Apple Developer 服务；
5. iPhone 已加入当前开发签名允许的设备范围。

`ios:device` 已向 `xcodebuild` 传入 `-allowProvisioningUpdates`，前提是 Xcode 本身已经
完成账号登录和协议确认。

### 7.6 `The developer disk image could not be mounted`

- 解锁 iPhone 并保持屏幕亮起；
- 确认已信任 Mac；
- 打开 Xcode 的 Devices and Simulators，等待设备支持组件安装完成；
- 重新插拔 USB 后再执行脚本。

### 7.7 `No connected and unlocked iPhone was found`

脚本只自动选择已连接、已解锁并建立设备通道的 iPhone。先处理连接状态，或者传入明确
的设备 ID：

```bash
npm run ios:device -- <DEVICE_ID>
```

## 8. 正式打包边界

当前命令链只用于真机开发：

```text
npm start          → Metro 开发服务器
npm run ios:device → Debug .app 编译、签名、安装和启动
```

未来正式发布至少还需要：

1. 确认 Expo / React Native 已支持目标稳定版 Xcode 与 iOS SDK；
2. 配置正式版本号和 Build Number；
3. 配置 Apple Distribution 证书及 App Store Provisioning Profile；
4. 生成 Release Archive；
5. 在真机执行完整 BLE、安全停止和后台行为回归；
6. 上传 TestFlight，完成内测后再提交 App Store。

正式发布可选择 Xcode Archive 或 EAS Build，但在签名、发布渠道和 CI 方案确认前，不应
把当前 Debug 脚本扩展成发布脚本。

## 9. 推荐的当前工作方式

日常启动：

```bash
# 终端 1
cd /Users/dong4j/Developer/1.AI/ai-incubator/desk-gateway/mobile/app
npm start

# 终端 2：仅在需要安装或更新原生 App 时执行
cd /Users/dong4j/Developer/1.AI/ai-incubator/desk-gateway/mobile/app
npm run ios:device
```

记忆方式：

- `npm start`：把最新业务代码提供给已经安装的 App；
- `npm run ios:device`：重新制造、签名并安装手机上的 App 外壳。
