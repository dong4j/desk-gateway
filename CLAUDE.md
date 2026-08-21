# Desk Gateway Claude Instructions

> Claude Code 读取本文件；Cursor / Codex 等读取 [`AGENTS.md`](./AGENTS.md)。
> **两份必须保持同一份操作约束**，改一处必须改另一处。
> 维护日期：2026-08-21。完成度细节以状态文档为准，不要在本文件里复制一份会过期的清单。

## 动手前

1. 读 [`docs/standards/`](docs/standards/README.md) 下的全部规范，当前至少包括 [代码提交规范](docs/standards/git-commit-convention.md)。
2. 改功能或改口径前，先读对应的事实来源，不要凭训练数据或旧 README 臆造状态。
3. 未得到用户明确要求时，不要 commit、push、开 PR、改 git config。
4. **改代码、烧录、在多种修法里选一条落地，必须先等用户确认。** 诊断、读代码、复述日志可以先做；下面这些在用户点头之前禁止：
   - 修改固件、客户端、脚本、测试，或改会改变行为的口径文档
   - 把「确认是某某问题」当成授权实现；那只同意诊断，不等于让你改
   - 拉长超时、取消看门狗、改键码、烧录、`erase-flash`、为烧录而杀掉占用串口的 monitor
   有两种以上修法时，先用几句话列出利弊，等用户点名再动。用户说「改」「按这个做」「烧录」「直接烧」才算授权对应动作。

### 事实来源（不要互相抄出第二套真相）

| 问题 | 以谁为准 |
|---|---|
| 已完成 / 待验收 / 未实现 | [`docs/status/current-status-and-priorities.md`](docs/status/current-status-and-priorities.md) |
| V1 能否发布 | [`docs/status/v1-release-acceptance.md`](docs/status/v1-release-acceptance.md)（当前 **NO-GO**） |
| 怎么控桌 | [`docs/guides/control-methods.md`](docs/guides/control-methods.md) |
| 本地多端部署（改 IP / 密码 / 路径） | [`docs/guides/local-multi-client-setup.md`](docs/guides/local-multi-client-setup.md) |
| REST 契约 | [`docs/guides/rest-api.md`](docs/guides/rest-api.md)，路由以 `firmware/desk-gateway/components/connectivity/web/desk_web.c` 为准 |
| 接线、GPIO、真机步骤 | [`docs/guides/bringup-checklist.md`](docs/guides/bringup-checklist.md)、[`firmware/desk-gateway/README.md`](firmware/desk-gateway/README.md) |
| 分层与硬件拓扑 | [`docs/architecture/overview.md`](docs/architecture/overview.md) |
| 协议键码与时序 | [`docs/architecture/protocol-reverse-notes.md`](docs/architecture/protocol-reverse-notes.md) |
| 文档总目录 | [`docs/README.md`](docs/README.md) / [`docs/README.zh-CN.md`](docs/README.zh-CN.md) |
| 对外说明 | [`README.zh-CN.md`](README.zh-CN.md) / [`README.md`](README.md) |

## 现在是什么项目

Desk Gateway 是跑在 **ESP32-S3** 上的升降桌智能网关。厂商协议收进可插拔 **Desk Driver**；Web / REST / UART / BLE / 手机 / Watch / 键盘 / 语音 / D200H 只调 **`desk_core`**。

- **Phase 1 已完成**：可模拟原厂 Mxtark 面板，并在局域网和 BLE 上用多种客户端控真实桌子。
- **Phase 2 真桌已通过**：原厂面板透传、断线 STOP、仲裁、童锁真屏蔽。
- **产品高度源是 TOF400C**，不是控制盒数码管。TOF050C 是右侧间距，参与低位上升保护。
- **当前 Driver**：`mxtark` 已实现；`loctek` / `jiecang` 仍是 stub。
- **V1 未发布**：P0 真机门禁已过，P1 发布门禁未齐，结论仍是 **NO-GO**。

## 仓库地图

```text
firmware/desk-gateway/     ESP-IDF 主固件（目标芯片 esp32s3）
mobile/app/                iPhone / Android（React Native + Expo Development Build）
mobile/watch/              独立 Apple Watch App（XcodeGen + SPM）
integrations/              第三方入口（MCP、D200H、Karabiner、GoatRemote）
scripts/                   固件检查、完整烧录、desk-preset.sh
docs/                      文档（status / guides / architecture / hardware / future / history / standards）
docs/architecture/images/  架构 PNG 与生成脚本
```

## 项目开发规范（强制）

所有分析、修改、测试或提交代码前，必须遵守 [`docs/standards/`](docs/standards/README.md)。

- 提交格式：`type(scope): 中文描述`，见 [代码提交规范](docs/standards/git-commit-convention.md)。
- 一次提交只放一个可独立验证的小功能；不要夹带无关格式化。
- 只暂存当前任务文件；保留工作区里其他人的修改。
- 未经用户明确授权：禁止 push、创建远程分支、开 PR、rebase / reset 改写历史。

## ESP-IDF 固件构建环境（强制）

本项目固定使用 ESP-IDF v6.0.2，Python venv 位于项目机器的非默认目录。执行任何 ESP32 固件构建、检查、烧录或监视命令前，必须先在**同一个 Shell 进程**中加载以下环境：

```bash
export IDF_PYTHON_ENV_PATH=/Users/dong4j/.espressif/tools/python/v6.0.2/venv
export IDF_PYTHON_CHECK_CONSTRAINTS=no
source /Users/dong4j/.espressif/v6.0.2/esp-idf/export.sh >/dev/null
```

执行隔离固件构建时，直接使用下面这条完整命令：

```bash
zsh -lc 'export IDF_PYTHON_ENV_PATH=/Users/dong4j/.espressif/tools/python/v6.0.2/venv IDF_PYTHON_CHECK_CONSTRAINTS=no; source /Users/dong4j/.espressif/v6.0.2/esp-idf/export.sh >/dev/null && ./scripts/check-firmware.sh'
```

- 禁止先在未激活 ESP-IDF 的终端中执行一次构建，再根据失败结果补环境重试。
- 激活后必须先确认 `idf.py --version` 输出 `ESP-IDF v6.0.2`；未确认版本不得开始构建。
- `ESP-IDF not active`、`IDF_PATH` 缺失或 Python venv 路径错误属于环境准备失败，不能记为代码构建失败。
- 只有进入 CMake/Ninja 编译阶段后产生的错误，才能归因于固件代码或构建配置。

日常烧录用 [`scripts/flash-firmware.sh`](scripts/flash-firmware.sh)（full flash，含 `audio.bin`，保留 NVS）。不要用 `idf.py app-flash` 代替完整烧录去更新语音分区。`idf.py erase-flash` 会清空 NVS。

## 按区域验证

改了哪一层，就跑哪一层；不要用“编译过了”代替该层的检查。无法执行的验证必须如实写未跑，禁止写成已通过。

| 区域 | 命令 | 说明 |
|---|---|---|
| 固件逻辑 / Web 静态 | `./scripts/check-height-decoder.sh` | 不需要 IDF；含 host C 测试、WAV 合同、`www/*.js` |
| 固件可重复构建 | 上文隔离 `check-firmware.sh` | 会先跑 host 测试，再在临时目录编译 |
| 音频资源 | `./scripts/check-audio-assets.sh` | 16 kHz / 16-bit / mono PCM |
| 手机 App | `cd mobile/app && npm run typecheck && npm test` | BLE 不能用 Expo Go |
| Watch | `cd mobile/watch && swift test` | `project.yml` 是工程事实来源；生成的 `*.xcodeproj` 不提交 |
| 架构图 | `python3 docs/architecture/images/generate_architecture_pngs.py` | 只产出 PNG |

真机运动、BLE 射频、ToF、OLED 显示、功放音质 **不能**用单元测试或模拟器代替。iOS 27 真机安装见 [`docs/guides/mobile-ios-device-deployment.md`](docs/guides/mobile-ios-device-deployment.md)，不要直接 `npm run ios`。

## 硬约束

这些是反复被写错的事实。与代码或文档冲突时，先改到与下列约束一致，再报告完成。

### 控制面

- 任何控桌入口不得直接调厂商协议细节，只调 `desk_core`。
- 禁止在 Driver 里伪造未验证键码。原厂面板档位 **2 / 3** 与「上+下约 5 秒重置」的 `DR` **尚未验收 / 尚未抓包**，不得写成已支持。
- 童锁 ON：除 `STOP` 和解除童锁外，REST / 串口 / BLE / 原厂面板都不能启动或维持运动。
- 优先级：急停 > 全局童锁 > 来源权限 >（未锁且允许时）面板优先。
- HOLD 与 jog 不是一回事：按住升降要续期；jog 是短租约；松手、断连、重启、来源关闭都必须停桌。

### 高度与安全

- 产品高度 = 处理后的 **TOF400C** 距离。不要把控制盒 digit 解析当产品高度源。
- 默认 `min / max / preset1 / preset4` = **550 / 940 / 550 / 870 mm**。最低高度只约束档位输入，不触发下行 STOP。
- 高度未知、达到最高高度，或高度低于 800 mm 且右侧间距未知/小于 80 mm 时禁止上升。下降和 STOP 始终可用。
- 测试运动时必须写明：有人在桌旁；不要把“命令已发送”当成桌子真的动了。

### 硬件接线

| 总线 | GPIO | 用途 |
|---|---|---|
| 控制盒 CLK/DAT | GPIO4 / GPIO5 | 硬件 I²C Slave `@0x24` |
| 原厂面板 CLK/DAT | GPIO6 / GPIO7 | 9.6 kHz 软件代理；不得与控制盒短接 |
| ToF / OLED | GPIO10 / GPIO11 | 外设 I²C1 |
| MAX98357A | GPIO14 / 15 / 16 | BCLK / LRC / DIN |
| 状态灯 红 / 黄 / 蓝 | GPIO1 / GPIO2 / GPIO8 | 固件已驱动；真机未验收。GPIO17 仍留给功放 `SD`；GPIO48 板载 RGB 仍空闲 |

- ESP32 用 USB-C 独立供电。RJ45 红线 3.3V **不得**接到 ESP32 `3V3`。
- 拔掉原厂面板后必须补 2 kΩ（可用 2.2 kΩ）上拉到控制盒 CLK/DAT。
- 不要把面板 CLK/DAT 跳到控制盒 CLK/DAT 上绕过 ESP32。

### 连接与安全边界

- SoftAP：SSID `DeskGateway`，密码 `desk-gateway`，配网页 `http://192.168.4.1/`。Web 默认密码同样是 `desk-gateway`。
- Web / REST **仅限局域网**，不要做公网端口映射，也不要为了“方便调试”把鉴权拆掉。
- 脚本鉴权用 `X-Desk-Key`；浏览器用 `Authorization: Bearer`。两者等价，值等于当前 Web 密码。
- BLE 广播名 `DeskGateway`；最多三个 Central；单一运动所有者；非所有者收到 Desk Busy `0x80`；任意 STOP 始终有效。
- 小智 MCP 只允许五个固定工具，不接受任意 URL / Method / 目标高度。

### 工程卫生

- 不要提交 `build/`、`sdkconfig`、`managed_components/`、`.env`、证书、生成的 Watch `xcodeproj`。
- 不要发明日志路径、抓包文件或验收证据。没有文件就写“无证据 / 未执行”。
- 架构图用 PNG（`docs/architecture/images/`），**不要用 drawio**。改图就改生成脚本并重新导出。
- REST 路由变了必须同步 `docs/guides/rest-api.md`；用户控桌方式变了必须同步 `docs/guides/control-methods.md`。
- 给人看的说明文档：英文是 `README.md`，中文是 `README.zh-CN.md`。某个目录有其中一个，就必须有另一个。
- 注释写「为什么」和约束，不要只复述代码。标识符用英文；给人看的文档用中文（用户另有要求除外）。

## 状态口径（强制）

- **禁止**把 V1 写成 GO / 已发布。
- **禁止**把下列项标成已完成（代码在不代表验收过）：
  - V1-08 的 iPhone 连续 20 次重连与权限异常矩阵（Android / Watch 真机控制已过，本项只是部分通过）
  - V1-09 BLE/Wi-Fi 超距回退
  - V1-10 自定义档位 / B12 真桌验收
  - V1-11 OLED 30 分钟稳定性
  - V1-12 TestFlight / Android 内测
  - V1-13 番茄语音真机（功放/扬声器未验收；可选是否纳入 V1）
  - V1-14 小智端到端（云端 Endpoint 与真桌语音未跑；可选）
  - Matter / Home Assistant / OTA / Loctek / Jiecang / 原厂键 2、3
- 编译、host 测试、模拟器、Development Build 安装成功，只能写成对应那一层通过，不能升级成“真机验收通过”。
- 只有代码完成且核心真机路径由负责人确认后，才能把状态文档里的项从「待验收」挪到「已完成」。

## 改完要同步什么

行为或口径变了，按需更新，不要只改代码：

1. [`docs/status/current-status-and-priorities.md`](docs/status/current-status-and-priorities.md)
2. 若动到 V1 门禁：[`docs/status/v1-release-acceptance.md`](docs/status/v1-release-acceptance.md)
3. 用户路径：[`docs/guides/`](docs/guides/) 与 README
4. 接线 / 高度策略：固件 README 与 `docs/guides/bringup-checklist.md`
5. 架构事实：`docs/architecture/overview.md`；需要改图时重跑 PNG 生成脚本
