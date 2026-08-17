# Desk Gateway documentation index

**Language:** English · [简体中文](./README.zh-CN.md)

Pick an entry by what you need. Implementation status lives in [current status and priorities](./5-current-status-and-priorities.md). Whether V1 can ship lives in [V1 release acceptance](./12-v1-release-acceptance.md).

Phase 1 (panel emulation + platform skeleton + multi-client control) is done. Web, REST, UART, BLE, phone, Watch, keyboard, knob, voice, and Stream Deck-style keys can move the same Mxtark desk. Phase 2 original-panel proxy, disconnect STOP, arbitration, and child-lock lockout have passed on the real desk.

## Start here

| You want to | Doc |
|---|---|
| See what the project does and how to flash | Root [README.md](../README.md) |
| Move the desk with an existing client | [Control methods](./guides/control-methods.md) |
| Script or integrate over HTTP | [REST API](./guides/rest-api.md) |
| Wire, power, and debug hardware | [Bring-up checklist](./bringup-checklist.md) |
| See which hardware gates remain | [Current status and priorities](./5-current-status-and-priorities.md) |

## Usage and integrations

| Doc | What it covers |
|---|---|
| [Control methods](./guides/control-methods.md) | Web, REST, UART, phone, Watch, keyboard, knob, voice, Ulanzi |
| [REST API](./guides/rest-api.md) | Auth, motion, presets, child-lock, bonds, Pomodoro |
| [Keyboard, knob, and voice](./keyboard-voice-control.md) | GoatRemote, Karabiner, knob jog |
| [XiaoZhi AI desk control](./11-xiaozhi-ai-desk-control.md) | Voice → MCP → REST |
| [XiaoZhi firmware and local server](./10-xiaozhi-ai-firmware-and-local-server.md) | XiaoZhi hardware-side deploy |
| [Integrations overview](../integrations/README.md) | Third-party clients and their boundaries |
| [Ulanzi D200H plugin](../integrations/ulanzi-d200h/README.md) | Sit / stand / Pomodoro keys |
| [XiaoZhi MCP bridge](../integrations/xiaozhi-mcp/README.md) | Cloud endpoint to local REST |
| [Karabiner](../integrations/karabiner/README.md) | Keyboard and knob complex modifications |
| [GoatRemote](../integrations/goatremote/README.md) | Spoken sit / stand |

## Mobile

| Doc | What it covers |
|---|---|
| [Phone app](../mobile/app/README.md) | React Native capabilities and commands |
| [iOS device deploy](./guides/mobile-ios-device-deployment.md) | iPhone Development Build |
| [Android device deploy](./guides/mobile-android-device-deployment.md) | Android SDK / ADB |
| [Store release](./guides/mobile-watch-production-release.md) | iPhone, Android, Watch signing |
| [Apple Watch](../mobile/watch/README.md) | watchOS build, signing, hardware gates |

## Architecture and protocol

| Doc | What it covers |
|---|---|
| [Architecture overview](./architecture/overview.md) | Layers, hardware topology, child-lock, arbitration |
| [Platform design spec](./superpowers/specs/2026-08-06-desk-gateway-platform-design.md) | Implementation source of truth |
| [Protocol reverse notes](./3-protocol-reverse-notes.md) | mxtark I²C key codes and timing |
| [BLE Accessory Profile](./architecture/ble-accessory-profile.md) | UUIDs, byte protocol, LightBlue |
| [BLE three-client and bonds](./architecture/ble-multi-client-bond-management.md) | Ownership, pairing window, bond delete |
| [Mobile dual transport](./architecture/mobile-connection-transport.md) | BLE first, REST fallback |
| [Apple Watch control](./architecture/apple-watch-control.md) | Crown safe-stop and dual transport |
| [Dual ToF safety](./4-tof-distance-sensor-plan.md) | Height, side gap, preset closed loop |

## Hardware and firmware

| Doc | What it covers |
|---|---|
| [Requirements](./0-requirements.md) | Scope, phase gates, locked decisions |
| [MCU selection](./2-esp32-s3-n16r8-platform.md) | YD-ESP32-S3 N16R8 |
| [Firmware README](../firmware/desk-gateway/README.md) | Build, wiring, panel proxy, height policy |
| [Logic-analyzer capture](./1-protocol-capture-with-logic-analyzer.md) | Phase 0 flow |
| [Hardware I²C restore](./7-hardware-i2c-restoration-investigation.md) | Why the default is `@0x24` again |
| [OLED status](./9-oled-status-display.md) | SSD1306 wiring and pages |
| [Pomodoro voice](./6-pomodoro-reminder-plan.md) | Local timer and I2S voice |

## Later ecosystem

These have designs. They are not delivered features:

| Doc | What it covers |
|---|---|
| [MQTT / Home Assistant](./architecture/mqtt-home-assistant.md) | Cover / Number entity plan |
| [Xiaomi / Huawei](./architecture/ecosystem-xiaomi-huawei.md) | Native module vs Matter |

## Engineering standards

| Doc | What it covers |
|---|---|
| [Standards](./standards/README.md) | Mandatory agent rules |
| [Git commit convention](./standards/git-commit-convention.md) | Conventional Commits and staging bounds |
