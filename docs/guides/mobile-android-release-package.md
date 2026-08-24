# Desk Gateway 移动端 Android 本机正式包

| 项 | 当前值 |
|---|---|
| 文档编号 | DG-GUIDE-ANDROID-RELEASE-001 |
| 移动端目录 | `mobile/app` |
| Application ID | `com.dong4j.deskgateway` |
| 命令 | `npm run android:release` |
| 包类型 | 本机 Release APK：JS 打进 `index.android.bundle` |
| 签名 | 生成工程里的 debug keystore，只给自己的手机 USB 安装 |
| 不是 | Google Play / EAS Preview / V1-12 商店内测包 |

给自己的 Android 手机打一份能独立打开的 APK，装上后不需要再跑 `npm start`。
Debug 真机调试见 [Android 真机部署](mobile-android-device-deployment.md)。
Play 签名和内测见 [三端正式发布](mobile-watch-production-release.md)。

## 1. 三种包不要混

| 包 | 命令 | 要不要 Metro | 用途 |
|---|---|---|---|
| Debug Development Build | `npm run android:device` | 要，再开 `npm start` | 日常改 TypeScript |
| 本机正式包 | `npm run android:release` | 不要 | 自己的 Android 手机日常使用 |
| 商店候选包 | EAS Preview APK / Play AAB | 不要 | Google Play。当前未配完 |

`android:release` 走 Gradle `assembleRelease`。Expo 的 `export:embed` 会在 Release
里把当时的业务 JS 打进 APK。装上后关掉 Metro、拔掉 USB 也能打开。

当前生成的 `android/app/build.gradle` 里，Release 仍使用 debug 签名。这只够自己用
USB 安装，不能当 Play 包，也不能把 V1-12 标成完成。以后如果换成正式 keystore，同包
名可能无法覆盖 Debug App，要先卸掉再装，并先记下网关地址和 REST 密码。

不要用 Expo Go，也不要用下面这些命令代替本文：

```bash
npm run android
npx expo run:android --device
```

那些路径默认仍是 Debug，装上还要 Metro。

## 2. 打包前准备

Mac：

- Node.js 与 npm
- Android SDK（`ANDROID_HOME` 或默认 `/Users/dong4j/Library/Android/sdk`）
- `adb` 在 `PATH` 里，或能从 `$ANDROID_HOME/platform-tools/adb` 找到
- `mobile/app` 依赖已安装

手机：

- 已打开开发者选项和 USB 调试
- USB 已连接，或无线调试已配对
- `adb devices` 状态是 `device`，不是 `unauthorized` / `offline`

USB 已插上、调试已打开，但 `adb devices` 仍是空的或只剩 `offline` 模拟器时，先重启
adb。旧 daemon 卡住时，真机不会进列表：

```bash
adb kill-server
adb start-server
adb devices -l
```

列表里目标手机必须是 `device`。多台设备时记下序列号，后面传给
`npm run android:release`。重启 adb 后手机可能再弹一次「允许 USB 调试」，解锁后点允许。

只读检查：

```bash
echo "$ANDROID_HOME"
adb version
adb devices -l
```

本机还没有 `mobile/app/android` 时，先生成一次：

```bash
cd /Users/dong4j/Developer/1.AI/ai-incubator/desk-gateway/mobile/app
npx expo prebuild --platform android
```

`android` 由 Expo 生成，已被 `.gitignore` 忽略。持久配置写回 `app.json` 或 config
plugin，不要只改生成目录。

## 3. 打包并安装

```bash
cd /Users/dong4j/Developer/1.AI/ai-incubator/desk-gateway/mobile/app
npm run typecheck
npm test
npm run android:release
```

`android:release` 实际执行 `scripts/run-android-release.sh`。脚本会：

1. 检查 Android SDK 和 `adb`
2. 选择已授权真机；多台设备时传入序列号
3. 用 `./gradlew assembleRelease` 打 Release APK
4. 确认 APK 里有 `index.android.bundle` 或 `main.jsbundle`，没有就中止
5. `adb install -r` 覆盖安装并启动 `com.dong4j.deskgateway`

产物：

```text
mobile/app/android/app/build/outputs/apk/release/app-release.apk
```

多台设备：

```bash
adb devices -l
npm run android:release -- <SERIAL>
```

同一 Application ID 会覆盖手机上已有的 Debug 包。两边目前都是 debug 签名，一般可以直接覆盖。

## 4. 怎么确认这是正式包

装完后做这三步，缺一步都不能说「不需要 Metro」：

1. 关掉所有 `npm start` / Metro 终端
2. 拔掉 USB（无线调试也可以先断开）
3. 从桌面再打开 Desk Gateway，页面能出来，而不是停在连开发服务器

APK 里没有 JS bundle 时脚本会失败并拒绝安装。装上后如果仍提示连 Metro，说明装到
手机上的不是这次 Release，或者被 Debug 包覆盖回去了。重新执行
`npm run android:release`。

改 TypeScript、页面或业务逻辑之后必须再打一次。Release 没有 Fast Refresh。

新增或升级带原生代码的 npm 包、改 `app.json` 的 Android 插件 / 权限 / 包名，
或删掉了手机上的 App，同样要重新打包。

## 5. 连桌子

安装成功只证明这台 Android 能独立打开 App。控桌步骤与 Debug 包相同，见
[多种方式控制升降桌](control-methods.md) 和
[本地多端部署清单](local-multi-client-setup.md)：

1. 手机和 ESP32 同一局域网
2. 在已登录 Web 或已有 App 里开 120 秒配对窗口
3. 扫描 `DeskGateway`，允许蓝牙 / 附近的设备；Android 写 Client Info `01 03`
4. 设置页填写 REST 地址和 `X-Desk-Key`（当前 Web 密码）
5. 长按升降、松手 STOP。测试时人要在桌旁，不要把「命令已发送」当成桌子真的动了

## 6. 常见问题

| 现象 | 检查项 |
|---|---|
| `adb: command not found` | 安装 Platform-Tools，并检查 `ANDROID_HOME` / `PATH` |
| `No authorized Android device` 或列表是空的 | 先重启 adb：`adb kill-server`、`adb start-server`、`adb devices -l`。再解锁手机并确认 USB 调试弹窗；状态必须是 `device` |
| `Performing Streamed Install` 后只有空的 `failed to install ...apk:` | 旧的流式安装路径。当前脚本改为推到 `/data/local/tmp` 再 `pm install` |
| `failed to read copy response: EOF`，随后设备从列表消失 | 74MB APK 推完后小米 adbd 断开，多见于 USB 扩展坞。脚本会等设备回来再 `pm install`。仍掉线就把手机直插 Mac，不要走扩展坞；解锁保持亮屏后再跑 `adb kill-server` / `adb start-server` / `adb devices -l` |
| `device offline` | USB 线还在，但 adb 通道断了。解锁、点允许 USB 调试，再跑上面三条重启命令 |
| `INSTALL_FAILED_UPDATE_INCOMPATIBLE` | 手机上已有不同签名的同包名 App。先卸掉再装，并记下设置 |
| 点图标后仍要 Metro | 确认装的是这次 Release APK，不要再跑 `android:device` 覆盖回去 |
| 扫描不到 Desk Gateway | 蓝牙 / 附近设备权限、系统定位策略、120 秒配对窗口、三客户端上限 |
| 显示「另一台设备正在控制」 | 当前不是运动所有者；STOP 仍有效 |

## 7. 相关文档

| 文档 | 看什么 |
|---|---|
| [Android 真机部署](mobile-android-device-deployment.md) | Debug + Metro 日常开发 |
| [iOS 本机正式包](mobile-ios-release-package.md) | 同一目标的 iPhone 路径 |
| [三端正式发布](mobile-watch-production-release.md) | EAS / Google Play，当前 NO-GO |
| [手机 App README](../../mobile/app/README.zh-CN.md) | 能力列表和开发命令 |
| [V1 版本验收](../status/v1-release-acceptance.md) | V1-12 商店内测门禁，不要用本文关闭 |
