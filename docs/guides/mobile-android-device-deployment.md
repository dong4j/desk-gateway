# Desk Gateway 移动端 Android 真机部署说明

| 项目 | 当前值 |
|---|---|
| 移动端目录 | `mobile/app` |
| 技术栈 | React Native + Expo Development Build + TypeScript |
| Android Application ID | `com.dong4j.deskgateway` |
| 当前包类型 | Debug Development Build |
| 适用阶段 | BLE 技术验证与真机调试 |

本文说明如何在 macOS 上构建 Desk Gateway Android Development Build，并通过 USB 或
无线调试安装到 Android 真机。项目使用了 `react-native-ble-manager` 原生模块，不能用
Expo Go 代替 Development Build。

> 当前流程用于本地调试，不生成可提交 Google Play 的正式 AAB。Android 签名、内测轨道
> 和发布流程仍是独立任务。

## 1. 命令职责

### 1.1 `npm run android:device`

该命令执行 `scripts/run-android-device.sh`，负责：

1. 检查 `ANDROID_HOME`（或默认 `~/Library/Android/sdk`）和 `adb`；
2. 选择已授权且状态为 `device` 的真机，也可传入序列号；
3. 首次运行时根据 `app.json` 生成被 `.gitignore` 忽略的 `mobile/app/android`；
4. 调用本机 Android SDK 和 Gradle 编译 Debug Development Build；
5. 通过 ADB 安装并启动 `com.dong4j.deskgateway`；
6. 把 Metro `8081` reverse 到手机，方便 USB 下加载 bundle。

不要用 `expo start` 的 Expo Go 路径代替这条命令。BLE 原生模块不在 Expo Go 里。

首次安装、换手机、原生依赖变化或 `app.json` 原生配置变化后使用该命令。

仍可用 `npm run android -- --device` 走 Expo 交互式设备选择；日常真机部署优先 `android:device`，因为它会在 Gradle 之前把 SDK / 授权问题报出来。

### 1.2 `npm start`

`npm start` 只运行 `expo start --dev-client`。手机上已经安装兼容的 Development Build，
且本次只修改 TypeScript、页面或业务逻辑时，启动 Metro 后直接打开 Desk Gateway 即可。
它不会编译 APK，也不会安装 App。

## 2. Mac 端前置条件

### 2.1 安装 Android Studio 和 SDK

安装并至少启动一次 Android Studio，在 SDK Manager 中确认以下组件可用：

- Android SDK Platform 36；
- Android SDK Build-Tools；
- Android SDK Platform-Tools；
- Android SDK Command-line Tools；
- 需要 Emulator 时再安装 Android Emulator 和对应 System Image。

当前 Expo SDK 57 / React Native 0.86 工程以生成后的 Gradle 配置为最终依据。若 Gradle
提示缺少某个精确 SDK 或 Build-Tools 版本，应通过 SDK Manager 安装该版本，不要手工改
生成目录来绕过。

macOS 默认 SDK 目录为 `/Users/dong4j/Library/Android/sdk`。在 `~/.zshrc` 中配置：

```bash
export ANDROID_HOME=/Users/dong4j/Library/Android/sdk
export PATH="$PATH:$ANDROID_HOME/platform-tools:$ANDROID_HOME/emulator"
```

重新打开终端后检查：

```bash
java -version
adb version
```

优先使用 Android Studio 自带的 JDK。只有 Gradle 明确报告 Java 版本或路径错误时，才设置
`JAVA_HOME`，避免同时维护多套 JDK 选择。

### 2.2 当前开发机状态

2026-08-16 的只读检查结果为：Node.js、npm、Java 和 `node_modules` 已存在，但 Android
Studio、Android SDK、Platform-Tools 和 `adb` 尚未安装。因此当前状态是：

- 项目配置和部署命令已核对；
- Android 本地编译与真机安装 **NO-GO**；
- 完成工具链安装并取得实际构建结果后，才能更新为构建 GO；
- BLE、REST 和真实升降仍必须单独完成真机验收。

## 3. 准备 Android 手机

### 3.1 USB 调试

1. 在手机“关于手机”中连续点击版本号，开启开发者选项；
2. 在开发者选项中开启“USB 调试”；
3. 使用支持数据传输的 USB 线连接 Mac；
4. 手机弹出 RSA 授权时，确认允许这台电脑调试；
5. 在终端执行：

```bash
adb devices -l
```

设备状态必须是 `device`。`unauthorized` 表示手机尚未确认授权；列表为空时先检查数据线、
USB 模式和 Android Studio 的 Device Manager。

### 3.2 无线调试

Android 11 或更高版本可以在开发者选项中开启“无线调试”，再通过 Android Studio 的
Pair Devices Using Wi-Fi 完成配对。首次部署建议先使用 USB，确认 ADB、构建和安装链路
正常后再切换无线调试。

## 4. 首次编译并安装

进入移动端目录并安装锁定依赖：

```bash
cd /Users/dong4j/Developer/1.AI/ai-incubator/desk-gateway/mobile/app
npm install
```

先执行项目检查：

```bash
npm run typecheck
npm test
npm run doctor
adb devices -l
```

`expo-doctor` 的失败需要单独判断。依赖 patch 版本不一致时，先用
`npx expo install --check` 查看建议，不要在一次部署排障中顺带升级多项依赖并把失败
混在一起。

确认手机状态为 `device` 后构建并安装：

```bash
npm run android:device
```

首次运行会生成 `mobile/app/android`。该目录由 Expo Prebuild 管理且已被 `.gitignore`
忽略；持久原生配置应写入 `app.json` 或 Expo config plugin，不应只修改生成目录。

构建成功的最低证据包括：

1. Gradle 输出 `BUILD SUCCESSFUL`；
2. ADB 完成 APK 安装；
3. 手机能启动 Desk Gateway Development Build；
4. App 能连接 Metro 并显示页面。

这些证据仍不能证明 BLE、REST 或升降动作已经通过真机验收。

## 5. 日常开发与重新构建边界

| 修改内容 | `npm start` | `npm run android:device` |
|---|---:|---:|
| React 组件、样式、TypeScript 业务代码 | 需要 | 不需要 |
| BLE 协议编码、解析逻辑（纯 TypeScript） | 需要 | 不需要 |
| 新增、删除或升级原生 npm 包 | 需要 | 需要 |
| 修改 Android 权限或 Expo Plugin 配置 | 需要 | 需要 |
| 修改明文局域网 HTTP 配置 | 需要 | 需要 |
| 修改 Application ID、图标或原生资源 | 需要 | 需要 |
| App 被删除或更换 Android 手机 | 需要 | 需要 |

只需刷新业务代码时运行：

```bash
cd /Users/dong4j/Developer/1.AI/ai-incubator/desk-gateway/mobile/app
npm start
```

如果 USB 下 App 无法访问 Metro，可在 Metro 使用默认 `8081` 端口时执行：

```bash
adb reverse tcp:8081 tcp:8081
```

无线连接时，Mac 和手机需要能互相访问。Metro 使用 tunnel 只解决 Bundle 传输，不会让
手机自动获得 Desk Gateway 所在局域网的 REST 访问能力。

## 6. 分离编译和安装

正常开发优先使用 `npm run android:device`。需要保留 APK 或单独排查安装问题时，
先确保 `android` 目录已经由 Expo 生成，再执行：

```bash
cd /Users/dong4j/Developer/1.AI/ai-incubator/desk-gateway/mobile/app/android
./gradlew assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

随后回到 `mobile/app` 运行 `npm start`。Debug APK 只用于开发设备；正式内测或 Google
Play 发布需要独立配置版本号、Release 签名、AAB 和发布凭据。

## 7. 首次 BLE 和 REST 验收

首次 BLE 绑定前，在已认证 Web 或已有手机 App 中开启 120 秒配对窗口：

1. 启动 Android App，允许蓝牙或“附近的设备”权限；
2. 扫描并连接 `DeskGateway`；
3. Android 写入加密 Client Info `01 03`，出现系统配对提示时确认；
4. 确认收到 State Notify，并验证高度来自真实桌面；
5. 先验证 STOP，再在手靠近原控制器的情况下短按测试升降；
6. 验证松手 STOP、App 退后台、关闭蓝牙和断连后的停止行为；
7. 重启 App 和 ESP32，确认 bond 可以恢复。

Android 不同版本的 BLE 权限行为不同。若能安装但扫描不到设备，应同时检查系统蓝牙、
App 权限、系统定位策略、ESP32 广播、三客户端连接上限和配对窗口，不能仅根据编译成功
判断 BLE 配置正确。

REST 验收需要手机与 ESP32 位于同一局域网。优先访问
`http://desk-gateway.local/`，`.local` 解析失败时使用 ESP32 日志中的 IP，并确认 App
设置中的 `X-Desk-Key` 密码正确。

## 8. 常见问题

| 现象 | 检查项 |
|---|---|
| `adb: command not found` | 安装 Android SDK Platform-Tools，并检查 `ANDROID_HOME` 和 `PATH` |
| `adb devices` 显示 `unauthorized` | 解锁手机并确认 USB 调试 RSA 弹窗；必要时撤销 USB 调试授权后重连 |
| Gradle 提示 SDK location not found | 检查 `ANDROID_HOME` 是否指向 `/Users/dong4j/Library/Android/sdk` |
| Gradle 提示缺少 SDK 或 Build-Tools | 在 Android Studio SDK Manager 安装错误中点名的版本 |
| App 已安装但找不到 Metro | 保持 `npm start` 运行；USB 下检查 `adb reverse`，无线下检查网络互通和防火墙 |
| 一直扫描不到 Desk Gateway | 检查蓝牙/附近设备权限、系统定位策略、网关广播、连接上限和 120 秒配对窗口 |
| 配对或加密失败 | 删除手机和网关中的旧 Bond，重新开放配对窗口后连接；不要用 STOP 代替 Client Info 握手 |
| Wi-Fi REST 失败 | 检查同一局域网、`.local` 或固定 IP、REST 密码及固件 REST 来源权限 |
| 显示“另一台设备正在控制” | 当前 Android 不是运动所有者；保持状态订阅，可发送 STOP，等待所有者释放 |

## 9. 参考

- [Expo：本地创建 Debug Build](https://docs.expo.dev/guides/local-app-development/)
- [Android：在真机上运行 App](https://developer.android.com/studio/run/device)
- [移动端技术选型与真机门禁](../architecture/mobile-app-technology-selection.md)
- [移动端 BLE / REST 连接策略](../architecture/mobile-connection-transport.md)
