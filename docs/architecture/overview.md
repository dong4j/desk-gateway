# Desk Gateway 架构总览

> 精简给人读的版本。完整定稿见  
> [`docs/superpowers/specs/2026-08-06-desk-gateway-platform-design.md`](../superpowers/specs/2026-08-06-desk-gateway-platform-design.md)。

## 一句话

Desk Gateway 是一套跑在 ESP32-S3 上的**升降桌智能平台**：厂商差异收进 **Desk Driver**，Web / 串口 / **BLE 外设** / 未来 HA·Matter 共用 **desk_core**。

## 分层

```text
Web UI + REST/短轮询 + UART + BLE 外设（OLED/旋钮）
              │
         desk_core          ← 急停、超时、统一命令、童锁
              │
        desk_driver API
              │
   yourdesk_v1 / loctek* / jiecang* / …
```

## 当前状态

> 截至 2026-08-11：主固件可在 ESP-IDF 6.0.2 下编译通过。下面的“已实现”只表示代码已落地，
> 不代表真机验收完成；硬件结论以 [`bringup-checklist.md`](../bringup-checklist.md) 为准。

| 层 | 状态 |
|---|---|
| `yourdesk_v1`（I²C Slave 模拟面板） | 键通道已真机验证；实验性 GPIO 高度嗅探因干扰控桌而默认关闭 |
| `desk_core` + Driver 框架 | 已实现；含统一停止、运动超时、档位、全局童锁和来源权限 |
| WiFi + Web（局域网、密码、UI、升降动效） | 已实现；状态使用 250ms 短轮询，待板端/UI 验收 |
| 高度 | 完整 0–9 经验字模已锁定；真实总线接入待软件多地址 Slave，稳定版仅提供明确标记的 SIM |
| BLE / Loctek / Jiecang | BLE 仅有设计文档；Loctek / Jiecang 为 stub |
| 双 RJ45 中间人、面板仲裁、童锁真屏蔽 | 仲裁与权限代码已实现；原厂面板透传/真屏蔽仍待面包板抓包和真机验收 |
| HA / Matter / Siri / OTA | Phase 3+，未实现 |
| 米家 / 华为智慧生活 | Phase 3+；见 [生态调研](./ecosystem-xiaomi-huawei.md) |

## 仓库形状（目标）

```text
firmware/desk-gateway/
  components/
    desk_core/
    desk_driver/
    drivers/yourdesk_v1|loctek|jiecang
    connectivity/wifi|web|ble
docs/architecture/          ← 本目录
docs/superpowers/specs/     ← 设计定稿
```

## Web（本阶段）

- **仅局域网**；简单 Bearer 密码登录  
- 控制：升 / 降 / 停 / 已支持档位 / **童锁**  
- UI：品牌 + 升降桌示意图；`moving_up/down` 时示意图实时升降；有高度则按 mm 映射，无高度则按命令做相对动效  
- 状态：鉴权后的 `GET /api/v1/desk/status` 每 250ms 短轮询；含真实高度、`child_lock`、`control_sources`、安全上限和动态 `upward_blocked`  

## 童锁与仲裁

- 童锁 ON：除 `STOP` 和解除童锁外，REST、串口、未来蓝牙及原厂面板都不能启动或维持运动  
- 童锁 OFF：每个来源还需通过自己的 NVS 权限开关；当前 Web 可配置 REST、Bluetooth 和 Panel  
- 关闭任一来源会先停止当前运动；Panel 重新开放后必须先观察到物理按键松开，禁止解锁即误动作  
- 优先级：急停 > 全局童锁 > 来源权限 >（未锁且允许时）面板优先 > 其他入口  

## 待逆向（协议）

- 原厂面板：**同时按住上+下约 5 秒 → 重置**（已确认有此操作，`DR` 未知；见协议笔记）

## BLE 外设总线（摘要）

- Gateway = **GATT Server**；OLED + 无限旋钮等 = Client  
- Notify：高度 / 运动状态 / 童锁；Write：升 / 降 / 停  
- 与 Web 同走 `desk_core`；**板载**仍无旋钮无屏  
- 详情：[ble-accessory-profile.md](./ble-accessory-profile.md)

## 小米 / 华为生态（摘要）

| 目标 | 额外硬件？ |
|---|---|
| 米家 / 智慧生活 **原生上架** | **通常要** 各自认证 Wi‑Fi 模组 + 合作认证 |
| 手机 App 当 Matter 控制器添加本设备 | **一般不要**换主控（ESP32 上跑 Matter） |
| 详情 | [ecosystem-xiaomi-huawei.md](./ecosystem-xiaomi-huawei.md) |

## 相关文档

| 文档 | 用途 |
|---|---|
| [平台设计定稿](../superpowers/specs/2026-08-06-desk-gateway-platform-design.md) | 实现依据 |
| [小米/华为生态调研](./ecosystem-xiaomi-huawei.md) | 模组 vs Matter |
| [BLE 外设 Profile](./ble-accessory-profile.md) | 旋钮/OLED 配件总线 |
| [需求](../0-requirements.md) | 做什么、阶段门禁 |
| [主控板](../2-esp32-s3-n16r8-platform.md) | YD-ESP32-S3 N16R8 |
| [协议逆向](../3-protocol-reverse-notes.md) | yourdesk_v1 契约 |
| [Upsy Desky](https://github.com/tjhorner/upsy-desky) | 参考产品，非协议照搬 |
