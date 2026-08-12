# BLE 外设扩展：旋钮 / OLED 等配件总线

| 项 | 内容 |
|---|---|
| 文档 | DG-ARCH-BLE-ACC-001 |
| 日期 | 2026-08-11 |
| 状态 | Command / State v1 + Config v2 / System 扩展已实现；LightBlue 核心控制和 iPhone Config v2 已真机验收；三客户端与 Bond 管理仅完成设计 |
| 关联 | [平台设计定稿](../superpowers/specs/2026-08-06-desk-gateway-platform-design.md) |
| 多客户端设计 | [BLE 三客户端并发与配对设备管理](./ble-multi-client-bond-management.md) |

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

## 4. 已冻结的 GATT Profile v1

Gateway 广播名为 `DeskGateway`，广播包包含 Desk Accessory Service UUID。

当前固件仍只允许一个 BLE Central。后续三客户端实现保持下表既有 UUID 和字节布局，
并以可选的 Client Info Characteristic 做向后兼容扩展；连接表、运动所有权和 Bond 管理
语义以[三客户端设计](./ble-multi-client-bond-management.md)为准。

| Attribute | UUID | Properties | 说明 |
|---|---|---|---|
| Desk Accessory Service | `7f4e0001-6d4c-4f4b-9f7a-3c1d2e5a9b10` | Primary Service | 本项目原生 Profile |
| Command | `7f4e0002-6d4c-4f4b-9f7a-3c1d2e5a9b10` | Write, Write Encrypted | 单字节控制指令；首次写入会触发 Just Works 配对 |
| State | `7f4e0003-6d4c-4f4b-9f7a-3c1d2e5a9b10` | Read, Notify | 固定 8 字节、小端序状态 |
| Config | `7f4e0004-6d4c-4f4b-9f7a-3c1d2e5a9b10` | Read, Notify, Write, Write Encrypted | 设备设置快照与单字段更新 |
| System | `7f4e0005-6d4c-4f4b-9f7a-3c1d2e5a9b10` | Write, Write Encrypted | 与运动命令隔离的管理指令 |
| Device Information Service | `180A` | Primary Service | Bluetooth SIG 标准设备信息服务 |
| Firmware Revision String | `2A26` | Read | ASCII：`构建日期 构建时间 @ Git版本` |

Device Information 是向后兼容的附加服务，不改变 Desk Accessory Service 的 v1
Command / State UUID 和字节布局。旧客户端可以完全忽略；新客户端读取失败时也不得阻断
状态订阅、配对或控制。

Command 不实现 BLE UART，也不兼容任何第三方 App 的私有协议。客户端应按 Hex
写入一个字节：

| Hex | 指令 | 行为 |
|---|---|---|
| `00` | STOP | 无条件停止；同时释放 BLE 运动所有权 |
| `01` | HOLD_UP | 开始或续期上升 |
| `02` | HOLD_DOWN | 开始或续期下降 |
| `11` | PRESET_1 | 闭环前往设备配置的档位 1（默认 64 cm） |
| `14` | PRESET_4 | 闭环前往设备配置的档位 4（默认 102 cm） |

未知指令、长度不为 1、童锁拒绝、Bluetooth 来源关闭或驱动不支持时，Write
返回 ATT 错误，不会绕过 `desk_core`。

### 4.1 HOLD 租约

`01` / `02` 不是无限保持命令。每次成功写入只获得默认 `750ms` 的运动租约：

- 客户端按住期间建议每 `250–400ms` 重复写入同一指令；
- 松手立即写 `00`；即使 `00` 丢失，租约到期仍会停止；
- BLE 断连、NimBLE stack reset、童锁开启或 Bluetooth 来源关闭都会停止；
- 下降仍有 `15s` 总运动超时；上升当前由松手或显式 STOP 停止，最高安全高度拦截暂时停用。

档位指令由真实高度闭环停止，不使用短租约；但在运动完成前 BLE 连接断开仍会
触发 STOP。

### 4.2 State 数据

| Byte | 内容 |
|---|---|
| `0` | 协议版本，v1 固定 `01` |
| `1` | 状态：`00 idle`、`01 moving_up`、`02 moving_down`、`03 goto_preset`、`04 error` |
| `2` | flags，见下表 |
| `3` | 保留，v1 固定 `00` |
| `4..5` | `height_mm`，uint16 little-endian；未知为 `FF FF` |
| `6..7` | `max_height_mm`，uint16 little-endian |

Flags：

| Bit | 含义 |
|---|---|
| `0` | `height_known` |
| `1` | `height_sim` |
| `2` | `child_lock` |
| `3` | Bluetooth 来源允许 |
| `4` | `upward_blocked` |

状态变化后最多约 `200ms` Notify；静止时每 `1s` 发送心跳。无高度时客户端显示
`—`，不得把 `FF FF` 当成实际高度。

### 4.3 Config 数据与写入

Config Read / Notify v2 固定返回 8 字节：

| Byte | 内容 |
|---|---|
| `0` | 协议版本，v2 固定 `02` |
| `1` | flags：bit0 童锁、bit1 REST、bit2 Bluetooth、bit3 原厂面板 |
| `2..3` | `max_height_mm`，uint16 little-endian |
| `4..5` | `preset1_height_mm`，uint16 little-endian |
| `6..7` | `preset4_height_mm`，uint16 little-endian |

Config Write 固定为 `[version, field, value_le16]`，每次只修改一个字段，避免客户端拿旧
快照覆盖 Web 或其他入口刚更新的设置：

| Field | 含义 | Value |
|---|---|---|
| `01` | 童锁 | `0` / `1` |
| `02` | REST 来源允许 | `0` / `1` |
| `03` | Bluetooth 来源允许 | `0` / `1` |
| `04` | 原厂面板来源允许 | `0` / `1` |
| `05` | 最高安全高度暂存值 | 毫米，有效范围 `640..1290`；当前不参与运动控制 |
| `06` | 档位 1（请坐）高度 | 毫米，须满足 `640 <= 档位1 < 档位4` |
| `07` | 档位 4（站立）高度 | 毫米，须满足 `档位1 < 档位4 <= 1290` |

固件仍接受 v1 的 4 字节 Config Write，以兼容已经发布的童锁、来源权限和最高安全高度
客户端；新增档位字段必须使用 v2。移动端可读取旧固件的 4 字节 v1 快照，并回退显示
64 cm / 102 cm，但只有收到 v2 快照后才允许修改档位。设备 NVS 是
配置唯一事实来源：Web 保存或 BLE 写入成功后，Config Notify 会把同一份真实值同步给 App。

管理写入不受童锁或 Bluetooth 来源开关阻断，否则客户端关闭 Bluetooth 后无法通过同一
加密连接重新开启；但这些设置只能改变策略，所有运动命令仍必须经过 `desk_core` 的童锁、
来源权限裁决。最高安全高度当前只保存和同步，不裁决运动；写入成功后 Config 会 Notify 最新真实快照。

### 4.4 System 管理命令

System 与桌体 Command 分离，避免把重启误解释成运动：

| Hex | 指令 | 行为 |
|---|---|---|
| `01` | RESTART | 先 STOP，返回 ATT Write Response 后延迟软重启 |

System Write 同样要求加密连接；未知值或错误长度会返回 ATT 错误。

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

- Command、Config 写入和 System characteristic 强制 `WRITE_ENC`；首次写入由 NimBLE 发起 **Just Works** 配对。
- `CONFIG_BT_NIMBLE_NVS_PERSIST=y`，绑定密钥跨重启保存；当前最多保存 3 个 bond，
  但同时只允许 1 个连接。
- State 可在未配对连接上读取和订阅，运动 Write 不允许明文连接。
- BLE 与 Web 密码独立；盒子没有屏幕和键盘，因此当前不能提供 MITM 认证。
- 已加密连接仍必须通过全局童锁和 Bluetooth 来源权限，STOP 继续保持最高优先级。

## 7. 使用 LightBlue 验收

1. 烧录后确认串口出现：

   ```text
   I (...) desk_ble: GATT ready lease=750 ms
   I (...) desk_ble: advertising as DeskGateway
   ```

2. iPhone 打开 LightBlue，扫描并连接 `DeskGateway`。
3. 展开标准 Device Information Service `180A`，读取 Firmware Revision String
   `2A26`；内容应与串口启动日志中的编译时间和 Git 派生 App Version 一致。
4. 展开 Service `7f4e0001-...`，对 State `7f4e0003-...` 执行 Read，再开启
   Notify；核对 8 字节高度、状态、童锁和上限。
5. 对 Command `7f4e0002-...` 选择 Hex、Write，写 `01`。首次写入应弹出配对；
   完成配对后再次写 `01`，桌子只上升约 `750ms` 并自动停止。
6. 连续每 `250–400ms` 写 `01`，桌子持续上升；停止写入后不迟于一个租约窗口停止。
7. 写 `02` 验证下降，写 `00` 验证立即停止。
8. 写 `11`、`14` 验证档位 1/4；运动途中主动 Disconnect，桌子必须立即停止。
9. Web 开启童锁后写 `01` / `02` / `11` / `14`，LightBlue 应显示 Write 失败且
   桌子不动；`00` 仍可停止。
10. Web 关闭“允许蓝牙操作”后重复第 9 步；重新开启后恢复。
11. 读取 Config `7f4e0004-...`，应得到 8 字节 v2 快照；写 `02 01 01 00` 开启童锁，
    再读应看到 bit0=1；写 `02 01 00 00` 关闭童锁。
12. 写 `02 05 FC 03` 将安全上限设为 1020 mm，Config Read 和 State Read 均应回读
    `FC 03`；无效范围必须 Write 失败。
13. 写 `02 06 8A 02` 将档位 1 设为 650 mm，写 `02 07 FC 03` 将档位 4 设为
    1020 mm；Web 与 App 均应自动显示 65 / 102 cm，重启固件后仍保持。违反
    `640 <= 档位1 < 档位4 <= 1290` 的写入必须失败；最高安全高度暂存值不再约束档位 4。
14. 写 Config 分别关闭/开启 REST、Bluetooth、Panel，核对 Web 状态与对应入口行为；
    关闭 Bluetooth 后 Config 管理写入仍可重新开启它。
15. 对 System `7f4e0005-...` 写 `01`，确认先停车再重启；重新连接已绑定手机后 Notify 恢复。

如果 LightBlue 显示旧的 GATT 表，先在 iOS 蓝牙设置中忽略 `DeskGateway`，关闭并
重新打开 LightBlue 后重新扫描；固件协议 UUID 变化时系统缓存不会总是自动刷新。

## 8. 交付阶段

| 阶段 | 内容 |
|---|---|
| 文档与代码（当前） | `connectivity/ble`：NimBLE GATT Server、desk_core 接入、租约、断连停止、状态 Notify |
| 真机门禁 | LightBlue 核心控制和 iPhone Config v2 已通过；按本文件第 7 节补齐断连、权限和异常停止证据 |
| 后续 | 开源「参考旋钮+OLED」固件或对接指南；可选 Central 模式适配成品外设 |

本能力不改变 Web/REST 协议。BLE 与 Wi-Fi 核心控制已通过真机验收；
断连、权限拒绝、前后台切换和异常停止矩阵仍需继续补齐。

## 9. 修订记录

| 版本 | 日期 | 说明 |
|---|---|---|
| 0.1 | 2026-08-06 | 初稿：Gateway 为 GATT Server；OLED/旋钮为典型外设；与 desk_core 对齐 |
| 1.0 | 2026-08-11 | 冻结 UUID 和字节协议；实现加密 Write、HOLD 租约、断连停止、档位 1/4 与 State Notify |
| 1.1 | 2026-08-11 | 增加标准 Device Information / Firmware Revision String，Command / State v1 不变 |
| 1.2 | 2026-08-11 | 增加 Config 单字段读写、设置 Notify 与独立 System 重启命令；Command / State v1 保持不变 |
| 1.3 | 2026-08-11 | Config 升级为 v2，新增设备持久化的档位 1/4 高度并在 Web、App 间同步；保留 v1 读写兼容 |
| 1.4 | 2026-08-11 | 记录 LightBlue 核心控制和 iPhone Config v2 真机通过；断连、权限与异常停止矩阵仍待补齐 |
| 1.5 | 2026-08-11 | Firmware Revision 改为构建时间与 Git 派生版本，移动端可确认烧录对应提交 |
