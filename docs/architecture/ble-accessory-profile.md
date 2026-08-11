# BLE 外设扩展：旋钮 / OLED 等配件总线

| 项 | 内容 |
|---|---|
| 文档 | DG-ARCH-BLE-ACC-001 |
| 日期 | 2026-08-06 |
| 状态 | 架构约定（GATT UUID 实现时冻结） |
| 关联 | [平台设计定稿](../superpowers/specs/2026-08-06-desk-gateway-platform-design.md) |

## 1. 要解决什么

市场上已有大量 **OLED + 无极旋钮（及类似）** BLE 外设，很适合升降桌：

- OLED：**实时高度**、运动状态、童锁提示  
- 旋钮：旋转升/降、按下停止、长按档位等  

Desk Gateway **盒子本体**仍可不带旋钮/屏（纯网关），但平台必须把这类外设当成一等公民：通过 **标准化 BLE 数据面** 提供与 Web 同级的状态与控制，提高可扩展性——第三方或自研配件只对接 BLE Profile，不必懂桌厂协议。

```text
  [ OLED + 无限旋钮 / 其他 BLE 配件 ]
                 │ BLE GATT
                 ▼
          Desk Gateway（ESP32）
                 │
            desk_core
                 │
            desk_driver → 桌子
```

## 2. 角色约定（默认）

| 角色 | 谁 | 说明 |
|---|---|---|
| **GATT Server（Peripheral）** | Desk Gateway | 广播可发现；暴露 Desk Accessory Service；Notify 高度/状态 |
| **GATT Client（Central）** | 旋钮/OLED 外设、手机调试工具 | 订阅 Notify；Write 下发升降/停止 |

可选后续（不阻塞默认）：Gateway 作 **Central** 去连「只会当 Peripheral 的成品旋钮」——单独适配层，不替代上述开放 Profile。

## 3. 与现有通道的关系

| 通道 | 关系 |
|---|---|
| Web REST/短轮询 | **同一 `desk_core`**；语义对齐（up/down/stop/status/child_lock） |
| 原厂面板 | BLE 与面板均受全局童锁约束；解锁后再分别检查 Bluetooth / Panel 来源权限 |
| Matter / 米家 / 华为 | 生态轨；外设总线是 **本地配件轨**，互不替代 |
| 盒子本地旋钮/屏 | MVP **不做**；外设是可选配件，不是把旋钮焊进网关 |

**硬规则**：外设固件禁止直接谈 I²C/UART 桌厂协议；只走 Gateway BLE Profile → `desk_core`。

## 4. 数据面（逻辑，非最终 UUID）

外设至少要能：

| 方向 | 内容 |
|---|---|
| Gateway → 外设（Notify/Indicate） | `height_mm`（可空）、`height_known`、`status`（idle/up/down/…）、`child_lock`、`caps` |
| 外设 → Gateway（Write） | `stop`、`hold_up`、`hold_down`、`goto_preset(n)`（caps 允许时）、可选 `set_child_lock` |
| 发现 | 广播名如 `DeskGateway`；Service UUID 实现期分配并写入本文件修订版 |

推送节奏：BLE 状态变化立即 Notify；静止可 1–2s 心跳。Web 当前独立使用短轮询。无高度时外设 UI 可显示「—」或仅动画态，与 Web 动效策略一致。

## 5. 交互建议（给旋钮类配件的参考，非强制）

| 操作 | 建议映射 |
|---|---|
| 旋钮顺时针 | `hold_up`（或步进脉冲，由配件决定；松开/超时发 `stop`） |
| 旋钮逆时针 | `hold_down` |
| 短按 | `stop` |
| 双击 / 长按 | 档位 goto（若 caps 支持） |
| OLED | 大号高度 cm；升/降箭头；童锁图标 |

安全：配件侧也应有「松手停」；Gateway 侧仍有最长运动超时与急停优先。

## 6. 安全与配对

- 支持 BLE 配对/绑定（至少 Just Works；量产可升至 Passkey）。  
- 未绑定设备：可仅广播状态、拒绝运动 Write；或整段加密后再开放（实现时二选一，默认 **未绑定不可运动**）。  
- 与 Web 密码独立；物理近场假设强于公网，但仍防邻居乱控。

## 7. 交付阶段

| 阶段 | 内容 |
|---|---|
| 文档（本轮） | 约定外设总线目标与角色 |
| M3 / Phase 2 | `connectivity/ble`：GATT Server 雏形 + 与 `desk_core` 对接；可先用 nRF Connect / 简易旋钮固件联调 |
| 后续 | 开源「参考旋钮+OLED」固件或对接指南；可选 Central 模式适配成品外设 |

本能力 **不阻塞** Phase 1 Web；与 BLE stub 升级为正式 **Accessory Profile** 为同一组件演进。

## 8. 修订记录

| 版本 | 日期 | 说明 |
|---|---|---|
| 0.1 | 2026-08-06 | 初稿：Gateway 为 GATT Server；OLED/旋钮为典型外设；与 desk_core 对齐 |
