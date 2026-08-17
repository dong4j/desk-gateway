# Desk Gateway documentation index

**Language:** English · [简体中文](./README.zh-CN.md)

Pick an entry by what you need. Implementation status lives in
[current status and priorities](./status/current-status-and-priorities.md).
Whether V1 can ship lives in
[V1 release acceptance](./status/v1-release-acceptance.md).

Phase 1 (panel emulation + platform skeleton + multi-client control) is done.
Web, REST, UART, BLE, phone, Watch, keyboard, knob, voice, and Stream Deck-style
keys can move the same Mxtark desk. Phase 2 original-panel proxy, disconnect
STOP, arbitration, and child-lock lockout have passed on the real desk.

## How this folder is organized

| Directory | What belongs here |
|---|---|
| [`status/`](./status/current-status-and-priorities.md) | Living truth: done / still to accept / V1 gates / product requirements |
| [`guides/`](./guides/control-methods.md) | How to use, deploy, and wire |
| [`architecture/`](./architecture/overview.md) | How the **current** system is built |
| [`hardware/`](./hardware/esp32-s3-n16r8.md) | Board, capture, I²C investigation |
| [`future/`](./future/mqtt-home-assistant.md) | Designs that are **not** shipped |
| [`history/`](./history/mobile-app-technology-selection.md) | Frozen drafts and old implementation plans |
| [`standards/`](./standards/README.md) | Mandatory agent / commit rules |
| [`images/`](./images/) | Photos used by protocol and hardware notes |

Architecture diagrams (PNG only) live in
[`architecture/images/`](./architecture/images/).

## Start here

| You want to | Doc |
|---|---|
| See what the project does and how to flash | Root [README.md](../README.md) |
| Point every client at your LAN gateway | [Local multi-client setup](./guides/local-multi-client-setup.en.md) |
| Move the desk with an existing client | [Control methods](./guides/control-methods.md) |
| Script or integrate over HTTP | [REST API](./guides/rest-api.md) |
| Wire, power, and debug hardware | [Bring-up checklist](./guides/bringup-checklist.md) |
| See which hardware gates remain | [Current status and priorities](./status/current-status-and-priorities.md) |

## Status

| Doc | What it covers |
|---|---|
| [Current status and priorities](./status/current-status-and-priorities.md) | Done vs still to accept |
| [V1 release acceptance](./status/v1-release-acceptance.md) | Release gates (currently **NO-GO**) |
| [Requirements](./status/requirements.md) | Scope, phase gates, locked decisions |

## Guides

| Doc | What it covers |
|---|---|
| [Control methods](./guides/control-methods.md) | Web, REST, UART, phone, Watch, keyboard, knob, voice, Ulanzi |
| [Local multi-client setup](./guides/local-multi-client-setup.en.md) | IP, password, and repo-path checklist |
| [REST API](./guides/rest-api.md) | Auth, motion, presets, child-lock, bonds, Pomodoro |
| [Bring-up checklist](./guides/bringup-checklist.md) | Wiring, power, hardware checks |
| [Keyboard, knob, and voice](./guides/keyboard-voice-control.md) | GoatRemote, Karabiner, knob jog |
| [XiaoZhi AI desk control](./guides/xiaozhi-ai-desk-control.md) | Voice → MCP → REST |
| [XiaoZhi firmware and local server](./guides/xiaozhi-firmware-and-local-server.md) | XiaoZhi hardware-side deploy |
| [iOS device deploy](./guides/mobile-ios-device-deployment.md) | iPhone Development Build |
| [Android device deploy](./guides/mobile-android-device-deployment.md) | Android SDK / ADB |
| [Store release](./guides/mobile-watch-production-release.md) | iPhone, Android, Watch signing |
| [Phone app](../mobile/app/README.md) | React Native capabilities and commands |
| [Apple Watch](../mobile/watch/README.md) | watchOS build, signing, hardware gates |
| [Integrations overview](../integrations/README.md) | Third-party clients and their boundaries |

## Architecture

| Doc | What it covers |
|---|---|
| [Architecture overview](./architecture/overview.md) | Layers, hardware topology, child-lock, arbitration |
| [Platform design spec](./architecture/platform-design.md) | Implementation source of truth |
| [Protocol reverse notes](./architecture/protocol-reverse-notes.md) | mxtark I²C key codes and timing |
| [BLE Accessory Profile](./architecture/ble-accessory-profile.md) | UUIDs, byte protocol, LightBlue |
| [BLE three-client and bonds](./architecture/ble-multi-client-bond-management.md) | Ownership, pairing window, bond delete |
| [Mobile dual transport](./architecture/mobile-connection-transport.md) | BLE first, REST fallback |
| [Apple Watch control](./architecture/apple-watch-control.md) | Crown safe-stop and dual transport |
| [Dual ToF safety](./architecture/tof-safety.md) | Height, side gap, preset closed loop |
| [OLED status](./architecture/oled-status-display.md) | SSD1306 wiring and pages |
| [Pomodoro voice](./architecture/pomodoro-reminder.md) | Local timer and I2S voice |

## Hardware

| Doc | What it covers |
|---|---|
| [MCU selection](./hardware/esp32-s3-n16r8.md) | YD-ESP32-S3 N16R8 |
| [Firmware README](../firmware/desk-gateway/README.md) | Build, wiring, panel proxy, height policy |
| [Logic-analyzer capture](./hardware/protocol-capture.md) | Phase 0 flow |
| [Hardware I²C restore](./hardware/i2c-restoration.md) | Why the default is `@0x24` again |

## Later ecosystem

These have designs. They are not delivered features:

| Doc | What it covers |
|---|---|
| [MQTT / Home Assistant](./future/mqtt-home-assistant.md) | Cover / Number entity plan |
| [Xiaomi / Huawei](./future/ecosystem-xiaomi-huawei.md) | Native module vs Matter |

## History

Not user manuals. Keep for design context:

| Doc | What it covers |
|---|---|
| [Mobile technology selection](./history/mobile-app-technology-selection.md) | React Native / Expo decisions |
| [M1/M2 implementation plan](./history/plans/2026-08-06-desk-gateway-m1-m2.md) | 2026-08-06 task plan |

## Engineering standards

| Doc | What it covers |
|---|---|
| [CHANGELOG](../CHANGELOG.md) | Unreleased snapshot; V1 is not tagged |
| [Contributing](../CONTRIBUTING.md) | IDF v6.0.2 install, layer checks, PR expectations |
| [Security](../SECURITY.md) | How to report vulnerabilities privately |
| [Support](../SUPPORT.md) | Issue channels and hardware evidence |
| [Standards](./standards/README.md) | Mandatory agent rules |
| [Git commit convention](./standards/git-commit-convention.md) | Conventional Commits and staging bounds |
