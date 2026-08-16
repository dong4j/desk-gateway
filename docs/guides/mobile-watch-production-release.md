# Desk Gateway iPhone、Android 与 Apple Watch 正式发布说明

| 项 | 当前值 |
|---|---|
| 文档编号 | DG-GUIDE-RELEASE-001 |
| 版本 | 0.1 |
| 日期 | 2026-08-16 |
| iPhone Bundle ID | `com.dong4j.deskgateway` |
| Android Application ID | `com.dong4j.deskgateway` |
| Watch Bundle ID | `com.dong4j.deskgateway.watch` |
| 当前结论 | 三端开发代码存在；正式签名、商店内测和发布验收均未完成 |

本文定义 Desk Gateway iPhone、Android 和独立 Apple Watch App 从开发构建进入商店正式
发布的统一流程。构建成功、上传成功、商店内测和正式上线是四个不同阶段，不得互相替代。

本文只描述发布流程和门禁，不包含任何证书、私钥、密码或商店 API 凭据。

## 1. 发布形态

| 平台 | 内测渠道 | 正式渠道 | 商店产物 |
|---|---|---|---|
| iPhone | TestFlight | Apple App Store | 已签名 iOS Archive |
| Android | Google Play Internal testing；必要时先发签名 Preview APK | Google Play Production | AAB |
| Apple Watch | watchOS TestFlight | Apple Watch App Store | 已签名 Watch-only Archive |

Android APK 可以直接安装，适合不依赖 Metro 的内部验证；AAB 用于 Google Play，不能直接
通过 ADB 安装。iPhone 和 Watch 的正式候选包必须经过 App Store Connect/TestFlight，不能
用 Debug Development Build 或 Xcode Run 结果代替。

## 2. 当前状态

### 2.1 iPhone

当前已有：

- React Native + Expo Development Build 工程；
- `com.dong4j.deskgateway` Bundle ID；
- Apple Team `8WCUMGCWMB`；
- Xcode 26 编译、Xcode 27 Device Support 安装的 Debug 真机脚本；
- iPhone BLE 核心控制历史真机证据。

当前缺少：

- Release Archive 和 App Store Distribution 签名验证；
- 唯一递增的 `ios.buildNumber`；
- App Store Connect App record；
- TestFlight 构建和测试记录；
- 商店元数据、隐私声明、截图和审核材料；
- 当前 App 与最新固件组合的完整安全回归。

结论：**开发调试 GO；TestFlight/App Store NO-GO。**

### 2.2 Android

当前已有：

- Expo Prebuild 生成的本地 Android 工程；
- `com.dong4j.deskgateway` Application ID；
- `versionName=1.0.0`、`versionCode=1`；
- Android 36 target；
- 小米 11 Ultra Debug Development Build 编译和安装结果。

当前缺少：

- 正式 upload key/keystore；
- 签名 Preview APK；
- Production AAB；
- Google Play Console App 和 Play App Signing；
- Internal testing 安装记录；
- Android BLE、REST、后台和异常停止完整矩阵；
- Data safety、隐私政策和商店材料。

当前生成的 `android/app/build.gradle` 中，Release 仍使用 debug signing config，不能作为
正式签名依据。`android` 是 Expo 生成并忽略的目录，持久发布配置必须放在 Expo app config、
`eas.json` 或受控 config plugin 中。

结论：**Debug 真机安装 GO；签名内测/Google Play NO-GO。**

### 2.3 Apple Watch

当前已有：

- 原生 SwiftUI + CoreBluetooth + URLSession Watch-only App；
- `com.dong4j.deskgateway.watch` Bundle ID；
- `WKWatchOnly` 独立运行形态；
- Swift 单元测试、Simulator Mock 和无签名通用 watchOS 构建；
- Xcode 真机安装操作说明。

当前缺少：

- Distribution 签名的 Release Archive；
- Organizer Validate/Upload 验证；
- App Store Connect App record；
- watchOS TestFlight 记录；
- Watch 扫描、配对、Notify、Crown STOP、断网租约和三客户端真机矩阵；
- 商店元数据、截图、隐私和审核材料。

Apple 在 App Store Connect 中把 Watch-only App 归入 iOS platform，但它仍使用自己的
Watch Bundle ID 和 Watch-only 构建。当前 XcodeGen 单 Target 工程能通用编译，不代表
Archive 一定满足 App Store 包装要求；第一次 Archive、Validate 和 Upload 是独立门禁。

结论：**代码/测试/通用构建 GO；TestFlight/Watch App Store NO-GO。**

## 3. 统一版本与构建号

三个平台对外版本保持一致，例如 `1.0.0`：

| 平台 | 用户可见版本 | 开发者构建号 | 约束 |
|---|---|---|---|
| iPhone | Expo `version` / `CFBundleShortVersionString` | `ios.buildNumber` / `CFBundleVersion` | 同一版本每次上传必须递增 |
| Android | Expo `version` / `versionName` | `android.versionCode` | Google Play 中必须严格递增 |
| Watch | `CFBundleShortVersionString` | `CFBundleVersion` | 同一版本每次上传必须递增 |

建议使用以下编号方式：

```text
产品版本：1.0.0
iPhone build：1、2、3...
Android versionCode：1、2、3...
Watch build：1、2、3...
```

三端构建号不要求数值相同，但发布记录必须能对应到同一个 Git commit。不得复用已经上传
到 App Store Connect 或 Google Play 的构建号。

如果采用 EAS Build，建议由 `eas.json` 设置 remote version source，并只对 production
profile 开启 `autoIncrement`。EAS 远程版本不会自动写回本地 app config，发布记录仍需保存
EAS build ID 和商店实际构建号。

## 4. 统一发布前门禁

### 4.1 源码和自动化

开始制作候选包前必须满足：

```bash
git status --short

cd mobile/app
npm run typecheck
npm test
npm run doctor

cd ../watch
swift test
xcodegen generate
xcodebuild -project DeskGatewayWatch.xcodeproj \
  -scheme DeskGatewayWatch \
  -destination 'generic/platform=watchOS' \
  CODE_SIGNING_ALLOWED=NO build
```

要求：

- 工作区不存在误带入的修改；
- 移动端类型检查和测试通过；
- `expo-doctor` 不存在未解释的失败；
- Watch 测试和无签名通用构建通过；
- 发布 commit 已冻结；
- 三端对外版本和构建号已记录。

### 4.2 固件与真机

正式候选包必须绑定明确的固件 commit/build，不能写 `latest`。至少完成：

1. iPhone、Android、Watch 分别扫描、配对、重连和订阅 State；
2. 三端 Client Info 分别为 iOS `01 02`、Android `01 03`、Watch `01 01`；
3. STOP、短时升降、档位和童锁裁决符合协议；
4. 松手、App 退后台、关闭蓝牙、断连和被杀死后桌体停止；
5. BLE 不可用时 REST 回退不重放旧运动；
6. iPhone、Android、Watch 同时在线时所有权、Busy 和任意 STOP 正确；
7. 配对窗口、单删、全删和 bond 恢复正确；
8. 番茄时钟、语音设置和设备配置不触发意外运动；
9. 双 ToF 上升限制和真实桌体 STOP 矩阵完成。

详细签署入口使用 [`docs/12-v1-release-acceptance.md`](../12-v1-release-acceptance.md)。自动化、
模拟器、通用构建或一次安装成功都不能替代这些真机门禁。

## 5. iPhone：TestFlight 到 App Store

### 5.1 前置条件

- Apple Developer Program 会员有效；
- App Store Connect 协议和账号验证已完成；
- `com.dong4j.deskgateway` App ID 和 Distribution 能力可用；
- App Store Connect 已创建对应 App record；
- Xcode 已登录正确 Apple Account；
- 隐私政策和支持页面有稳定 HTTPS 地址。

自 2026-04-28 起，iOS App 上传需要使用 iOS 26 SDK 或更高版本构建。当前项目应继续使用
`/Applications/Xcode.app` 中的 Xcode 26 制作正式 Archive，不能把 Xcode 27 Beta Debug
安装成功当成生产兼容证据。

### 5.2 准备版本和原生工程

发布前确认 Expo app config 中至少包含：

```json
{
  "expo": {
    "version": "1.0.0",
    "ios": {
      "bundleIdentifier": "com.dong4j.deskgateway",
      "buildNumber": "1"
    }
  }
}
```

生成并同步原生工程：

```bash
cd /Users/dong4j/Developer/1.AI/ai-incubator/desk-gateway/mobile/app
npx expo prebuild --platform ios

cd ios
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer pod install
open -a Xcode DeskGateway.xcworkspace
```

`ios` 目录由 Expo 生成且不提交。权限、Bundle ID、Team 和 Info.plist 持久配置必须回写
`app.json` 或 config plugin；不要只修改生成工程。

### 5.3 Archive 和上传

在 Xcode 26 中：

1. Scheme 选择 `DeskGateway`；
2. Run Destination 选择 Any iOS Device/Generic iOS Device；
3. Signing & Capabilities 选择 Team `8WCUMGCWMB` 并启用自动签名；
4. Build Configuration 使用 Release；
5. 执行 Product → Archive；
6. 在 Organizer 中先执行 Validate App；
7. 通过后选择 Distribute App → App Store Connect → Upload；
8. 保存 Archive、上传时间、版本、build 和发布 commit。

当前 `npm run ios:device` 只生成 Debug `.app` 并安装真机，不能代替 Archive，也不能生成
可提交 App Store 的 `.ipa`。

### 5.4 TestFlight

构建处理完成后：

1. 填写 Beta App Description、测试重点和反馈邮箱；
2. 先加入 Internal TestFlight；
3. 从 TestFlight 安装，不使用 Xcode 覆盖安装；
4. 断开 Metro，执行完整 iPhone 真机矩阵；
5. 记录崩溃、反馈和构建状态；
6. 需要外部测试时再提交 Beta App Review。

TestFlight 支持最多 100 名 App Store Connect 内部测试者和最多 10,000 名外部测试者；
外部测试首个构建通常需要 Beta App Review。TestFlight 构建最多测试 90 天。

### 5.5 App Store 提交

提交审核前完成：

- 名称、副标题、描述、关键词和分类；
- iPhone 截图和 App Icon；
- Support URL、Marketing URL（如使用）和 Privacy Policy URL；
- App Privacy 数据处理声明；
- 年龄分级、价格、地区和内容权利；
- 出口合规问题；
- App Review 联系人和备注；
- 为审核人员说明本 App 只控制用户局域网中的硬件，并提供可复现的审核路径。

审核说明不得要求审核人员实际操作危险家具。应提供安全的只读/模拟说明、演示视频或审核
账号，并明确真实升降需要用户自有 Desk Gateway 硬件。

## 6. Android：签名 APK 到 Google Play

### 6.1 前置条件

- Google Play Developer 账号及身份验证完成；
- Play Console 创建 `com.dong4j.deskgateway` App；
- 固定包名、默认语言、免费/付费属性和地区；
- Play App Signing 已启用；
- upload key 已创建并安全保存；
- 隐私政策和支持页面有稳定 HTTPS 地址。

Google Play 使用两级密钥：upload key 用于签名上传的 AAB，app signing key 由 Google
用于签名最终分发给用户的 APK。两把私钥都不能进入 Git；本地只需要保管 upload key。

### 6.2 推荐 EAS 配置

当前工程使用 Expo CNG，推荐用 EAS Build 管理 Preview APK 和 Production AAB。目标配置：

```json
{
  "cli": {
    "appVersionSource": "remote"
  },
  "build": {
    "preview": {
      "distribution": "internal",
      "android": {
        "buildType": "apk"
      }
    },
    "production": {
      "autoIncrement": true,
      "android": {
        "buildType": "app-bundle"
      }
    }
  }
}
```

初始化命令：

```bash
cd /Users/dong4j/Developer/1.AI/ai-incubator/desk-gateway/mobile/app
npx eas-cli@latest login
npx eas-cli@latest build:configure
```

第一次配置凭据时可以让 EAS 创建 Android keystore，或者上传已经安全保存的 upload key。
选择后必须记录凭据归属和恢复方式，不能在后续构建中随意生成第二把 key。

### 6.3 签名 Preview APK

先生成不依赖 Metro、可以直接安装的内部候选包：

```bash
npx eas-cli@latest build --platform android --profile preview
```

下载 APK 后验证签名、版本和哈希，再安装到 Android 真机。由于当前 Debug App 使用 debug
key，而 Preview 使用正式 keystore，同包名覆盖安装可能报
`INSTALL_FAILED_UPDATE_INCOMPATIBLE`。卸载 Debug App 会清除本地设置，应先记录网关地址、
REST 密码和验收基线。

Preview APK 必须在不运行 Metro 的情况下完成 Android 真机矩阵。Preview 通过不等于已经
满足 Google Play 提交要求。

### 6.4 Production AAB

生成 Google Play 候选包：

```bash
npx eas-cli@latest build --platform android --profile production
```

下载并归档 `.aab`、EAS build ID、`versionCode`、Git commit 和 SHA-256。AAB 不能直接用
`adb install`；必须通过 Google Play 测试轨道，或使用 Google 提供的 bundle 工具生成
设备 APK。

### 6.5 Internal testing

在 Play Console 中：

1. 打开 Testing → Internal testing；
2. 建立测试者邮箱列表；
3. 上传 Production AAB；
4. 填写 release notes；
5. 发布到 Internal testing；
6. 测试者通过邀请链接加入并从 Google Play 安装；
7. 验证升级、重装、权限、BLE、REST 和后台停止。

Internal testing 支持最多 100 名测试者。新建个人开发者账号可能还需要满足 Google Play
规定的测试要求才能进入 Production，具体以 Play Console 当前提示为准。

### 6.6 Production

进入 Production 前完成：

- Store listing、图标、Feature Graphic、手机截图和说明；
- Privacy Policy URL；
- Data safety；
- App access、Ads、Content rating、Target audience；
- 地区、价格和设备兼容性；
- Android developer verification；
- Internal/Closed testing 结果；
- 分阶段发布比例和停止条件。

Data safety 必须覆盖 App 自身和第三方 SDK 的实际数据行为。即使结论是不收集数据，也要
基于代码、SDK 和网络请求证据填写，不能凭产品直觉声明。

## 7. Apple Watch：TestFlight 到 Watch App Store

### 7.1 当前产品形态

当前 Watch 是 `WKWatchOnly` App，不依赖 iPhone companion App。用户可以直接在 Apple
Watch App Store 获取并独立运行。它与 iPhone App 使用不同 Bundle ID，按当前工程形态
作为独立 Watch-only 产品发布。

如果以后希望 iPhone 与 Watch 作为同一个 App Store 产品、统一购买或自动随 iPhone App
安装，需要把 Watch target 纳入 iOS App 的可发布工程并重新确定 Bundle ID/Universal
Purchase 关系。这是工程和商店模型变更，不属于当前发布操作。

### 7.2 前置条件

- Apple Developer Program 和 App Store Connect 可用；
- `com.dong4j.deskgateway.watch` App ID 可用于 Distribution；
- App Store Connect 创建 Watch-only App record；
- 当前 Team、Bundle ID、版本和 build 已持久写回 `project.yml`/`App/Info.plist`；
- Xcode 支持目标 watchOS 和 App Store 当前 SDK 要求；
- Watch App Icon、隐私、支持页面和截图齐全。

App Store Connect 创建记录时，Watch-only App 归入 iOS platform。Bundle ID 一旦上传构建
后不能随意更改。

### 7.3 Release Archive

生成工程并打开：

```bash
cd /Users/dong4j/Developer/1.AI/ai-incubator/desk-gateway/mobile/watch
xcodegen generate
open -a Xcode DeskGatewayWatch.xcodeproj
```

在 Xcode 26 中：

1. 选择 `DeskGatewayWatch` Scheme；
2. 选择 Generic/Any watchOS Device；
3. 检查 Team、Bundle ID、Marketing Version 和 Build；
4. Build Configuration 使用 Release；
5. 执行 Product → Archive；
6. 在 Organizer 执行 Validate App；
7. 通过后上传 App Store Connect；
8. 保存 Archive 和上传记录。

如果 Organizer 不把当前单 Target XcodeGen 工程识别为可分发 Watch-only Archive，或者
Validate 报缺少包装 Target、Info.plist、图标或签名关系，发布状态保持 NO-GO，并根据
Xcode 当前 Watch-only 模板调整工程结构。不得因为无签名通用构建成功而绕过 Validate。

### 7.4 watchOS TestFlight

上传处理完成后：

1. 配置 TestFlight 测试说明；
2. 先添加内部测试者；
3. 在真实 Apple Watch 上通过 TestFlight 安装；
4. 确认不安装 iPhone companion 仍能完成权限请求、BLE 和 REST 配置；
5. 验证 Crown、STOP、断网租约、触感和后台恢复；
6. 与 iPhone、Android 同时执行三客户端矩阵；
7. 记录 Watch 型号、watchOS 版本、构建号和测试结果。

### 7.5 Watch App Store

提交前准备：

- Watch App 名称、描述、关键词和分类；
- Apple Watch 尺寸要求对应的截图；
- Privacy Policy 和 App Privacy；
- 年龄分级、地区、价格和支持信息；
- App Review 说明和硬件依赖说明；
- 不依赖 iPhone companion 的独立运行证明。

## 8. 隐私、权限与审核说明

发布前必须审计：

- BLE 扫描、连接和 Bond 信息是否离开设备；
- REST 地址和密码的本地保存位置；
- 是否存在日志、崩溃报告、统计或第三方 SDK 上报；
- 局域网 IP、设备名称、固件版本和使用行为是否构成收集数据；
- Expo、React Native 和原生依赖是否引入新的数据处理；
- App 是否使用受出口合规问题影响的加密能力。

当前产品使用蓝牙和局域网控制用户自有硬件。商店说明应直接写清：

- App 需要 Desk Gateway 硬件；
- BLE 和局域网权限的用途；
- App 不应暴露到公网；
- 家具运动需要用户现场看护；
- 上升限制由固件最终裁决，App 不是安全边界。

“不收集数据”只能在完成代码、依赖和网络请求审计后填写。Apple App Privacy 和 Google
Play Data safety 必须与实际版本一致。

## 9. 凭据管理

以下文件和内容禁止提交 Git：

- Android keystore、store password、key alias password；
- Google Play Service Account JSON；
- Apple Distribution 私钥和导出的 `.p12`；
- Provisioning Profile 的私有备份；
- App Store Connect API `.p8`；
- Expo、Apple、Google 的访问 token。

推荐存储位置：

- EAS Credentials；
- macOS Keychain；
- 受控 CI Secret Manager；
- 加密离线备份。

发布记录只保存证书指纹、key alias、有效期、负责人和恢复位置，不保存明文密码或私钥。

## 10. 发布顺序

统一执行顺序：

1. 冻结版本、构建号、发布 commit 和固件 build；
2. 自动化测试通过；
3. 生成三端签名候选包；
4. 保存产物、哈希和构建日志；
5. Android Preview APK、iPhone TestFlight、watchOS TestFlight 完成基础安装；
6. 完成三端单机和三客户端真实桌体矩阵；
7. Android AAB 进入 Internal testing；
8. 三端商店元数据和隐私材料复核；
9. 分别提交 Apple App Review 和 Google Play Production；
10. 审核通过后分阶段发布并监控崩溃、反馈和安全问题。

iPhone、Android、Watch 可以使用同一产品版本，但每个平台独立审核、独立上线。某个平台
通过不能解除其他平台的门禁。

## 11. 回滚与热修复

商店发布后不能通过降低 build/versionCode 覆盖已发布版本。出现问题时：

- Apple：暂停自动发布或下架可用性，修复后用更高 build 提交新版本；
- Google Play：停止/暂停 staged rollout，修复后用更高 versionCode 上传新 AAB；
- TestFlight/Internal testing：停止旧测试构建，明确标记失效版本；
- 安全问题：同步更新发布说明、支持页面和硬件操作警告。

当前 App 没有远程强制停用机制。涉及运动安全的缺陷不能只依赖商店更新速度，应立即停止
相关版本分发，并通过用户可见渠道说明风险和临时规避措施。

## 12. 发布记录模板

| 项 | iPhone | Android | Watch |
|---|---|---|---|
| 产品版本 |  |  |  |
| 构建号/versionCode |  |  |  |
| App commit |  |  |  |
| 固件 commit/build |  |  |  |
| 构建工具版本 |  |  |  |
| 产物/EAS build/Archive ID |  |  |  |
| SHA-256 |  |  |  |
| 内测渠道 | TestFlight | Internal testing | TestFlight |
| 内测结果 |  |  |  |
| 真机验收记录 |  |  |  |
| 商店提交时间 |  |  |  |
| 审核状态 |  |  |  |
| 正式发布时间 |  |  |  |

发布完成的最低定义：产物签名正确、商店内测通过、真实硬件门禁签署、元数据与隐私声明
一致、商店审核通过并按计划上线。缺少任一项时只能报告对应阶段完成，不能报告三端正式
发布完成。

## 13. 官方参考

### Apple

- [App Store 当前提交要求](https://developer.apple.com/app-store/submitting/)
- [创建 App Store Connect App record](https://developer.apple.com/help/app-store-connect/create-an-app-record/add-a-new-app/)
- [上传构建](https://developer.apple.com/help/app-store-connect/manage-builds/upload-builds/)
- [TestFlight](https://developer.apple.com/help/app-store-connect/test-a-beta-version/testflight-overview/)
- [App Privacy](https://developer.apple.com/help/app-store-connect/manage-app-information/manage-app-privacy/)
- [提交 App Review](https://developer.apple.com/help/app-store-connect/manage-submissions-to-app-review/overview-of-submitting-for-review/)
- [独立 Watch-only App](https://developer.apple.com/documentation/watchos-apps/creating-independent-watchos-apps)

### Android / Google Play

- [Android App Signing](https://developer.android.com/studio/publish/app-signing)
- [Google Play 测试轨道](https://support.google.com/googleplay/android-developer/answer/9845334)
- [Google Play Data safety](https://support.google.com/googleplay/android-developer/answer/10787469)

### Expo

- [EAS Build](https://docs.expo.dev/build/setup/)
- [EAS build profiles](https://docs.expo.dev/build/eas-json/)
- [App version management](https://docs.expo.dev/build-reference/app-versions/)
- [Android Production Build](https://docs.expo.dev/tutorial/eas/android-production-build/)
- [Android APK](https://docs.expo.dev/build-reference/apk/)
