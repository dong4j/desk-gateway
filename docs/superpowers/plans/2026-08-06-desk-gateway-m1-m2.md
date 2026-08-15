# Desk Gateway M1+M2 Implementation Plan

> **历史计划说明（2026-08-09）**：M1/M2 代码已落地并通过编译，但真机验收仍开放。
> 本文保留原始任务与勾选状态作为实施记录；Task 8 的同步 SSE 方案已撤回，当前产品使用 250ms 短轮询，
> 现行契约以平台设计和架构总览为准。

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将现有 Phase1 固件演进为平台工程 `firmware/desk-gateway/`：可插拔 `desk_driver` + 统一 `desk_core`（含童锁/超时），默认驱动 `mxtark`，并交付局域网 WiFi + 带认证的现代化 Web（REST/SSE、升降示意图动效）。

**Architecture:** 单 ESP-IDF 工程；所有控桌入口（UART、Web）只调 `desk_core`；`mxtark` 实现 I²C Slave @0x24 回 `DR`；Web 静态资源嵌入固件。BLE / Matter / 双 RJ45 / 米家华为原生 **不在本计划**。

**Tech Stack:** ESP-IDF ≥ 5.2，目标 `esp32s3`（YD-ESP32-S3 N16R8），`esp_http_server`，NVS，FreeRTOS，原生 HTML/CSS/JS（无 CDN）。

**Spec:** `docs/superpowers/specs/2026-08-06-desk-gateway-platform-design.md`（已批准 v0.4）

## Global Constraints

- 上电默认静止：`DR=0x2E` / `desk_core_stop()`；禁止未验证键码（含 Preset2/3、上+下重置）。
- 升/降最长按住默认 **15s**（`CONFIG_DESK_MOTION_TIMEOUT_MS`）。
- Web **仅局域网**；简单 Bearer 密码；默认密码写入 README，UI 提示修改。
- USB 供电 ESP32，与主机共地；不用桌子 3.3V 作主供电。
- 默认 I²C：SCL=GPIO4、SDA=GPIO5；地址 7-bit `0x24`。
- 注释：新文件需模块/函数说明（解释为什么）；中文文档、代码标识符英文。
- 本计划不实现：BLE GATT、Matter、双 RJ45、Loctek/Jiecang 真协议、高度 digit 嗅探（可留 `UNSUPPORTED`）。

## Scope split（后续计划，勿塞进本计划）

| 后续计划 | 内容 |
|---|---|
| BLE Accessory | `docs/architecture/ble-accessory-profile.md` |
| Phase 2 MITM | 双 RJ45 + 面板仲裁 + 童锁真屏蔽 |
| Ecosystem | Matter / 米家模组 / 华为模组 |

---

## File map（将创建 / 迁移）

```text
firmware/desk-gateway/
  CMakeLists.txt
  sdkconfig.defaults
  README.md
  main/
    CMakeLists.txt
    Kconfig.projbuild          # 引脚、超时、默认密码相关
    app_main.c
  components/
    desk_driver/
      include/desk_driver.h    # ops + caps + status 枚举
      desk_driver_registry.c
      CMakeLists.txt
    desk_core/
      include/desk_core.h
      desk_core.c              # 超时、童锁 NVS、扇出到 driver
      CMakeLists.txt
    drivers/
      mxtark/
        include/mxtark.h
        mxtark.c          # 自 phase1 desk_dr + i2c_panel_slave 迁入
        CMakeLists.txt
      loctek/                  # stub: init 返回 OK，动作 NOT_SUPPORTED
      jiecang/                 # stub
    connectivity/
      wifi/
        include/desk_wifi.h
        desk_wifi.c
        CMakeLists.txt
      web/
        include/desk_web.h
        desk_web.c             # httpd + auth + REST + SSE
        www/                   # 静态页（嵌入）
          index.html
          login.html
          app.js
          style.css
        CMakeLists.txt
  # 可选：host 侧纯逻辑测
  host_test/
    test_desk_core_logic.c     # 若采用；否则用手工验收清单

firmware/phase1-panel-slave/README.md  # 改为指向 desk-gateway，停止双源开发
```

---

### Task 1: 脚手架 `firmware/desk-gateway` + sdkconfig

**Files:**
- Create: `firmware/desk-gateway/CMakeLists.txt`
- Create: `firmware/desk-gateway/sdkconfig.defaults`
- Create: `firmware/desk-gateway/main/CMakeLists.txt`
- Create: `firmware/desk-gateway/main/Kconfig.projbuild`
- Create: `firmware/desk-gateway/main/app_main.c`（空壳：只 log + `nvs_flash_init`）
- Create: `firmware/desk-gateway/README.md`（编译烧录、接线、默认密码占位）

**Interfaces:**
- Consumes: 无
- Produces: 可 `idf.py set-target esp32s3 && idf.py build` 的空工程

- [ ] **Step 1: 写工程根 CMakeLists.txt**

```cmake
# Desk Gateway 主固件：多 Driver 平台 + Web
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(desk-gateway)
```

- [ ] **Step 2: 写 sdkconfig.defaults（N16R8）**

内容对齐现有 `firmware/phase1-panel-slave/sdkconfig.defaults`：`esp32s3`、16MB Flash、Octal PSRAM、UART 115200。

- [ ] **Step 3: 写 main 空壳与 Kconfig**

Kconfig 项至少：`DESK_I2C_SCL_GPIO`（默认 4）、`DESK_I2C_SDA_GPIO`（默认 5）、`DESK_MOTION_TIMEOUT_MS`（15000）、`DESK_GOTO_HOLD_MS`、`DESK_SAVE_HOLD_MS`、`DESK_WEB_DEFAULT_PASSWORD`（string，默认 `desk-gateway`）。

- [ ] **Step 4: 编译验证**

```bash
cd firmware/desk-gateway
. $HOME/esp/esp-idf/export.sh   # 按本机 IDF 路径
idf.py set-target esp32s3
idf.py build
```

Expected: Build complete，无 error。若本机无 IDF：先按 `docs/2-esp32-s3-n16r8-platform.md` 安装，再继续后续 Task。

- [ ] **Step 5: Commit**

```bash
git add firmware/desk-gateway
git commit -m "$(cat <<'EOF'
chore: scaffold desk-gateway ESP-IDF project for platform build

EOF
)"
```

---

### Task 2: `desk_driver` 接口与注册表

**Files:**
- Create: `firmware/desk-gateway/components/desk_driver/include/desk_driver.h`
- Create: `firmware/desk-gateway/components/desk_driver/desk_driver_registry.c`
- Create: `firmware/desk-gateway/components/desk_driver/CMakeLists.txt`

**Interfaces:**
- Consumes: 无
- Produces:

```c
typedef enum {
    DESK_STATUS_IDLE = 0,
    DESK_STATUS_MOVING_UP,
    DESK_STATUS_MOVING_DOWN,
    DESK_STATUS_GOTO_PRESET,
    DESK_STATUS_ERROR,
} desk_status_t;

typedef struct {
    bool hold_up_down;
    bool preset_goto;   /* bit 含义由 caps_preset_mask 细拆也可 */
    bool preset_save;
    bool height;
    uint8_t preset_mask; /* bit0=preset1 … */
} desk_caps_t;

typedef struct desk_driver {
    const char *name;
    esp_err_t (*init)(void);
    esp_err_t (*deinit)(void);
    esp_err_t (*stop)(void);
    esp_err_t (*hold_up)(void);
    esp_err_t (*hold_down)(void);
    esp_err_t (*goto_preset)(uint8_t n);   /* 1-based；不支持 → ESP_ERR_NOT_SUPPORTED */
    esp_err_t (*save_preset)(uint8_t n);
    esp_err_t (*get_height_mm)(int *out_mm); /* 不支持 → ESP_ERR_NOT_SUPPORTED */
    desk_status_t (*get_status)(void);
    desk_caps_t (*get_caps)(void);
} desk_driver_t;

esp_err_t desk_driver_register(const desk_driver_t *drv);
const desk_driver_t *desk_driver_get_active(void);
```

- [ ] **Step 1: 实现 header + registry（单活跃驱动指针）**

`desk_driver_register`：若已有活跃驱动先 `deinit`，再 `init` 新驱动并保存指针。

- [ ] **Step 2: 编译（main 仍可不链接驱动）**

`idf.py build` Expected: PASS（若 main 未引用可先不链；下一步 Task 再链）。

- [ ] **Step 3: Commit**

```bash
git add firmware/desk-gateway/components/desk_driver
git commit -m "$(cat <<'EOF'
feat: add desk_driver API and registry

EOF
)"
```

---

### Task 3: `desk_core`（超时、童锁、统一 API）

**Files:**
- Create: `firmware/desk-gateway/components/desk_core/include/desk_core.h`
- Create: `firmware/desk-gateway/components/desk_core/desk_core.c`
- Create: `firmware/desk-gateway/components/desk_core/CMakeLists.txt`

**Interfaces:**
- Consumes: `desk_driver_t` from Task 2
- Produces:

```c
esp_err_t desk_core_init(const desk_driver_t *drv);
esp_err_t desk_core_stop(void);
esp_err_t desk_core_hold_up(void);
esp_err_t desk_core_hold_down(void);
esp_err_t desk_core_goto_preset(uint8_t n);
esp_err_t desk_core_save_preset(uint8_t n);
esp_err_t desk_core_set_child_lock(bool enabled); /* NVS 持久化 */
bool      desk_core_get_child_lock(void);

typedef struct {
    desk_status_t status;
    int height_mm;       /* -1 = unknown */
    bool height_known;
    bool child_lock;
    const char *driver;
} desk_core_snapshot_t;

desk_core_snapshot_t desk_core_snapshot(void);
```

行为：
- `hold_up`/`hold_down` 启动 `esp_timer` 单次超时 → 到期 `stop`。
- `goto`/`save` 用 Kconfig 默认 hold，到期 `stop`（与现 `desk_dr_pulse` 一致）。
- `child_lock` 只存状态；Phase1 无面板可屏蔽，但 API/NVS/快照必须完整。
- 所有入口先拿到驱动；`stop` 必须同步调用 `drv->stop()`。

- [ ] **Step 1: 写 desk_core.h / desk_core.c**

从 `firmware/phase1-panel-slave/main/desk_dr.c` 迁超时逻辑到 core（驱动只负责设 DR）。

- [ ] **Step 2: 手工逻辑自检（无硬件）**

在 `app_main` 临时调用（或 host_test）：`init` → `hold_up` → snapshot.status==MOVING_UP → `stop` → IDLE；`set_child_lock(true)` 重启后仍 true（NVS）。验证后删临时调用。

- [ ] **Step 3: Commit**

```bash
git add firmware/desk-gateway/components/desk_core
git commit -m "$(cat <<'EOF'
feat: add desk_core with motion timeout and child lock

EOF
)"
```

---

### Task 4: `mxtark` 驱动（迁 I²C Slave）

**Files:**
- Create: `firmware/desk-gateway/components/drivers/mxtark/*`（从 phase1 的 `desk_dr` 键码常量 + `i2c_panel_slave` 迁入）
- Modify: `firmware/desk-gateway/main/app_main.c` — `desk_core_init(&mxtark_driver)`

**Interfaces:**
- Consumes: `desk_driver_t`；GPIO from Kconfig
- Produces: `extern const desk_driver_t mxtark_driver;`

映射（已批准契约）：

| ops | DR |
|---|---|
| stop | `0x2E` |
| hold_up | `0x47` |
| hold_down | `0x4F` |
| goto 1/4 | `0x17`/`0x2F` |
| save 1/4 | `0x57`/`0x6F` |
| height | `ESP_ERR_NOT_SUPPORTED` |
| get_status | 由驱动内部当前 DR 推导 |

- [ ] **Step 1: 迁 I²C Slave 与 DR 状态进 mxtark**

保持 phase1 的 ISR→队列→`i2c_slave_write` 模式；DR 原子变量留在驱动内。

- [ ] **Step 2: 实现 `mxtark_driver` ops 表并注册**

- [ ] **Step 3: 烧录 + 串口冒烟（有硬件时）**

接线：GND 共地，CLK→GPIO4，DAT→GPIO5，USB 供电。  
临时：在 console 未就绪前可用 log；或提前做 Task 5。  
Expected：主机轮询时 Slave ACK；默认不运动。

- [ ] **Step 4: Commit**

```bash
git add firmware/desk-gateway/components/drivers/mxtark firmware/desk-gateway/main
git commit -m "$(cat <<'EOF'
feat: migrate mxtark I2C panel slave as first desk_driver

EOF
)"
```

---

### Task 5: UART console → `desk_core`

**Files:**
- Create: `firmware/desk-gateway/main/console_cmd.c` / `.h`（自 phase1 改编）
- Modify: `app_main.c` 启动 `console_cmd_task`

**Interfaces:**
- Consumes: `desk_core_*`
- Produces: 命令 `help|status|idle|stop|up|down|p1|p4|save1|save4|lock|unlock|wifi …`

- [ ] **Step 1: 改写 console，全部经 desk_core（禁止直接调驱动）**

`lock` / `unlock` → `desk_core_set_child_lock`。

- [ ] **Step 2: 硬件验收**

`up` → 桌升；`stop` → 停；`down` → 降；`lock` 后 `status` 显示 child_lock=1。

- [ ] **Step 3: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat: wire UART console through desk_core

EOF
)"
```

---

### Task 6: WiFi STA（NVS 凭证 + 串口配网）

**Files:**
- Create: `firmware/desk-gateway/components/connectivity/wifi/*`

**Interfaces:**
- Consumes: NVS
- Produces:

```c
esp_err_t desk_wifi_init(void);           /* 读 NVS，有凭证则连 STA */
esp_err_t desk_wifi_set_sta(const char *ssid, const char *pass); /* 写入 NVS 并重连 */
bool      desk_wifi_is_connected(void);
esp_err_t desk_wifi_get_ip(char *buf, size_t len);
```

- [ ] **Step 1: 实现 STA + 事件循环；失败只打日志，不复位成运动**

- [ ] **Step 2: console：`wifi <ssid> <pass>` 与 `wifi status`**

- [ ] **Step 3: 真机连家宽，确认拿到 IP**

- [ ] **Step 4: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat: add WiFi STA with NVS credentials and serial provisioning

EOF
)"
```

---

### Task 7: Web 认证 + REST API

**Files:**
- Create: `firmware/desk-gateway/components/connectivity/web/desk_web.c` 等
- NVS key：`web_password`；缺省用 `CONFIG_DESK_WEB_DEFAULT_PASSWORD`

**Interfaces:**
- Consumes: `desk_core_*`，`desk_wifi_*`
- Produces: HTTP 路由（均需 Bearer，除 login）

| Method | Path | Body / 行为 |
|---|---|---|
| POST | `/api/v1/auth/login` | `{"password":"..."}` → `{"token":"..."}` |
| POST | `/api/v1/auth/password` | `{"password":"new"}` |
| POST | `/api/v1/desk/up` | hold_up |
| POST | `/api/v1/desk/down` | hold_down |
| POST | `/api/v1/desk/stop` | stop |
| POST | `/api/v1/desk/preset/{n}/goto` | |
| POST | `/api/v1/desk/preset/{n}/save` | |
| POST | `/api/v1/desk/child-lock` | `{"enabled":true\|false}` |
| GET | `/api/v1/desk/status` | snapshot JSON |

Token：随机 16+ 字节 hex，存 RAM；重启失效。

- [ ] **Step 1: 实现 httpd + JSON（cJSON 或手写小解析）+ Bearer 校验中间层**

- [ ] **Step 2: curl 验收**

```bash
TOKEN=$(curl -s -X POST http://$IP/api/v1/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"password":"desk-gateway"}' | jq -r .token)
curl -s http://$IP/api/v1/desk/status -H "Authorization: Bearer $TOKEN"
curl -s -X POST http://$IP/api/v1/desk/up -H "Authorization: Bearer $TOKEN"
curl -s -X POST http://$IP/api/v1/desk/stop -H "Authorization: Bearer $TOKEN"
```

Expected：登录成功；up/stop 桌子响应；无 Token 返回 401。

- [ ] **Step 3: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat: add authenticated REST API for desk control

EOF
)"
```

---

### Task 8: SSE `/api/v1/desk/events`

**Files:**
- Modify: `desk_web.c`

- [ ] **Step 1: 实现 SSE：状态变化立即推；静止心跳 1–2s**

事件 JSON 字段：`status,height_mm,height_known,child_lock,driver,ts_ms`（与定稿一致）。

- [ ] **Step 2: 用浏览器或 curl `-N` 观察 `up` 时 `moving_up`**

- [ ] **Step 3: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat: add SSE desk status event stream

EOF
)"
```

---

### Task 9: 现代化 Web UI（登录 + 控制台 + 桌子动效）

**Files:**
- Create: `firmware/desk-gateway/components/connectivity/web/www/login.html`
- Create: `firmware/desk-gateway/components/connectivity/web/www/index.html`
- Create: `firmware/desk-gateway/components/connectivity/web/www/app.js`
- Create: `firmware/desk-gateway/components/connectivity/web/www/style.css`
- Embed：CMake `TARGET_ADD_EMBED_FILES` 或 SPIFFS；**禁止外网 CDN**

**UX（定稿）：**
- 品牌 Desk Gateway 主视觉；中央升降桌 SVG；大号 升 / **停** / 降；童锁开关；档位 1/4（caps）。
- `moving_up/down`：桌面平滑移动；`height_known=false` 时相对缓动，stop 立即停。
- 登录页；token 存 `sessionStorage`；API 带 Bearer。
- 视觉：自定色板与字体；避免紫白模板风、避免卡片墙。

- [ ] **Step 1: 实现 login.html + index 静态结构与 SVG 桌子**

- [ ] **Step 2: app.js：SSE 优先，失败 250ms 轮询 status；按钮调 REST**

- [ ] **Step 3: 嵌入固件并 `idf.py build flash`**

- [ ] **Step 4: 浏览器验收清单**

1. 打开 `http://$IP/` → 登录  
2. 升 → 示意图上升 + 真桌升  
3. 停 → 动画与桌子停  
4. 降 → 对称  
5. 童锁开关 → status 同步  
6. 断网 CDN 仍可用（本地资源）

- [ ] **Step 5: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat: add local web console with desk motion visualization

EOF
)"
```

---

### Task 10: stub 驱动 + 废弃双源 + 文档收尾

**Files:**
- Create: `drivers/loctek`、`drivers/jiecang` stub（`get_caps` 全 false；动作为 `ESP_ERR_NOT_SUPPORTED`）
- Modify: `firmware/phase1-panel-slave/README.md` → 指向 `../desk-gateway/`，注明停止维护
- Modify: `firmware/desk-gateway/README.md` — 完整接线、命令、默认密码、安全警告
- Modify: `docs/2-esp32-s3-n16r8-platform.md` — 主工程路径改为 `firmware/desk-gateway/`

- [ ] **Step 1: 加 stub，Kconfig 选驱动仅 mxtark 默认可编译**

- [ ] **Step 2: 更新文档交叉链接**

- [ ] **Step 3: 全量回归（串口 + Web）按需求 P1-F01…F11 勾选**

- [ ] **Step 4: Commit**

```bash
git commit -m "$(cat <<'EOF'
docs: point phase1 tree to desk-gateway and document M1/M2 usage

EOF
)"
```

---

## Spec coverage（自检）

| 定稿项 | Task |
|---|---|
| desk_driver / registry | 2 |
| desk_core 超时 | 3 |
| 童锁 API/NVS/UI | 3, 7, 9 |
| mxtark I²C | 4 |
| UART 经 core | 5 |
| WiFi STA | 6 |
| REST + Bearer | 7 |
| SSE | 8 |
| 现代化 UI + 动效 | 9 |
| loctek/jiecang stub | 10 |
| 上+下重置不实现 | 全局约束（不写码） |
| BLE / Matter / Phase2 MITM | 明确排除 |

## Placeholder scan

无 TBD 实现步骤；UUID/软 AP 配网不在本计划。

---

## Execution handoff

Plan complete and saved to `docs/superpowers/plans/2026-08-06-desk-gateway-m1-m2.md`.

**Two execution options:**

1. **Subagent-Driven（推荐）** — 每 Task 新开子代理，Task 间复审，迭代快  
2. **Inline Execution** — 本会话按 executing-plans 连续执行并设检查点  

你选哪一种？若本机还没装 ESP-IDF，建议先装好再开 Task 1 编译门禁。
