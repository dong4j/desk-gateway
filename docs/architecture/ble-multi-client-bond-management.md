# BLE 三客户端并发与配对设备管理方案

| 项 | 内容 |
|---|---|
| 文档编号 | DG-ARCH-BLE-MULTI-001 |
| 版本 | 0.1 |
| 日期 | 2026-08-12 |
| 状态 | 设计已确认；实现未开始 |
| 开发分支 | `codex/ble-multi-client-bond-management` |
| 关联协议 | [BLE 外设扩展 Profile v1](./ble-accessory-profile.md) |
| 客户端 | [Apple Watch](./apple-watch-control.md) / [移动端](./mobile-app-technology-selection.md) |

本文冻结 Desk Gateway 同时连接 iPhone、Apple Watch 和 Android 手机时的 BLE
连接模型、运动控制权、配对设备身份、删除 API、Web / 手机端入口以及验收门禁。

当前固件仍是单 BLE Central 实现。本文件描述的是后续实现目标，不能作为当前固件已经
支持三连接或配对管理的证据。

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

---

## 3. 配置基线

目标配置为：

```text
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=3
CONFIG_BT_NIMBLE_MAX_BONDS=3
```

两个值的含义不同：

| 配置 | 含义 | 设计理由 |
|---|---|---|
| `MAX_CONNECTIONS=3` | 最多三个同时在线的 BLE Central | 覆盖 iPhone、Apple Watch、Android 同时连接 |
| `MAX_BONDS=3` | 最多持久保存三个已配对身份 | 仅保留用户明确使用的三台设备，满额后先删除旧设备 |

Bond 数量不预留无界冗余。更换手机或出现旧 Bond 时，由本方案的单删 / 全删入口显式
释放名额，避免通过持续扩大 Bond 上限掩盖设备管理问题。

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
| `peer_identity` | Bond 使用的对端 Identity Address |
| `client_kind` | unknown / watchOS / iOS / Android |
| `state_subscribed` | 是否订阅 State Notify |
| `config_subscribed` | 是否订阅 Config Notify |
| `delete_pending` | 是否正在执行删除流程 |

连接建立、订阅变化、加密变化和断开事件都必须通过 `conn_handle` 定位槽位。禁止在一个
客户端断开时清空其他客户端的订阅或连接状态。

### 4.2 广播规则

NimBLE 建立连接后会结束当次可连接广播。固件按以下规则恢复广播：

1. stack 同步且当前连接数小于 3 时保持可连接广播；
2. 第一个或第二个客户端连接成功后立即重新开始广播；
3. 达到三个连接后不再启动可连接广播；
4. 任一客户端断开并释放槽位后重新广播；
5. stack reset 后安全停止 BLE 所有者的运动、重建连接表并重新广播。

### 4.3 Notify 分发

State / Config 仍使用原有 Characteristic 和字节布局。`ble_gatts_chr_updated()` 负责向
所有已启用对应 CCCD 的连接发送通知；连接表中的订阅字段用于判断是否需要触发更新、
生成诊断状态和验证断连清理，不能再用单个全局布尔值代表所有客户端。

---

## 5. 运动控制权

### 5.1 所有权状态

BLE 层新增可空的 `motion_owner_conn_handle`。它只区分 Bluetooth 来源内部的客户端；
Bluetooth 与 REST、原厂面板之间仍由 `desk_core` 的来源权限和全局安全规则仲裁。

| 事件 | 行为 |
|---|---|
| 无所有者时收到 HOLD / PRESET | 命令成功后将发送者设为所有者 |
| 所有者续发同方向 HOLD | 续期租约 |
| 所有者发送其他运动命令 | 按现有 `desk_core` 规则执行，并继续持有所有权 |
| 非所有者发送 HOLD / PRESET | 返回 Busy 对应的 ATT 错误，不改变当前运动 |
| 任意客户端发送 STOP | 无条件 STOP，并释放所有权 |
| 所有者 HOLD 租约超时 | STOP，并释放所有权 |
| 所有者完成 PRESET、进入 idle | 释放所有权 |
| 所有者断开或被删除 | STOP，然后释放所有权 |
| 非所有者断开或被删除 | 不影响当前运动 |
| 童锁开启或 Bluetooth 来源关闭 | STOP，并释放所有权 |
| NimBLE stack reset | STOP，并释放所有权 |

STOP 始终是全局安全动作，不受所有权限制。这样另一台设备即使不能接管运动，也仍然能
在异常情况下停止桌面。

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

REST 返回稳定但不承诺跨删除重建的 Bond ID。`id` 使用 Bond Identity Address 的摘要
生成 `bond_<12位十六进制>`，保证三个槽位内可唯一定位且不直接暴露完整地址。UI 只消费
`id`、自动生成的 `label` 和状态字段；`label` 的四位后缀取自该摘要。

```json
{
  "devices": [
    {
      "id": "bond_73c98f21a1b2",
      "kind": "watchos",
      "label": "Apple Watch · A1B2",
      "connected": true,
      "controlling": false,
      "deleting": false
    }
  ],
  "capacity": 3
}
```

### 7.2 REST API

| Method | Path | 行为 |
|---|---|---|
| `GET` | `/api/v1/bluetooth/bonds` | 返回所有 Bond、客户端类型、在线和控制状态 |
| `DELETE` | `/api/v1/bluetooth/bonds/{id}` | 安全断开并删除一个 Bond |
| `DELETE` | `/api/v1/bluetooth/bonds` | 安全断开并删除全部 Bond |

三个接口都必须通过现有 Web Session Token 或 `X-Desk-Key` 认证。不得通过未认证的 Setup
页面暴露，也不新增 BLE 删除命令。

### 7.3 单独删除

1. 校验认证信息和 Bond ID；
2. 将目标标记为 `deleting`，避免重复删除；
3. 如果目标是运动所有者，先执行 STOP 并释放所有权；
4. 如果目标在线，发起 GAP terminate；
5. 删除 NimBLE Bond 和对应 NVS 客户端类型元数据；
6. 清理连接槽并恢复广播；
7. 未找到 ID 返回 `404`，重复删除不得误删其他 Bond。

### 7.4 删除全部

1. 无条件执行 STOP 并释放 BLE 所有权；
2. 将所有在线连接标记为待删除并发起 GAP terminate；
3. 清空 NimBLE Bond Store；
4. 清空全部客户端类型元数据和连接槽；
5. 恢复广播；
6. 返回实际删除数量。

删除请求通过 HTTP 到达，但 GAP 断开是异步事件。固件应在 BLE 管理任务中串行执行删除，
HTTP Handler 不得阻塞等待蓝牙事件。API 可以先返回 `202 Accepted`，Web 和手机端刷新列表
直到目标消失；删除期间列表项显示 `deleting=true`。

---

## 8. Web UI

Web 控制页新增“蓝牙配对设备”区域：

- 显示 `已配对数量 / 3`；
- 每行显示自动名称、在线/离线和“控制中”状态；
- 每行提供“删除”按钮；
- 区域底部提供危险样式的“删除全部配对设备”；
- 单删和全删都必须二次确认；
- 删除期间禁用重复操作并轮询刷新；
- API 返回认证失败时沿用现有登录失效流程；
- 删除失败必须显示固件返回原因，不能乐观地从列表永久移除。

Web UI 仅消费 REST API，不读取或修改浏览器所在设备的本地蓝牙配对记录。

---

## 9. 手机端 UI

手机端在“设置 → 连接”区域增加“蓝牙配对设备”卡片，与 Web 使用相同 REST API：

- 显示三类设备和在线/控制状态；
- 支持单删和全部删除；
- 使用系统确认弹窗；
- 删除完成后刷新列表和当前连接状态；
- 如果删除的是本机 Bond，BLE 连接会断开，下次连接重新触发配对；
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
| 删除本机 Bond | 当前 BLE 会话断开；下次连接重新配对 |
| 回滚到单连接固件 | 已保存 Bond 可继续使用，但只能有一个 Central 在线 |

State、Config、Command 和 System 的现有 UUID 与字节布局保持不变。Client Info 是可选的
向后兼容扩展；不得为了三连接修改桌面运动命令字节。

---

## 11. 预计实现范围

### 11.1 固件

- `sdkconfig.defaults`：连接数调整为 3；
- BLE 组件：连接表、广播续开、订阅跟踪、所有权、Client Info、Bond 管理任务；
- Web 组件：Bond REST API、认证、异步删除状态；
- Web 静态资源：设备列表、确认和错误反馈；
- NVS：Bond Identity 到客户端类型的最小元数据映射；
- 测试：连接表、所有权、单删、全删和协议编码。

### 11.2 客户端

- Watch：连接后写入 watchOS Client Info，移除用 STOP 作为正常配对握手；
- iOS / Android：写入对应 Client Info；
- 手机 REST Client：查询、单删和全删 Bond；
- 手机设置页：配对设备列表和确认交互。

---

## 12. 验证门禁

### 12.1 自动化与静态门禁

- 固件构建通过；
- BLE 协议和连接表单元测试通过；
- 三连接所有权状态机测试通过；
- REST API 的认证、404、单删、全删和重复删除测试通过；
- Web JavaScript 语法和静态资源嵌入通过；
- 手机端 `npm run typecheck` 与 `npm test` 通过；
- Watch `swift test` 和通用 watchOS 无签名构建通过；
- `git diff --check` 通过。

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
11. 被删除设备重新连接时再次出现系统配对流程；
12. 重启网关后 Bond 列表、客户端类型和连接能力符合预期。

自动化和模拟器只能证明实现结构与编译结果，不能替代上述 BLE 射频、并发时序和真实
升降安全验收。完成三台真机门禁之前，结论必须保持 **代码 GO、产品验收 NO-GO**。

---

## 13. 实施顺序

1. 冻结本文档和 BLE Profile 扩展；
2. 抽取可测试的连接表与所有权状态机；
3. 实现三个连接、广播续开和多订阅 Notify；
4. 实现 Client Info 与客户端类型持久化；
5. 实现 Bond 查询、单删、全删和安全断开；
6. 接入 Web UI；
7. 更新 Watch、iOS、Android 客户端；
8. 接入手机设置页；
9. 完成自动化门禁；
10. 执行三台真机和真实升降桌验收。

后续实现不得把“连接数调整为 3”单独视为功能完成。只有连接表、所有权、删除语义、
两套管理 UI 和真机安全矩阵全部闭环，才能将本文状态改为“已实现”。
