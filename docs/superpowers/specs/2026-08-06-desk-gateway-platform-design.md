# Desk Gateway 平台架构设计

| 项 | 内容 |
|---|---|
| 文档 | DG-ARCH-DESIGN-001 |
| 日期 | 2026-08-06 |
| 状态 | **已批准**（v0.4，2026-08-06 用户确认「文档通过」） |
| 对应决策 | 方案 B + 板载 Web（局域网 + 简单认证 + 现代化 UI / 升降动效） |
| 前置 | [需求文档](../../0-requirements.md)、[协议笔记](../../3-protocol-reverse-notes.md) |

本文最初是实现前的**架构定稿**。截至 2026-08-09，M1/M2 代码已落地并通过编译，
但真机门禁仍未完成；当前状态以[架构总览](../../architecture/overview.md)和[验收清单](../../bringup-checklist.md)为准。

---

## 1. 目标与非目标

### 1.1 目标

把 Desk Gateway 从「单桌 DIY 固件」演进为**可持续演进的智能平台**：

1. **Desk Driver 可插拔**：适配不同厂商只新增驱动，不改 Web / WiFi / 控制面。
2. **控制面统一**：串口、Web、**BLE 外设总线**（及未来 Matter）全部经 `desk_core`，共享安全策略。
3. **Phase 1 即可用 Web**：局域网访问，简单密码认证，现代化控制页，带升降桌示意图与实时动效。
4. **连通层可扩展**：WiFi / Web 本阶段实现；**BLE 定位为配件总线**（OLED+旋钮等，见 ble-accessory 文档），本阶段可 stub；Matter / HA / 米家/华为 / OTA 只留接口边界。
5. **童锁**：开启后所有来源都不能启动或维持运动；仅 `STOP` 和解除童锁始终有效。
6. **外设可扩展**：向第三方 BLE 配件提供标准化高度/状态/控制数据面，盒子本身不必焊接旋钮屏。
### 1.2 非目标（本设计周期不做）

- 双 RJ45 主动中间人硬件闭环（仍属 Phase 2；童锁对面板的**真正屏蔽**依赖 Phase 2 透传路径）
- Loctek / Jiecang / Upsy 真协议实现（仅目录与 stub）
- Matter / MQTT / Home Assistant / Siri / OTA 实现  
- **米家 / 华为智慧生活「原生上架」**（需认证模组与合作流程；调研见 [ecosystem-xiaomi-huawei.md](../../architecture/ecosystem-xiaomi-huawei.md)）  
- 公网穿透、云账号、OAuth  
- 量产认证（FCC/CE）  
- **原厂「上+下同时按住 ≈5s → 重置」的 DR 键码**（用户已确认面板有此操作；**尚未抓包**，本设计只登记待逆向，禁止臆造码）

### 1.3 与 Upsy Desky 的关系

[Upsy Desky](https://github.com/tjhorner/upsy-desky) = 开源硬件 + **ESPHome** + 偏 UART 控制盒兼容列表。

本项目差异化：

| 维度 | Upsy Desky | Desk Gateway |
|---|---|---|
| 固件 | ESPHome | ESP-IDF 自研平台 |
| 协议 | 以 UART 方言为主 | **多 Driver**（含 I²C 面板模拟） |
| 智能层 | 强绑 HA | Web 优先，HA/Matter 后接同一 `desk_core` |

可借鉴：中间人产品形态、安全默认、兼容性文档写法。不照搬单协议 ESPHome 模型。

---

## 2. 逻辑架构

```text
┌─────────────────────────────────────────────────────────────┐
│                     Desk Gateway 平台                         │
│                                                             │
│   ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────────────────┐ │
│   │ Web UI   │  │REST/轮询 │  │ UART CLI │  │ BLE Accessory API  │ │
│   │ (静态页) │  │ (+认证)  │  │          │  │ 旋钮/OLED 等外设   │ │
│   └────┬─────┘  └────┬─────┘  └────┬─────┘  └─────────┬──────────┘ │
│        └─────────────┴───────┬─────┴──────────────────┘            │
│                              ▼                                     │
│                    ┌─────────────────┐                             │
│                    │    desk_core    │  命令仲裁 / 超时 / 急停 / 童锁 │
│                    └────────┬────────┘                             │
│                             ▼                               │
│                    ┌─────────────────┐                      │
│                    │ desk_driver API │  厂商无关契约          │
│                    └────────┬────────┘                      │
│           ┌─────────────────┼─────────────────┐             │
│           ▼                 ▼                 ▼             │
│    mxtark          loctek*           jiecang* …        │
│    (I²C Slave)          (stub)            (stub)            │
└─────────────────────────────────────────────────────────────┘

* stub：仅占位，返回 NOT_SUPPORTED
```

**硬规则**：任何控桌入口不得直接调用某厂商协议细节；只调 `desk_core`。

---

## 3. 仓库与固件目录

采用 **单 ESP-IDF 工程 + components**（已确认方案 1）。

```text
desk-gateway/
  docs/
    architecture/
      overview.md                 # 给人读的架构总览（本设计的精简版）
    superpowers/specs/
      2026-08-06-desk-gateway-platform-design.md  # 本文
    0-requirements.md             # 需求（与本设计同步修订）
    …
  firmware/
    desk-gateway/                 # 唯一主固件（由 phase1-panel-slave 演进/迁入）
      main/
      components/
        desk_core/                # 统一命令与安全
        desk_driver/              # 接口定义 + 驱动注册
        drivers/
          mxtark/            # 现有 TM1650 键通道模拟
          loctek/                 # stub
          jiecang/                # stub
        connectivity/
          wifi/                   # STA（+ 可选 SoftAP 配网，可二期）
          web/                    # HTTP + 静态 UI + 认证
          ble/                    # Accessory GATT（旋钮/OLED）；见 ble-accessory-profile
          matter/                 # Phase3+ stub；见生态调研
      spiffs_or_embed/            # Web 静态资源（见 §6）
  hardware/                       # 预留原理图/外壳
  captures/                       # 抓包资产（.sr 等）；分析仪见 nanoDLA 上游
```

抓包硬件推荐开源逻辑分析仪 **[nanoDLA](https://github.com/wuxx/nanoDLA)**（本仓库不 Vendoring 其固件/文档）。

**驱动选择（本阶段）**：Kconfig 编译期选择默认 Driver（默认 `mxtark`）。运行时多驱动切换列为 Backlog。

**迁移策略**：将现有 `firmware/phase1-panel-slave/` 逻辑拆入 `drivers/mxtark` + `desk_core` + `connectivity`；旧目录可保留短暂兼容说明后删除或改成指向新工程的 README 跳转，避免双源。

---

## 4. `desk_driver` 契约

### 4.1 状态枚举

| 值 | 含义 |
|---|---|
| `IDLE` | 静止 / 无按住运动 |
| `MOVING_UP` | 正在升高（按住升或等价） |
| `MOVING_DOWN` | 正在降低 |
| `GOTO_PRESET` | 正在前往档位（若驱动可区分） |
| `ERROR` | 链路/驱动错误 |
| `UNSUPPORTED` | 能力不存在（查询类） |

### 4.2 操作表

| 操作 | 语义 | 必须？ |
|---|---|---|
| `init` / `deinit` | 装载硬件资源 | 是 |
| `stop` | 立即停止；映射为该厂商的 idle/松开 | **是（最高优先）** |
| `hold_up` / `hold_down` | 按住升降；松开靠 `stop` | 是 |
| `goto_preset(n)` | 短码前往；未知 n → `NOT_SUPPORTED` | 能则实现 |
| `save_preset(n)` | 长码保存；同上 | 能则实现 |
| `get_height_mm` | 当前高度；无则 `UNSUPPORTED` | Phase1 可选 |
| `get_status` | 上表状态 | 是 |
| `get_caps` | 位标志：升降/档位/高度/… | 是 |

**禁止**：驱动内伪造未验证的厂商键码（如 mxtark 的 Preset2/3）。

### 4.3 `mxtark` 映射（已逆向）

| core 操作 | DR / 行为 |
|---|---|
| stop / idle | `0x2E` |
| hold_up | `0x47` |
| hold_down | `0x4F` |
| goto_preset(1/4) | `0x17` / `0x2F` |
| save_preset(1/4) | `0x57` / `0x6F`（≥4s） |
| 高度 | Phase1 可不实现；后续旁路听 digit 写入 |
| **factory/reset（上+下≈5s）** | **未知** — 见 §5.2；未验证前 `NOT_SUPPORTED` |

安全：升/降最长按住超时（默认 15s）在 **`desk_core`** 统一执行，不分散到各入口。

---

## 5. `desk_core` 职责

1. 持有当前选中的 `desk_driver` 实例。  
2. 暴露线程安全 API：`desk_core_up/down/stop/goto/save/set_child_lock/get_status/…`。  
3. 运动超时、异常默认 `stop`。  
4. 向 UI 层提供状态快照（当前供短轮询）：至少含 `status`、`height_mm`（可空）、`caps`、`child_lock`、`uptime`。  
5. 童锁状态机 + Phase 2 面板仲裁（见下）。

串口 CLI 与 Web 必须调用同一套 core API，保证任一侧 `stop` 立即生效。

### 5.1 童锁（Child Lock）

| 项 | 约定 |
|---|---|
| 语义 | 开启后除 `STOP` 和解除童锁外，Web / UART / BLE / 原厂面板及后续来源均不能启动或维持运动 |
| 状态 | `child_lock: bool`，默认 **false**；持久化 NVS |
| API | `POST /api/v1/desk/child-lock` `{ "enabled": true\|false }`；`status` 带 `child_lock` |
| UI | 控制台显式开关 + 锁定态提示；另提供 REST / Bluetooth / Panel 来源权限 |
| Phase 1 | REST / UART 已在 `desk_core` 强制拦截，状态、API、UI 与 NVS 已实现 |
| Phase 2 | MITM 丢弃面板运动意图；解锁或重新允许 Panel 后，先等物理按键松开再恢复 |

**仲裁优先级（Phase 2，从高到低）**：

1. 安全急停（`stop` 始终放行）  
2. **童锁 ON** → 拒绝所有来源的运动意图  
3. 童锁 OFF → 检查来源权限  
4. Panel 允许时，原厂面板优先于其他入口  
5. 其他已允许来源  

童锁不能关闭 Web 急停；改童锁必须已登录。

### 5.2 原厂面板：上+下同时约 5s → 重置（待逆向）

用户面板实测（**业务事实，协议未解**）：

> 同时按住 **上升 + 下降** 约 **5 秒**，触发原厂 **重置** 类操作（恢复出厂 / 清行程 / 清档位等，以抓包与表现为准）。

| 项 | 状态 |
|---|---|
| 文档登记 | 协议笔记 + 需求 Backlog **必须有** |
| `DR` / 时序 | **未知** — 禁止固件臆造 |
| 抓包任务 | `reset_up_down_hold_5s.sr`（见协议笔记） |
| Driver | 码未验证前 `reset` → `NOT_SUPPORTED`；Web 不展示或灰显「重置」并注明待协议 |
| 与童锁 | 属面板操作；童锁 ON 时 Phase 2 一并屏蔽 |

---

## 6. 连通层与 Web

### 6.1 WiFi

- 模式：STA 连家庭局域网（凭证存 NVS）。  
- 配网：本阶段可用串口命令写 SSID/密码；SoftAP 配网页可作为紧随其后的小迭代，不阻塞首版 Web。  
- **仅局域网使用**；文档与 UI 明示「不要端口映射到公网」。

### 6.2 认证（简单、必做）

| 项 | 约定 |
|---|---|
| 凭据 | 单密码，存 NVS；出厂/首次默认密码写入文档（如 `desk-gateway`），**首次登录后强制修改**（推荐）或至少 UI 强烈提示修改 |
| 登录 | `POST /api/v1/auth/login` `{ "password": "..." }` → `{ "token": "..." }` |
| 鉴权 | 除登录页与静态登录资源外，API 需 `Authorization: Bearer <token>` |
| Token | 随机不透明串，存 RAM（重启失效）；可选 TTL（如 24h） |
| 改密 | `POST /api/v1/auth/password`（需已登录） |
| 传输 | 首版 HTTP（局域网）；HTTPS/mTLS 列为 Backlog |

串口 CLI **不走** Web 密码（物理接触假设）；但串口可 `set_wifi` / `set_web_password`。

### 6.3 HTTP API

| 方法 | 路径 | 说明 |
|---|---|---|
| POST | `/api/v1/auth/login` | 登录 |
| POST | `/api/v1/auth/password` | 改密 |
| POST | `/api/v1/desk/up` | 开始升高（按住） |
| POST | `/api/v1/desk/down` | 开始降低 |
| POST | `/api/v1/desk/stop` | 停止（P0） |
| POST | `/api/v1/desk/preset/{n}/goto` | 前往档位 |
| POST | `/api/v1/desk/preset/{n}/save` | 保存档位 |
| POST | `/api/v1/desk/child-lock` | 开关童锁 `{ "enabled": bool }` |
| GET | `/api/v1/desk/status` | 状态快照 JSON |

状态示例：

```json
{
  "status": "moving_up",
  "height_mm": null,
  "height_known": false,
  "child_lock": false,
  "driver": "mxtark",
  "ts_ms": 123456
}
```

客户端每 250ms 请求一次 `GET /status` 驱动示意图。当前不提供同步 SSE 长连接，
避免 ESP-IDF HTTP server handler 长时间占用 server task、影响急停等控制请求；未来如恢复 SSE，必须使用异步请求生命周期并验证控制并发。

### 6.4 Web UI / UX

**定位**：现代化本地控制台，不是「临时调试页」。

**首屏结构（一构图，非仪表盘堆砌）**：

1. 品牌：**Desk Gateway** 作为主视觉锚点。  
2. 中央：**升降桌示意图**（侧视或简约等距线稿/矢量），桌面高度随状态变化。  
3. 主操作：大号 **升 / 停 / 降**（停最醒目）。  
4. 次要：档位 1/4（仅 `caps` 支持时显示）；Preset2/3 若 `NOT_SUPPORTED` 不展示或禁用。  
5. **童锁开关**（显式、可发现）；锁定时提示「原厂面板已锁定」。  
6. 状态文案：静止 / 上升中 / 下降中；有高度则显示 cm。  
7. 「原厂重置」：协议未解前不提供可点按钮（或灰显 +「待逆向」），避免误导。

**动效约定**：

| 条件 | 视觉行为 |
|---|---|
| `IDLE` | 桌子停在当前视觉高度；微弱呼吸感可选、勿喧宾夺主 |
| `MOVING_UP` | 桌面平滑上移；可加立柱伸缩；运动指示方向 |
| `MOVING_DOWN` | 桌面平滑下移 |
| `height_known=true` | 视觉高度按 `height_mm` 映射到行程比例（需配置 min/max mm） |
| `height_known=false` | **命令驱动相对动画**：升/降时持续缓动；`stop` 后立刻停住（不假装精确高度） |
| `stop` / 错误 | 动画立即停止；可短暂闪错误态 |

技术：静态资源嵌入固件（`esp_http_server` 提供 `/`、`/assets/*`）；原生 CSS/JS 或极轻量构建，**禁止依赖外网 CDN**（断网局域网也要能用）。

视觉方向（实现时遵守项目前端规则）：明确色板与字体；避免紫白渐变套模板、避免无意义卡片墙；动效服务状态反馈，不做噪声。

登录页：简洁密码框 + 品牌；登录成功进主控制台。

---

## 7. 安全

1. 上电默认静止（`stop` / idle DR）。  
2. 连续升/降最长超时（默认 15s）强制 `stop`。  
3. Web / 串口任一 `stop` 立即生效。  
4. Web 仅局域网 + Bearer 认证；UI 与 README 警告勿暴露公网。  
5. 电气：USB 供电 ESP32，与主机共地；不用桌子 3.3V 作主供电。  
6. 错 DR / 未知档位：拒绝，不电毁主机；机械风险靠人在场 + 超时 + 急停。  
7. 童锁 ON 时屏蔽所有运动来源；**不**屏蔽 `stop` / 解锁。  
8. 上+下 5s 重置：未逆向前不得注入伪造键码。

---

## 8. 分阶段交付（本设计覆盖的实现范围）

> 当前证据：M1/M2 代码已实现且编译通过；M1/M2 涉及串口、浏览器和真桌的退出标准仍未验收。
> M3 只有 Loctek/Jiecang stub，BLE 组件尚未实现。

| 里程碑 | 内容 | 退出标准 |
|---|---|---|
| **M0 文档** | 本文 + architecture overview + requirements 同步 | 你审阅通过 |
| **M1 骨架** | 目录重组、`desk_driver` + `desk_core`、迁入 `mxtark`、串口仍可用 | 串口 up/down/stop 与现 Phase1 等价 |
| **M2 WiFi+Web** | STA、认证、REST、短轮询、UI 动效、**全局童锁与来源权限** | 局域网可升降停；童锁可开关并阻止运动；动效随 status |
| **M3 stubs** | loctek/jiecang 空壳；**ble Accessory GATT 雏形或完整 stub 接口** | 编译可选；有 BLE 时可用调试器订阅 status |
| **Phase 2** | 双 RJ45 + 面板仲裁 + 全局童锁的 Panel 真机验收；BLE 外设联调 | 锁 ON 后所有入口不能运动；锁 OFF 后按来源权限仲裁；旋钮类可升降 |

「上+下 5s 重置」抓包与 Driver 实现：**独立后门禁**，不阻塞 M2。

Phase 2 中间人、生态接入的其余部分仍按需求文档。

### 8.1 生态扩展（Phase 3+，本设计周期不实现）

目标生态除 HA / Matter / Siri 外，明确包含 **小米米家** 与 **华为智慧生活 / 鸿蒙智联**（可玩性与国内适用性）。

调研结论（详见 [ecosystem-xiaomi-huawei.md](../../architecture/ecosystem-xiaomi-huawei.md)）：

| 路径 | 额外硬件 | 说明 |
|---|---|---|
| **Matter（推荐默认）** | 一般不需要 | 在现有 ESP32-S3 上增加 Matter 软件栈；凡支持 Matter 的米家/华为 App 可添加配件 |
| **米家原生上架** | **通常需要** 小米 MIIO 认证模组 | 开放平台 + MCU↔模组串口 + 认证；不是「只在 ESP32 上自实现米家协议」 |
| **华为原生上架** | **通常需要** HarmonyOS Connect / HiLink 认证模组 | 伙伴流程 + SDK + 认证；与小米模组一般 **不能共用一颗** |
| HA MQTT | 不需要 | 已规划中转路径 |

架构预留：`connectivity/matter`、`connectivity/ble`（**Accessory Profile**，见 [ble-accessory-profile.md](../../architecture/ble-accessory-profile.md)）；可选硬件 SKU 预留米家/华为模组焊盘。`desk_core` 不绑定任一厂商云协议。

**BLE 外设总线（摘要）**：Gateway 作 GATT Server，向 OLED+无极旋钮等配件 Notify 高度/状态，并接受升降 Write；与 Web 同级走 `desk_core`。盒子 MVP 仍无板载旋钮/屏——扩展靠外设，不靠把旋钮焊进网关。

---

## 9. 需求文档需同步的要点

（实现前写入 `docs/0-requirements.md`）

- 产品定位改为：**多厂商可适配的 Desk Gateway 平台**（先打通 mxtark）。
- Phase 1 退出标准增加：局域网 Web + 简单认证 + 控制升降停 + 童锁 API/UI（面板屏蔽 Phase 2 生效）。  
- 「多品牌开箱即用」仍非首版；改为「Driver 框架已就绪，其他厂商 stub」。  
- Web：局域网、简单密码、现代化 UI、升降示意图动效。  
- 登记原厂「上+下同时约 5s → 重置」为待逆向项。  
- 生态 Backlog：米家、华为智慧生活；默认优先 Matter；原生上架需认证模组（见生态调研文）。

---

## 10. 开放问题

1. 行程 min/max mm 默认值（有真实高度后映射用）。  
2. 默认密码文案与是否强制首登改密的严格程度。  
3. 原厂「重置」确切副作用（清什么）——以抓包后更新协议笔记为准。  
4. Matter 设备类型选型（Window Covering vs 其他）及米家/华为 App 实测支持度。  
5. 是否规划「米家版 / 华为版」独立硬件 SKU（双模组成本）。

**已拍板**：整体分层与目录；Web 局域网；SoftAP 配网；简单认证；短轮询状态；UI 现代化 + 桌子示意图实时动效；方案 B + 真 Web；**童锁屏蔽原厂面板**；上+下 5s 重置先文档后逆向；**米家/华为进生态目标，默认先走 Matter，原生上架需模组**；**BLE 为 OLED/旋钮等外设总线**（Gateway=GATT Server）。

---

## 11. 修订记录

| 版本 | 日期 | 说明 |
|---|---|---|
| 0.1 | 2026-08-06 | 初稿：平台分层、Driver、Web 认证与 UI/动效、交付里程碑 |
| 0.2 | 2026-08-06 | 初版童锁（面板失效、网关仍可控；该语义已被 0.4 取代）；登记上+下≈5s 重置待逆向 |
| 0.4 | 2026-08-11 | 童锁提升为全局最高权限；增加 REST / Bluetooth / Panel 来源权限与松键后恢复约束 |
| **0.3** | **2026-08-06** | 补充小米/华为生态调研结论与 Matter vs 认证模组双轨；链到 ecosystem 文档 |
| **0.4** | **2026-08-06** | BLE 定位为外设总线（OLED/旋钮等）；链到 ble-accessory-profile；与 Web 同级 desk_core |
