# BLE 三客户端并发与配对设备管理方案

| 项 | 内容 |
|---|---|
| 文档编号 | DG-ARCH-BLE-MULTI-001 |
| 版本 | 1.0 |
| 日期 | 2026-08-13 |
| 状态 | 代码与自动化门禁已完成；三台真机安全矩阵待验收 |
| 开发分支 | `codex/ble-multi-client-bond-management` |
| 关联协议 | [BLE 外设扩展 Profile v1](./ble-accessory-profile.md) |
| 客户端 | [Apple Watch](./apple-watch-control.md) / [移动端](./mobile-app-technology-selection.md) |

本文冻结 Desk Gateway 同时连接 iPhone、Apple Watch 和 Android 手机时的 BLE
连接模型、运动控制权、配对设备身份、删除 API、Web / 手机端入口以及验收门禁。

固件、Web、React Native 手机端和 Watch 客户端已按本文完成实现。自动化与静态构建结果
见第 12.1 节；在第 12.2 节三台真机门禁完成前，结论保持**代码 GO、产品验收 NO-GO**。

---

## 1. 结论

Desk Gateway 将支持最多三个同时在线的 BLE Central：

```text
iPhone ───────┐
Apple Watch ──┼── BLE GATT ──> Desk Gateway ──> desk_core ──> 升降桌
Android ──────┘
```

三个客户端可以同时保持连接并接收高度、运动状态和设备配置 Notify，但任何时刻只允许
一个 BLE 客户端拥有运动控制权。连接能力和运动能力必须分离，不能因为客户端已连接就
允许它覆盖另一个客户端正在执行的运动。

配对设备的查看和删除统一通过已认证 REST API 提供。Web UI 和手机端设置页复用同一组
API；Watch 不提供删除入口，只负责登记自身客户端类型。

---

## 2. 范围与非目标

### 2.1 本阶段范围

- 同时保持三个 BLE Central 连接；
- 向所有已订阅客户端分发 State / Config Notify；
- 以 `conn_handle` 区分 BLE 运动所有者；
- 支持 iPhone、Apple Watch、Android 和未知客户端的类型登记；
- 查看已绑定设备及其在线、控制中状态；
- 通过已认证入口开启固定 120 秒的新设备配对窗口；
- 单独删除某个 Bond；
- 删除全部 Bond；
- 在 Web UI 和手机端设置页提供上述管理入口；
- 保留旧版 GATT 客户端的基础连接和控制兼容性。

### 2.2 非目标

- 不允许三个客户端同时控制运动；
- 不实现 BLE Mesh、广播控制或 Central 中继；
- 不通过 WatchConnectivity 转发实时运动命令；
- 不在 Watch 上提供 Bond 删除入口；
- 不允许未认证的局域网请求查看或删除配对设备；
- 不把设备完整 BLE Identity Address 直接展示在 UI 中；
- 不在本阶段提供自定义设备重命名。
- 不把客户端自报的 `client_kind` 用于身份认证、授权或运动优先级判断。

---

## 3. 配置基线

当前配置为：

```text
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=3
CONFIG_BT_NIMBLE_MAX_BONDS=3
```

两个值的含义不同：

| 配置 | 含义 | 实现理由 |
|---|---|---|
| `MAX_CONNECTIONS=3` | 最多三个同时在线的 BLE Central | 覆盖 iPhone、Apple Watch、Android 同时连接 |
| `MAX_BONDS=3` | 最多持久保存三个已配对身份 | 仅保留用户明确使用的三台设备，满额后先删除旧设备 |

Bond 数量不预留无界冗余。更换手机或出现旧 Bond 时，由本方案的单删 / 全删入口显式
释放名额，避免通过持续扩大 Bond 上限掩盖设备管理问题。

早期固件使用的 `ble_store_util_status_rr` 会在 Bond Store 溢出时删除最旧 Bond，只适合
示例程序。当前实现已替换为产品侧 Store Status Callback：容量不足时拒绝新 Bond，禁止
自动调用 `ble_gap_unpair_oldest_peer()` 或任何等价淘汰逻辑。`MAX_BONDS=3` 表示硬上限，
不表示循环覆盖槽位。

ESP32-S3 上三个连接与 Wi-Fi 共存仍需要真机验证，包括堆内存余量、Notify 时延、HOLD
续租和断连 STOP；仅通过编译不能证明三连接运行稳定。

---

## 4. 固件连接模型

### 4.1 固定容量连接表

固件使用容量为 3 的连接表替代当前全局 `s_connected`、`s_state_subscribed` 和
`s_config_subscribed` 单值状态。每个槽位至少记录：

| 字段 | 说明 |
|---|---|
| `in_use` | 槽位是否有效 |
| `conn_handle` | NimBLE 当前连接句柄 |
| `generation` | 槽位复用代次，防止迟到的异步事件命中新连接 |
| `peer_identity_valid` | 是否已经解析到稳定的 Bond Identity |
| `peer_identity` | 已解析时保存对端 Identity Address；未解析时不得用于查询或删除 |
| `encrypted` | 当前连接是否已完成加密 |
| `client_kind` | unknown / watchOS / iOS / Android |
| `state_subscribed` | 是否订阅 State Notify |
| `config_subscribed` | 是否订阅 Config Notify |
| `delete_state` | idle / pending / failed |
| `delete_error` | 最近一次异步删除失败原因；成功或重试时清空 |

连接建立、订阅变化、加密变化和断开事件都必须通过 `conn_handle` 定位槽位。禁止在一个
客户端断开时清空其他客户端的订阅或连接状态。

### 4.2 状态所有权与执行上下文

连接表、运动所有者、配对窗口和删除状态以 NimBLE Host 上下文为唯一写入者。HTTP Handler
不得直接调用 GAP、Bond Store 或连接表写操作，只能向有界命令队列提交请求，再由
NimBLE Host Event Queue 串行执行。Notify 任务和 REST 查询只能读取受锁保护的只读快照。

槽位只能由对应 `conn_handle + generation` 的事件修改。在线设备收到删除请求后先标记
`pending` 并发起 terminate，在匹配的 `BLE_GAP_EVENT_DISCONNECT` 到达前不得清空或复用
该槽位。这样可以避免旧断开事件、删除超时或句柄复用误伤新连接。

### 4.3 广播规则

NimBLE 建立连接后会结束当次可连接广播。固件按以下规则恢复广播：

1. stack 同步且当前连接数小于 3 时保持可连接广播；
2. 第一个或第二个客户端连接成功后立即重新开始广播；
3. 达到三个连接后不再启动可连接广播；
4. 任一客户端断开并释放槽位后重新广播；
5. stack reset 后安全停止 BLE 所有者的运动、重建连接表并重新广播。

广播持续存在不等于允许任何新身份持久配对。已保存 Bond 可以在配对窗口关闭时正常重连；
尚未绑定的连接必须经过第 7.3 节的配对准入检查。

### 4.4 Notify 分发

State / Config 仍使用原有 Characteristic 和字节布局。`ble_gatts_chr_updated()` 负责向
所有已启用对应 CCCD 的连接发送通知；连接表中的订阅字段用于判断是否需要触发更新、
生成诊断状态和验证断连清理，不能再用单个全局布尔值代表所有客户端。

---

## 5. 运动控制权

### 5.1 所有权状态

BLE 层新增可空的 `motion_owner_conn_handle`，并同时记录所有者槽位的 `generation`。它只
区分 Bluetooth 来源内部的客户端；Bluetooth 与 REST、原厂面板之间不新增互斥锁，仍按
`desk_core` 的童锁、来源开关和运动安全规则执行。

如果 REST 或原厂面板的非 STOP 运动命令被 `desk_core` 接受，BLE 所有权必须在不额外
发送 STOP 的前提下立即释放，避免原 BLE 客户端随后断开时停止其他来源的新运动。为此
`desk_core` 必须向 BLE 管理层提供“已接受运动来源变化”事件或等价的活动来源快照，不能
只依赖 200 ms 状态轮询猜测来源。

| 事件 | 行为 |
|---|---|
| 无所有者时收到 HOLD / PRESET | 命令成功后将发送者设为所有者 |
| 所有者续发同方向 HOLD | 续期租约 |
| 所有者发送其他运动命令 | 按现有 `desk_core` 规则执行，并继续持有所有权 |
| 非所有者发送 HOLD / PRESET | 返回 ATT Application Error `0x80`（Desk Busy），不改变当前运动 |
| 任意客户端发送 STOP | 无条件 STOP，并释放所有权 |
| 所有者 HOLD 租约超时 | STOP，并释放所有权 |
| 所有者完成 PRESET、进入 idle | 释放所有权 |
| 所有者断开或被删除 | STOP，然后释放所有权 |
| 非所有者断开或被删除 | 不影响当前运动 |
| 童锁开启或 Bluetooth 来源关闭 | STOP，并释放所有权 |
| NimBLE stack reset | STOP，并释放所有权 |

STOP 始终是全局安全动作，不受所有权限制。这样另一台设备即使不能接管运动，也仍然能
在异常情况下停止桌面。

`0x80` 是 Desk Accessory Profile 的稳定应用错误。Watch、iOS 和 Android 新客户端必须
将它显示为“另一台设备正在控制”，不能把它当作断连；旧客户端即使只能显示通用写入
失败，也不得重试为 STOP 或覆盖当前所有者。

### 5.2 连接握手不再依赖 STOP

现有客户端可能通过首次加密 STOP 验证配对和写入能力。三连接模式下，第二个客户端上线
时发送 STOP 会意外中断第一个客户端的正常运动。

新版 Watch 和手机客户端改为写入加密的 Client Info Characteristic 完成配对验证，不能
再把 STOP 当作常规连接握手。旧客户端仍可连接；如果旧客户端主动发送 STOP，固件继续
按安全语义执行停止。

---

## 6. Client Info 扩展

在 Desk Accessory Service 下新增向后兼容的 Characteristic：

| Attribute | UUID | Properties | 说明 |
|---|---|---|---|
| Client Info | `7f4e0006-6d4c-4f4b-9f7a-3c1d2e5a9b10` | Write, Write Encrypted | 登记客户端协议版本和类型，同时触发安全配对 |

固定 2 字节，小端序无关：

| Offset | 字段 | 值 |
|---|---|---|
| 0 | Client Info 版本 | `0x01` |
| 1 | 客户端类型 | `0x00` unknown；`0x01` watchOS；`0x02` iOS；`0x03` Android |

固件通过 GATT 回调的 `conn_handle` 查询已解析的对端 Bond Identity，将客户端类型保存到
NVS 元数据。客户端不上传设备名称、完整地址或其他个人信息。

`client_kind` 是客户端自报的展示字段，固件只校验版本和枚举范围。该字段不得用于认证、
运动仲裁、删除权限或配对窗口绕过；未知值按 `unknown` 处理。

界面显示名称由固件返回的类型和 Bond ID 后四位生成，例如：

```text
Apple Watch · A1B2
iPhone · C3D4
Android · E5F6
未知设备 · 91A0
```

已有 Bond 在首次升级后显示为“未知设备”；对应客户端重新连接并写入 Client Info 后更新
类型。设备类型元数据随单个 Bond 或全部 Bond 一起删除。

---

## 7. Bond 管理模型

### 7.1 API 数据结构

REST 返回稳定但不承诺跨删除重建的 Bond ID。首次发现 Bond 时由固件生成 48-bit 随机
opaque ID，编码为 `bond_<12位十六进制>`，与 Identity Address 一起保存在内部 NVS
元数据中。生成时必须检查当前最多三个条目并在碰撞时重新生成，不能使用无密钥地址摘要、
地址后缀或可逆编码。UI 只消费 `id`、自动生成的 `label` 和状态字段；`label` 的四位后缀
取自 opaque ID。

```json
{
  "devices": [
    {
      "id": "bond_73c98f21a1b2",
      "kind": "watchos",
      "label": "Apple Watch · A1B2",
      "connected": true,
      "controlling": false,
      "delete_state": "idle",
      "delete_error": null
    }
  ],
  "capacity": 3,
  "pairing_window": {
    "open": false,
    "remaining_seconds": 0
  }
}
```

### 7.2 REST API

| Method | Path | 行为 |
|---|---|---|
| `GET` | `/api/v1/bluetooth/bonds` | 返回所有 Bond、客户端类型、在线和控制状态 |
| `POST` | `/api/v1/bluetooth/pairing-window` | 开启或续期固定 120 秒的新设备配对窗口 |
| `DELETE` | `/api/v1/bluetooth/pairing-window` | 提前关闭新设备配对窗口 |
| `DELETE` | `/api/v1/bluetooth/bonds/{id}` | 安全断开并删除一个 Bond |
| `DELETE` | `/api/v1/bluetooth/bonds` | 安全断开并删除全部 Bond |

全部接口都必须通过现有 Bearer Token 或 `X-Desk-Key` 认证。不得通过未认证的 Setup
页面暴露，也不新增 BLE 删除命令。

固定响应语义如下：

| 状态 | 含义 |
|---|---|
| `200 OK` | 查询成功、配对窗口已打开/关闭，或离线 Bond 已同步删除 |
| `202 Accepted` | 在线 Bond 的异步删除已受理；重复提交同一 `pending` 目标仍返回 `202` |
| `401 Unauthorized` | 认证信息缺失或失效 |
| `404 Not Found` | Bond ID 不存在，或已在先前请求中删除完成 |
| `409 Conflict` | 全删请求与正在执行或待重试的单删状态冲突，或内部状态不允许当前操作 |
| `500 Internal Server Error` | 请求未能入队或内部状态无法安全推进 |

### 7.3 新设备配对准入

- 配对窗口重启后默认关闭，打开后固定 120 秒自动关闭，不允许客户端自定义无限时长；
- 已保存且能用原密钥恢复加密的 Bond 不受窗口影响；
- 新身份只有在窗口打开且 Bond 数量小于 3 时才能完成持久配对；
- 窗口关闭或容量已满时，未绑定连接可以完成物理连接，但不得进入任何加密 Command /
  Config / System 业务回调，必须被拒绝并安全断开；
- 第三个新 Bond 成功保存后立即关闭窗口；
- 客户端已“忽略设备”后触发的 Repeat Pairing 也需要配对窗口，防止伪造旧 Identity 的
  附近设备删除已有密钥；
- Store 溢出只能让本次配对失败，绝不能静默删除最旧 Bond。

本设备使用 Just Works，没有 MITM 保护。配对窗口只缩短未经授权设备占用 Bond 名额的
暴露时间，不能把 Just Works 提升为可验证的设备身份认证。

### 7.4 单独删除

1. 校验认证信息和 Bond ID；
2. 在 Host 上下文将目标标记为 `pending`，清空旧的 `delete_error`；
3. 如果目标是运动所有者，先执行 STOP 并释放所有权；
4. 目标离线时，同步删除 NimBLE Bond 和全部对应元数据并返回 `200`；
5. 目标在线时发起 GAP terminate 并返回 `202`，不得阻塞 HTTP Handler；
6. 收到匹配 `conn_handle + generation` 的断开事件后，再删除 Bond、元数据并释放槽位；
7. terminate、断开等待或 Store 删除失败时保留条目，设置 `delete_state=failed` 和可展示的
   `delete_error`；用户再次确认后允许显式重试；
8. 未找到 ID 返回 `404`，`pending` 期间的重复请求保持幂等，不得误删其他 Bond。

### 7.5 删除全部

1. 无条件执行 STOP 并释放 BLE 所有权；
2. 在 Host 上下文将所有目标标记为 `pending`，关闭配对窗口；
3. 离线 Bond 立即删除；在线 Bond 分别发起 GAP terminate；
4. 每个在线目标只在匹配的断开事件到达后删除 Bond、元数据并释放槽位；
5. 全部目标完成后恢复广播，并在最终列表中反映实际删除结果；
6. 任一目标失败时保留其 `failed` 状态和原因，不能把“已入队数量”当作“实际删除数量”。

删除请求通过 HTTP 到达，但 GAP 断开是异步事件。固件应通过 Host 命令队列串行执行删除，
HTTP Handler 不得阻塞等待蓝牙事件。API 对在线目标先返回 `202 Accepted`，Web 和手机端
刷新列表直到目标消失或进入 `failed`；只有这样才能向用户展示异步失败的固件原因。

启动时还必须以 NimBLE Bond Store 为事实源对账 NVS 元数据：为缺少元数据的旧 Bond
生成 opaque ID 和 `unknown` 类型，清理已不存在 Bond 的孤儿元数据。Repeat Pairing 删除
旧密钥时必须走同一套对账逻辑，不能遗留重复 ID 或幽灵设备。

---

## 8. Web UI

Web 控制页新增“蓝牙配对设备”区域：

- 显示 `已配对数量 / 3`；
- 提供“允许新设备配对”按钮并显示 120 秒倒计时；
- 每行显示自动名称、在线/离线和“控制中”状态；
- 每行提供“删除”按钮；
- 区域底部提供危险样式的“删除全部配对设备”；
- 单删和全删都必须二次确认；
- `pending` 期间禁用重复操作并轮询刷新，`failed` 时显示原因和“重试”；
- API 返回认证失败时沿用现有登录失效流程；
- 删除失败必须显示固件返回原因，不能乐观地从列表永久移除。

Web UI 仅消费 REST API，不读取或修改浏览器所在设备的本地蓝牙配对记录。

---

## 9. 手机端 UI

手机端在“设置 → 连接”区域增加“蓝牙配对设备”卡片，与 Web 使用相同 REST API：

- 显示三类设备和在线/控制状态；
- 支持开启或提前关闭 120 秒配对窗口；
- 支持单删和全部删除；
- 使用系统确认弹窗；
- 删除完成后刷新列表和当前连接状态；
- 如果删除的是本机 Bond，BLE 连接会断开；重新连接前需要再次开启配对窗口；
- 如果未配置网关地址或 REST 密码，入口保持禁用并提示“需先配置局域网管理”；
- 当前控制链路是 BLE 时，管理请求仍单独走 Wi-Fi / REST，不通过 BLE 转发。

iOS 和 Android 共用 React Native UI 与 REST Client；客户端类型登记由 BLE Client 根据
`Platform.OS` 写入不同枚举值。

---

## 10. 兼容与迁移

| 场景 | 行为 |
|---|---|
| 旧手机 / BLE 配件忽略 Client Info | 仍可使用原 Command / State / Config；列表显示未知设备 |
| 新客户端连接旧固件 | 找不到 Client Info 时退回原连接流程，但不得假定多连接可用 |
| 旧 Bond 升级固件 | 保留 Bond；重新连接后补齐客户端类型 |
| Bond 已满 | 新身份配对失败；用户通过 Web / 手机 REST 先删除旧设备 |
| 删除本机 Bond | 当前 BLE 会话断开；开启配对窗口后尝试重新配对 |
| 手机仍保留本地 Bond | 网关无法远程删除系统蓝牙记录；自动重配失败时提示用户在系统设置中忽略/取消配对 |
| 客户端忘记、网关仍保留 Bond | 通过已认证入口打开配对窗口后才允许 Repeat Pairing |
| 回滚到单连接固件 | 已保存 Bond 可继续使用，但只能有一个 Central 在线 |

State、Config、Command 和 System 的现有 UUID 与字节布局保持不变。Client Info 是可选的
向后兼容扩展；不得为了三连接修改桌面运动命令字节。

---

## 11. 已实现范围

### 11.1 固件

- `sdkconfig.defaults`：连接数调整为 3；
- BLE 组件：连接表、Host 命令队列、广播续开、订阅跟踪、所有权、配对窗口、Client Info、
  无淘汰 Store Callback 和 Bond 管理状态机；
- `desk_core`：向 BLE 管理层暴露已接受运动来源变化，避免 BLE 断连误停其他来源；
- Web 组件：Bond / 配对窗口 REST API、认证、完整 HTTP 状态和异步删除状态；
- Web 静态资源：设备列表、确认和错误反馈；
- NVS：Bond Identity 到 opaque ID 和客户端类型的最小元数据映射；
- 测试：连接表代次、所有权、配对准入、Store 满额、单删、全删、异步失败和协议编码。

### 11.2 客户端

- Watch：连接后写入 watchOS Client Info，移除用 STOP 作为正常配对握手；
- iOS / Android：写入对应 Client Info；
- 手机 REST Client：查询、单删和全删 Bond；
- 手机设置页：配对窗口、设备列表、失败重试和确认交互。

---

## 12. 验证门禁

### 12.1 自动化与静态门禁

- 固件构建通过；
- BLE 协议和连接表单元测试通过；
- 三连接所有权状态机测试通过；
- Store 满额时新 Bond 失败且三个旧 Bond 均保留；
- 配对窗口关闭、超时、容量已满和第三个 Bond 成功后的准入测试通过；
- REST API 的认证、200/202/404/409/500、单删、全删、重复删除和异步失败测试通过；
- 迟到断开事件和 `conn_handle` 复用不会清理新槽位；
- Repeat Pairing 和启动对账不会留下孤儿元数据；
- Watch / 手机端能识别 Desk Busy `0x80`，且不会将其当作断连；
- Web JavaScript 语法和静态资源嵌入通过；
- 手机端 `npm run typecheck` 与 `npm test` 通过；
- Watch `swift test` 和通用 watchOS 无签名构建通过；
- `git diff --check` 通过。

截至 2026-08-13，上述自动化与静态门禁均已通过：固件隔离构建使用 ESP-IDF v6.0.2；
手机端 `npm run typecheck` 与 36 项测试通过；Watch `swift test` 14 项测试和通用 watchOS
无签名构建通过。详细命令与最终结果记录在仓库根目录完成结果报告中。

### 12.2 三台真机门禁

测试设备固定为 iPhone、Apple Watch 和 Android 手机：

1. 三台设备依次配对并同时保持连接；
2. 三台都持续收到相同高度和运动状态；
3. 第一个客户端控制运动后，另两台运动命令被拒绝；
4. 任意非所有者 STOP 可以立即停止；
5. 非所有者断开不停止当前运动；
6. 所有者断开、杀进程或失联会在安全时限内停止；
7. 三台在线时 HOLD 续租不因 Wi-Fi 共存超时；
8. 单独删除在线所有者会先 STOP，再断开并清除 Bond；
9. 单独删除离线设备不会影响其他连接；
10. 删除全部会停止运动、断开三台设备并恢复可配对广播；
11. 配对窗口关闭时第四台设备不能占用 Bond，三个旧 Bond 不被淘汰；
12. 被删除设备在重新打开配对窗口后再次进入系统配对流程；
13. iOS / Android 保留本地 Bond 导致自动重配失败时，客户端能给出系统设置操作提示；
14. REST 或原厂面板接管运动后，旧 BLE 所有者断开不会停止新的运动来源；
15. 重启网关后配对窗口关闭，Bond 列表、客户端类型和连接能力符合预期。

自动化和模拟器只能证明实现结构与编译结果，不能替代上述 BLE 射频、并发时序和真实
升降安全验收。完成三台真机门禁之前，结论必须保持 **代码 GO、产品验收 NO-GO**。

---

## 13. 实施结果 checklist

- [x] 冻结 Busy `0x80`、Client Info 和三连接设计；
- [x] 实现连接表、槽位代次、多订阅 Notify 和 BLE 运动所有权；
- [x] 替换 Round-Robin Store Callback，实现容量拒绝、配对窗口和启动对账；
- [x] 实现 Client Info、opaque Bond ID 和客户端类型持久化；
- [x] 实现 Host 命令队列、单删、全删、失败状态和安全断开；
- [x] 实现已认证 REST API 与 Web 管理 UI；
- [x] 更新 Watch、iOS 和 Android 客户端握手与 Busy 语义；
- [x] 实现手机端配对窗口、设备列表、失败重试和系统确认；
- [x] 完成固件、Web、手机和 Watch 自动化与静态构建门禁；
- [ ] 完成 iPhone、Apple Watch、Android 与真实升降桌三台真机安全矩阵。

## 14. 原实施顺序

1. 冻结本文档、Busy `0x80` 和 BLE Profile 扩展；
2. 抽取可测试的连接表、槽位代次、所有权和 Host 命令队列；
3. 替换 Round-Robin Store Callback，实现容量拒绝、配对窗口和启动对账；
4. 实现三个连接、广播续开和多订阅 Notify；
5. 实现 Client Info、opaque Bond ID 和客户端类型持久化；
6. 实现 Bond 查询、单删、全删、失败状态和安全断开；
7. 接入 Web UI；
8. 更新 Watch、iOS、Android 客户端；
9. 接入手机设置页和系统 Bond 失败提示；
10. 同步 BLE Profile、Watch 和手机端关联文档；
11. 完成自动化门禁；
12. 执行三台真机和真实升降桌验收。

本次实现没有把“连接数调整为 3”单独视为功能完成；连接表、所有权、删除语义和两套管理
UI 已闭环。三台真机安全矩阵仍是产品验收的最后门禁，完成前不得宣称硬件并发能力已验收。
