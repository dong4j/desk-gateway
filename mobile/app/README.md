# Desk Gateway Mobile

**Language:** English · [简体中文](./README.zh-CN.md)

Cross-platform phone app for Desk Gateway. React Native, Expo Development Build, and TypeScript.

Phase 1 multi-client control is available. Android and the three-client concurrent matrix have passed on hardware. Home / Pomodoro / Settings follow the confirmed prototype and share one client for BLE GATT and LAN REST. Decisions and hardware gates: [`docs/architecture/mobile-app-technology-selection.md`](../../docs/architecture/mobile-app-technology-selection.md).

BLE-first, Wi-Fi fallback, mDNS, and the LAN security boundary: [`docs/architecture/mobile-connection-transport.md`](../../docs/architecture/mobile-connection-transport.md).

Status and remaining work: [`docs/5-current-status-and-priorities.md`](../../docs/5-current-status-and-priorities.md).

How to move the desk: [`docs/guides/control-methods.md`](../../docs/guides/control-methods.md).

First iPhone install: [`docs/guides/mobile-ios-device-deployment.md`](../../docs/guides/mobile-ios-device-deployment.md).

First Android install: [`docs/guides/mobile-android-device-deployment.md`](../../docs/guides/mobile-android-device-deployment.md).

Signing and store builds for iPhone, Android, and Watch: [`docs/guides/mobile-watch-production-release.md`](../../docs/guides/mobile-watch-production-release.md).

## Current capabilities

- Scan and connect to an ESP32 advertising `DeskGateway`.
- Auto mode prefers BLE; if BLE fails or drops, fall back to REST at `desk-gateway.local`.
- Settings can force Auto / BLE-only / Wi-Fi-only and store the REST URL plus `X-Desk-Key`.
- Discover the Desk Accessory Service.
- Pairing handshake writes Client Info `01 02` on iOS and `01 03` on Android. Do not use STOP as a handshake.
- Read and subscribe to the fixed 8-byte State characteristic.
- Read standard Device Information `180A/2A26` and show the firmware build time.
- Encrypted Command characteristic: STOP, HOLD, and two presets.
- Stop HOLD renewals when the app leaves the foreground.
- Fail closed on unknown protocol version, length, or state.
- Home shows live height, desk animation, hold controls, STOP, presets, and child-lock.
- Home and Settings both write child-lock and only display the value read back from the ESP32.
- Settings stores max safe height, REST / Bluetooth / Panel source ACL, and can reboot the gateway.
- Settings also covers transport, local auto-connect, and haptics. The whole row is tappable; nested touch targets are not used.
- The bonded-device card uses REST to show online/controlling state, open the 120 s pairing window, and delete one or all bonds with confirm + retry.
- Desk Busy `0x80` shows “another device is controlling” while keeping the BLE connection and notify subscription.
- Older firmware without Config can still move the desk; device settings stay disabled.
- Home links to Pomodoro. That page shows ESP remaining time and sends seven fixed actions. The phone does not run a second timer.
- BLE reads/subscribes Reminder v1. Wi-Fi reads the same snapshot from `GET /api/v1/desk/status`.
- Duration, voice, volume, and preview save over authenticated REST. BLE desk-control mode still needs the LAN management channel.

## Development commands

BLE uses a native module. **Do not use Expo Go.**

```bash
npm install
npm start
```

`npm start` only runs Metro. First install or a native-dependency change also needs a platform build. iOS 27 Beta devices must use `npm run ios:device` below.

### Android device

```bash
npm run android:device
npm start
```

`android:device` checks `ANDROID_HOME` / `adb`, selects an authorized phone, builds the Debug Development Build, and reverses Metro port `8081`. Pass a serial if more than one device is connected:

```bash
npm run android:device -- emulator-5554
```

The first run generates the gitignored `android/` tree. If the Android toolchain is missing, the script fails before Gradle. Do not bypass that with Expo Go.

### iOS 27 Beta device

Expo SDK 57 / React Native 0.86 has not adopted the UIScene lifecycle that the iOS 27 SDK requires. `npm run ios` under Xcode 27 can install the app, then it exits before React Native starts.

This machine keeps Xcode 26.6 (`/Applications/Xcode.app`) and Xcode 27 Beta (`/Applications/Xcode-beta.app`). For an iOS 27 phone:

```bash
npm run ios:device
npm start
```

`ios:device` builds with Xcode 26.6 / iOS 26 SDK, then installs and launches with Xcode 27 device support. Optional device id:

```bash
npm run ios:device -- 00008101-0000000000000000
```

This is a bounded upstream workaround, not a fake UIScene port. Remove the script once Expo / React Native supports iOS 27 and go back to `npm run ios`.

Static checks:

```bash
npm run typecheck
npm test
npm run doctor
```

## Hardware gates

TypeScript, a Metro bundle, or a simulator passing does **not** prove BLE. Before freezing the BLE library, do this on a real iPhone and a real Android phone:

1. Scan, connect, and discover services.
2. First encrypted Client Info write / pairing.
3. State Notify.
4. HOLD renewals and release STOP.
5. STOP after disconnect, background, and Bluetooth off.
6. Bond restore after ESP32 and app restart.
7. Motion ownership, any-client STOP, and bond-delete with iPhone + Apple Watch + Android online together.
8. Pomodoro actions on BLE and Wi-Fi, foreground/background resume, duration, mute, volume, and voice preview.

Command / State v1 stay unchanged. Config / System characteristics carry device settings and reboot. iOS and Android already have real BLE motion, the shipping screens, and settings writes. Three-client concurrency has passed. BLE/Wi-Fi out-of-range fallback and store beta builds are still open.
