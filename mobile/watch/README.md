# Desk Gateway Watch

**Language:** English · [简体中文](README.zh-CN.md)

Independent watchOS app. Apple Watch talks to Desk Gateway over BLE or LAN REST. Product interaction, Digital Crown stop timing, Pomodoro Reminder v1, and hardware gates: [`docs/architecture/apple-watch-control.md`](../../docs/architecture/apple-watch-control.md).

Signing and store builds for iPhone, Android, and Watch: [`docs/guides/mobile-watch-production-release.md`](../../docs/guides/mobile-watch-production-release.md).

## Tools and project boundary

The Watch project uses Swift Package Manager and XcodeGen. Before starting:

```bash
xcodebuild -version
xcodegen --version
swift --version
```

Install XcodeGen with `brew install xcodegen` if needed. Xcode must support the watchOS version on the target Apple Watch. `swift test` does not need a development signature. Installing on hardware needs an Xcode account, Team, provisioning profile, and a registered device.

`project.yml` is the source of truth. Generated `DeskGatewayWatch.xcodeproj` is not committed. Do not persist Team, Bundle ID, Info.plist, or Build Settings only in the generated project — the next `xcodegen generate` will overwrite them.

## Local verification

From the repository root:

```bash
cd mobile/watch
swift test
xcodegen generate
xcodebuild -project DeskGatewayWatch.xcodeproj \
  -scheme DeskGatewayWatch \
  -destination 'generic/platform=watchOS' \
  CODE_SIGNING_ALLOWED=NO build
```

`swift test` covers GATT, REST state mapping, and the Crown state machine. A generic watchOS build only proves SwiftUI / CoreBluetooth / URLSession compile. It does not replace scan, pairing, LAN, Crown, haptics, or real motion on a Watch.

`CODE_SIGNING_ALLOWED=NO` does not produce a signed app and does not install anything. Hardware install still needs the signing and Xcode Run steps below.

## Simulator Debug Mock

A Watch Simulator Debug run uses a local mock and does not scan BLE. The orange “模拟” badge means there is no real desk:

- Initial height `72.0 cm`, ceiling `94.0 cm`.
- Digital Crown simulates continuous up/down. Watchdog renews every `250 ms`; `500 ms` without input sends STOP.
- Sit moves to `55 cm`, stand to `87 cm`.
- STOP during motion aborts the mock immediately.
- The timer button opens Pomodoro. The simulator only flips discrete action states; it does not run a local countdown.

The mock is compiled in only under `DEBUG && targetEnvironment(simulator)`. Watch hardware Debug and every Release build always use `DeskConnectionManager`. There is no runtime fallback to mock after a connection failure.

Suggested simulator order:

1. Confirm “已连接” plus orange “模拟”, height `72.0 cm`, ceiling `94.0 cm`.
2. Rotate the Crown; height should change, then return to “已连接” after stop.
3. Tap sit or stand; height should move and STOP should appear.
4. Tap STOP during motion; height must freeze.

## Install on a physical Apple Watch

### 1. Prerequisites

- watchOS 11 or later, paired with an iPhone.
- Xcode on the Mac must support that watchOS version.
- Sign in under Xcode → Settings → Accounts.
- Enable Developer Mode on both iPhone and Apple Watch (Settings → Privacy & Security). First enable requires a reboot; on Watch choose Turn On, then Trust.
- Keep the Watch unlocked and near the paired iPhone and Mac.

Apple docs:

- [Enabling Developer Mode on a device](https://developer.apple.com/documentation/xcode/enabling-developer-mode-on-a-device)
- [Managing devices in Device Hub](https://developer.apple.com/documentation/xcode/managing-your-simulated-and-physical-devices-in-device-hub)

### 2. Generate and open the Xcode project

This directory does not commit `.xcodeproj`. Regenerate after `project.yml` changes:

```bash
cd mobile/watch
xcodegen generate
open DeskGatewayWatch.xcodeproj
```

This is a `WKWatchOnly` independent Watch app. You do not install an iPhone companion first.

### 3. Signing

In Xcode, Target `DeskGatewayWatch` → Signing & Capabilities:

1. Enable Automatically manage signing.
2. Pick a Team available to the signed-in Apple ID.
3. Keep the Bundle Identifier unique for that team.

Defaults in `project.yml`:

```text
DEVELOPMENT_TEAM = 8WCUMGCWMB
PRODUCT_BUNDLE_IDENTIFIER = com.dong4j.deskgateway.watch
```

If the Team does not match, edit `project.yml` and run `xcodegen generate` again.

### 4. Prepare the Watch in Device Hub

1. Xcode → Open Developer Tool → Device Hub, or Manage Devices… from the run destination menu.
2. Cable-connect the paired iPhone if needed and trust this computer.
3. Select the Apple Watch under Physical Devices.
4. Follow Developer Mode / Trust / registration prompts.
5. Wait until Preparing finishes and the device is Ready.

See [Running your app on simulated or physical devices](https://developer.apple.com/documentation/xcode/running-your-app-on-simulated-or-physical-devices).

### 5. Install and run

1. Scheme: `DeskGatewayWatch`.
2. Destination: the physical Apple Watch, not Simulator or Any watchOS Device.
3. Run (`⌘R`).
4. Wait for sign, install, and launch.

Hardware Debug always uses `DeskConnectionManager` for BLE / REST. The orange mock badge must not appear.

### 6. When to regenerate

| Change | `swift test` | `xcodegen generate` | Xcode Build / Run |
|---|---:|---:|---:|
| Protocol or state machine in `Sources/` | yes | no | yes |
| Tests in `Tests/` | yes | no | no |
| SwiftUI / BLE / REST in `App/` | as needed | no | yes |
| `App/Info.plist` or assets | no | no | yes |
| `project.yml`, Team, Bundle ID, Build Settings | as needed | yes | yes |
| New Watch or app deleted | no | no | yes |

Close the old project after `project.yml` changes, regenerate, then reopen. Ordinary Swift edits do not need regenerate.

### 7. First BLE acceptance

Open the 120 s pairing window on authenticated Web or phone Settings first. Gateway must be powered and advertising:

1. Allow Bluetooth on first launch.
2. Wait for “已连接” and a height that matches the real desk.
3. Watch writes Client Info `01 01`. Allow the system pairing prompt. A normal handshake does not send STOP.
4. Keep a hand on the original controller; rotate Crown briefly, then tap STOP immediately.
5. After the stop path is proven: up, down, 250 ms watchdog renewals, 500 ms idle STOP.
6. Sit preset 1, stand preset 4, child-lock, and Bluetooth source deny.
7. With iPhone and Android online: non-owner shows “another device is controlling” but stays connected; any client STOP wins.
8. Pomodoro page: remaining time comes from ESP Notify. Start / pause / resume / skip / later / stop must not move the desk.
9. B12 no-motion hint once; reset disables every motion source for about 8 s.

Automation, Simulator, and screenshots do not close this gate.

### 8. First Wi-Fi / REST acceptance

1. Watch and gateway on the same LAN; `http://desk-gateway.local/` or a fixed IP must work.
2. Connection settings → Wi-Fi; store URL and REST password in Watch Keychain.
3. After reconnect, top bar shows “Wi-Fi”; height, presets, and Pomodoro come from the live gateway.
4. Up, down, reverse, STOP. Crown must use `/api/v1/desk/jog/up|down`.
5. Drop Watch network during motion; the last jog lease must auto-stop in about 500 ms.
6. Wrong password, REST source off, child-lock, upward blocked, and gateway reboot.
7. Auto: stay on BLE when BLE works; switch to Wi-Fi when BLE is gone. Do not replay motion across the switch.

A generic build only proves the REST path compiles.

### 9. Troubleshooting

| Symptom | Check |
|---|---|
| Xcode cannot see the Watch | Developer Mode on iPhone and Watch; Watch unlocked and near iPhone; Device Hub prompt |
| Preparing never finishes | Keep iPhone cabled, Watch unlocked; Xcode must support this watchOS |
| Signing / provisioning fails | Xcode account, Team, automatic signing, Bundle ID; persist changes in `project.yml` |
| Watch says Bluetooth unavailable | Watch Privacy settings; run destination must be the physical Watch |
| Never finds Desk Gateway | Gateway advertising; fewer than 3 centrals; first pair needs the 120 s window |
| Pairing / encryption fails | Delete the Watch bond in phone/Web, reopen the window, forget DeskGateway in Watch Bluetooth, reconnect |
| Another device is controlling | This Watch is not the BLE motion owner; status and STOP still work |
| Wi-Fi connect fails | Same LAN; try `desk-gateway.local` then IP; REST password and REST source ACL |
| Dual `WKWatchOnly` / `WKRunsIndependentlyOfCompanionApp` | Delete the old Watch app, keep only `WKWatchOnly`, Clean Build Folder, reinstall |

## Release boundary

This README covers Debug hardware install only. App Store still needs version, Distribution signing, Archive, privacy material, independent watchOS listing, and the real-device safety matrix. A generic build, Simulator mock, or one Xcode Run is not a release.
