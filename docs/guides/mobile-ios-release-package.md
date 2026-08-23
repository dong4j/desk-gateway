# Desk Gateway 移动端 iOS 本机正式包

| 项 | 当前值 |
|---|---|
| 文档编号 | DG-GUIDE-IOS-RELEASE-001 |
| 移动端目录 | `mobile/app` |
| Bundle ID | `com.dong4j.deskgateway` |
| Apple Team | `8WCUMGCWMB` |
| 命令 | `npm run ios:release` |
| 包类型 | 本机 Release：JS 打进 `main.jsbundle` |
| 签名 | Apple Development，只给已注册的个人 iPhone |
| 不是 | TestFlight / App Store / V1-12 商店内测包 |

给自己的 iPhone 打一份能独立打开的包，装上后不需要再跑 `npm start`。
Debug 真机调试见 [iOS 真机部署](mobile-ios-device-deployment.md)。
商店签名和 TestFlight 见 [三端正式发布](mobile-watch-production-release.md)。

## 1. 三种包不要混

| 包 | 命令 | 要不要 Metro | 用途 |
|---|---|---|---|
| Debug Development Build | `npm run ios:device` | 要，再开 `npm start` | 日常改 TypeScript |
| 本机正式包 | `npm run ios:release` | 不要 | 自己的 iPhone 日常使用 |
| 商店候选包 | Xcode Archive / EAS | 不要 | TestFlight、App Store。当前未配完 |

`ios:release` 使用 Release 配置，把当时的业务 JS 打进 App 内的 `main.jsbundle`。
AppDelegate 在非 DEBUG 时读这个文件，所以拔掉 USB、关掉 Metro 也能打开。

签名仍是 Apple Development，只覆盖当前 Team 描述文件里的设备。不要把它写成已经
上架，也不要把 V1-12 标成完成。

不要用下面这些命令代替本文：

```bash
npm run ios
npx expo run:ios --device
npx expo run:ios --configuration Release --device
```

Expo SDK 57 还没完成 iOS 27 要求的 `UIScene` 生命周期。用 Xcode 27 SDK 直接编，
图标能装上，一点就退。本文继续走：

```text
Xcode 26 / iOS 26 SDK 编译和签名
                ↓
Xcode 27 Device Support 安装到 iOS 27 真机
```

## 2. 打包前准备

Mac：

- Node.js 与 npm
- Xcode 26：`/Applications/Xcode.app`
- Xcode 27：`/Applications/Xcode-beta.app`
- 终端能执行 `pod`
- Xcode 已登录 Apple ID，Team `8WCUMGCWMB` 可用
- `mobile/app` 依赖已安装

路径或版本对不上时，先改安装位置，或显式设置 `DESK_XCODE_26_APP`、
`DESK_XCODE_27_APP`，不要靠全局 `xcode-select` 掩盖。

iPhone：

- USB 连上 Mac
- 已解锁、已信任此电脑
- 已打开「设置 → 隐私与安全性 → 开发者模式」
- Xcode 的 Devices and Simulators 能看到这台手机

只读检查：

```bash
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer xcodebuild -version
DEVELOPER_DIR=/Applications/Xcode-beta.app/Contents/Developer xcodebuild -version
pod --version
node --version
```

本机还没有 `mobile/app/ios` 时，先生成一次：

```bash
cd /Users/dong4j/Developer/1.AI/ai-incubator/desk-gateway/mobile/app
npx expo prebuild --platform ios
```

`ios` 由 Expo 生成，已被 `.gitignore` 忽略。

## 3. 打包并安装

```bash
cd /Users/dong4j/Developer/1.AI/ai-incubator/desk-gateway/mobile/app
npm run typecheck
npm test
npm run ios:release
```

`ios:release` 实际执行 `scripts/run-ios-release.sh`。脚本会：

1. 检查 Xcode 26 和 Xcode 27
2. 选择已连接且已解锁的 iPhone；多台设备时传入设备 ID
3. `pod install`
4. 用 Xcode 26 / iOS 26 SDK 编 Release
5. 确认 `.app` 里有 `main.jsbundle`，没有就中止，避免装上仍依赖 Metro 的空壳
6. 用 Apple Development 签名
7. 用 Xcode 27 的设备支持安装并启动 `com.dong4j.deskgateway`

产物目录：

```text
${TMPDIR}/desk-gateway-xcode26-release/Build/Products/Release-iphoneos/DeskGateway.app
```

多台设备：

```bash
DEVELOPER_DIR=/Applications/Xcode-beta.app/Contents/Developer xcrun devicectl list devices
npm run ios:release -- <DEVICE_ID>
```

第一次安装后，若系统要求信任开发者：设置 → 通用 → VPN 与设备管理，信任后再打开
Desk Gateway。

同一 Bundle ID 会覆盖手机上已有的 Debug 包。

## 4. 怎么确认这是正式包

装完后做这三步，缺一步都不能说「不需要 Metro」：

1. 关掉所有 `npm start` / Metro 终端
2. 拔掉 USB
3. 从桌面再打开 Desk Gateway，页面能出来，而不是停在连开发服务器

`main.jsbundle` 不存在时脚本会失败并拒绝安装。装上后如果仍提示连 Metro，说明装到
手机上的不是这次 Release，或者被 Debug 包覆盖回去了。重新执行 `npm run ios:release`。

改 TypeScript、页面或业务逻辑之后必须再打一次。Release 没有 Fast Refresh。

新增或升级带原生代码的 npm 包、改 `app.json` 的 iOS 插件 / 权限 / Bundle ID / Team、
或删掉了手机上的 App，同样要重新打包。

## 5. 连桌子

安装成功只证明这台 iPhone 能独立打开 App。控桌步骤与 Debug 包相同，见
[多种方式控制升降桌](control-methods.md) 和
[本地多端部署清单](local-multi-client-setup.md)：

1. 手机和 ESP32 同一局域网
2. 在已登录 Web 或已有 App 里开 120 秒配对窗口
3. 扫描 `DeskGateway`，允许蓝牙；iPhone 写 Client Info `01 02`
4. 第一次走 Wi-Fi REST 时允许「本地网络」；拒绝过就去设置 → 隐私与安全性 → 本地网络
5. 设置页填写 REST 地址和 `X-Desk-Key`（当前 Web 密码）
6. 长按升降、松手 STOP。测试时人要在桌旁，不要把「命令已发送」当成桌子真的动了

## 6. 常见问题

### 6.1 点图标立刻退出

先确认没有误用 `npm run ios` 或 `expo run:ios --configuration Release`。回到本文
第 3 节重新打本机正式包。

### 6.2 `No connected and unlocked iPhone was found`

解锁、保持亮屏、信任电脑，或把设备 ID 传给脚本。USB 已插上但通道仍是
`disconnected` 时，先在 Xcode Devices and Simulators 里等设备支持装完。

### 6.3 `No profiles for 'com.dong4j.deskgateway'`

Xcode 账号、Team `8WCUMGCWMB`、以及这台 iPhone 是否在该开发签名允许的设备里。

### 6.4 页面能开，但连不上网关

本机正式包不依赖 Metro，但 BLE 回退 REST 仍要局域网。检查本地网络权限、
`desk-gateway.local` 或设置页里的 IP 和密码。

## 7. 相关文档

| 文档 | 看什么 |
|---|---|
| [iOS 真机部署](mobile-ios-device-deployment.md) | Debug + Metro 日常开发 |
| [三端正式发布](mobile-watch-production-release.md) | TestFlight / App Store，当前 NO-GO |
| [手机 App README](../../mobile/app/README.zh-CN.md) | 能力列表和开发命令 |
| [V1 版本验收](../status/v1-release-acceptance.md) | V1-12 商店内测门禁，不要用本文关闭 |
