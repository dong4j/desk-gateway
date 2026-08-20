# 原厂面板继电器旁路 Implementation Plan

> **For agentic workers:** 第一期是硬件接线，不要改固件。真机门禁通过后再单独立项做 GPIO9 / Web。步骤用 `- [ ]` 跟踪。

**Goal:** 在不拔 RJ45 网线的前提下，用两块 DR21A01（5V DPDT）加一只 MTS-102，把总线在「ESP32 网关」和「原厂面板直连控制盒」之间切换。

**Architecture:** 继电器断电常闭 = 左右口 CLK/DAT 经 NC 短接、GPIO4–7 离开总线。吸合后 COM 接到 NO，恢复现有 GPIO4/5 控制盒、GPIO6/7 面板代理。红 / 绿不进继电器。左口 2 kΩ 不拆。

**Tech Stack:** 智胜 DR21A01 5V ×2（HK19F DPDT）、MTS-102、10 kΩ 下拉、YD-ESP32-S3 USB 5V。现有 ESP-IDF 固件第一期不改。

**Spec:** [`docs/hardware/panel-bypass-relay.md`](../../hardware/panel-bypass-relay.md)

## Global Constraints

- 真机未接线、固件未实现、未验收；禁止写入状态文档「已完成」。
- 任何控桌入口仍只调 `desk_core`；直连时 ESP32 必须电气离开总线，不得伪造键码。
- 红线 3.3V 不得接 ESP32 `3V3`；线圈 5V 不得取自桌子红线。
- GPIO12/13 是 ToF XSHUT，GPIO17 留给功放 `SD`，GPIO35–37 为 N16R8 内部占用。以后固件用 GPIO9 打两块 `IN`。
- 童锁 ON、原厂键 2/3、`DR` 重置与本计划无关；直连时童锁/ToF 保护失效是预期。
- 测试运动必须有人在桌旁；「继电器吸合」不等于桌子动了。
- 第一期不改 REST、Web、`desk_core`、架构 PNG。

## File map

| 路径 | 职责 |
|---|---|
| `docs/hardware/panel-bypass-relay.md` | 方案事实来源：背景、照片、脚位、门禁 |
| `docs/images/hardware/dr21a01-*.png` | 选购规格、丝印脚位、HK19F 实物 |
| `docs/guides/bringup-checklist.md` | 只加指向，不把旁路勾成已验收 |
| `docs/status/current-status-and-priorities.md` | 记为未实现 |
| 固件 / `www/*.js` | **第一期不改** |

---

### Task 1: 到货核对

**Files:** 无代码。对照 `docs/images/hardware/dr21a01-pinout.png`。

- [ ] **Step 1: 确认 SKU**

两块（或三块）丝印均为 `DR21A01`，`5V` 方框已点选，不是 12V。继电器为橙色 HK19F 类，负载丝印含 `2A 30V DC` 或 `1A 125V AC`。

- [ ] **Step 2: 数脚**

每块 9 个焊盘，从左到右：`NO2 NO1 NC2 NC1 COM2 COM1 IN VCC GND`。只有 6 针且没有 NC/NO 则退货。

- [ ] **Step 3: 确认机械开关**

MTS-102（或等效 ON-ON 钮子）能串进 5V。另有一只 10 kΩ 电阻给 `IN` 下拉。

Expected: 零件与 spec 文档第 4 节一致后才能上桌。

---

### Task 2: 断电改飞线

**Files:** 无代码。接线表以 spec 第 5 节为准。

- [ ] **Step 1: 断电**

拔 ESP32 USB，升降桌断电。不要带电拔 CLK/DAT 飞线。

- [ ] **Step 2: 保持不动的部分**

左右口红线跳线、绿线跳线、左口两只 2 kΩ、ToF/OLED/I2S/状态灯飞线全部不拆。

- [ ] **Step 3: 插入 CLK 模块**

断开「左口 CLK→GPIO4」「右口 CLK→GPIO6」。按 spec 表接到 COM1/COM2/NO1/NO2。NC1 用短跳线接到 NC2。

- [ ] **Step 4: 插入 DAT 模块**

同样处理左口 DAT / GPIO5、右口 DAT / GPIO7。NC1 短接 NC2。

- [ ] **Step 5: 并联控制脚**

两块 `GND` → ESP32 GND。两块 `VCC` 先入 MTS-102 公共端，开关另一侧接开发板 `5V`（若排针无 5V，先短接板背 `IN-OUT`，仍禁止桌子红线）。两块 `IN` 并联，经 10 kΩ 到 GND。GPIO9 第一期可以不接。

- [ ] **Step 6: 断电导通检查**

执行 spec 第 8.1 节全部条目。任一项失败先停，不要上电。

---

### Task 3: 直连真机

**Files:** 无代码。MTS-102 切断 `VCC`，或 `IN` 保持低。

- [ ] **Step 1: 只给桌子上电**

ESP32 USB 先不插。面板应亮，红↔绿约 3.3V。

- [ ] **Step 2: 短行程下降**

有人在桌旁。点按下降，松开即停。

- [ ] **Step 3: 短行程上升**

点按上升，松开即停。若上升被机械或控制盒自己拦住，记录现象，不要改成「命令已发送」。

- [ ] **Step 4: 掉电保护**

插上 ESP32 USB（固件仍是当前网关固件，但 `IN` 为低、继电器不应吸合）。面板仍应能升降。再拔 USB，面板仍应能升降。

Expected: spec 第 8.2 节可勾选。失败则回到 Task 2 查 NC 短接和 COM 是否接反。

---

### Task 4: 网关真机

**Files:** 无代码。MTS-102 接通 `VCC`，把并联后的 `IN` 临时接到 ESP32 3.3V（不要接到 5V）。

- [ ] **Step 1: 先停桌再吸合**

桌子空闲后接通 `VCC` 并给 `IN` 3.3V，应听到两只继电器几乎同时吸合，模块指示灯亮。

- [ ] **Step 2: 看启动日志**

监视器应仍有 `mxtark: I2C slave @0x24 SCL=4 SDA=5` 和 `mxtark_panel: software panel proxy SCL=6 SDA=7`。

- [ ] **Step 3: 面板与 Web 短行程**

按 bringup B.4.2：面板升/降松开即停；Web 按住升/降、松手停止。有人在桌旁。

- [ ] **Step 4: 童锁与断线**

童锁 ON 后面板和 Web 不能启动运动，STOP 有效。运动中拔右口网线，桌子停止。

- [ ] **Step 5: 切回直连**

空闲后拨 MTS-102 切断 `VCC`，继电器释放，重复 Task 3 短行程。

Expected: spec 第 8.3 节可勾选。全部通过后才能把 GPIO9 焊上并开第二期固件计划。

---

### Task 5: 文档勾选（硬件通过之后）

**Files:**
- Modify: `docs/hardware/panel-bypass-relay.md` 第 8 节对应复选框
- Modify: `docs/guides/bringup-checklist.md` 仅当负责人确认后增加已勾选的 B.5，不得提前

- [ ] **Step 1: 只勾已做的检查**

无日志、无真机现象的项保持未勾选。禁止把「继电器咔哒」升级成「真机验收通过」。

- [ ] **Step 2: 状态文档**

硬件通过后，把 `docs/status/current-status-and-priorities.md` 第 4 节对应条从「方案已选定」改成「硬件已接线，固件未实现」或「待验收」，**不要**移入第 2 节已完成，直到第二期固件也由负责人确认。

---

## 第二期（本计划不实施）

等 Task 3–4 真机通过后再写独立计划，接口冻结如下：

- GPIO9 推挽输出，复位默认低，10 kΩ 硬件下拉保留。
- 进入网关：`desk_core` 已输出空闲 / STOP 之后才把 GPIO9 拉高。
- 进入直连：先 STOP，再拉低 GPIO9。
- REST / Web 仅局域网，沿用现有 Bearer；路由变更必须同步 `docs/guides/rest-api.md` 与 `docs/guides/control-methods.md`。
- MTS-102 切断 `VCC` 时固件 GPIO9 无效，Web 必须显示旁路不可达或已直连，不得假装仍在中间人模式。

## Out of scope

固件、Web、架构 PNG 重绘、拆左口 2 kΩ、左右口 CLK/DAT 焊死、V1 GO。
