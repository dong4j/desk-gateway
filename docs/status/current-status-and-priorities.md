# Desk Gateway 当前状态与任务优先级

| 项 | 内容 |
|---|---|
| 文档编号 | DG-STATUS-001 |
| 版本 | 1.7 |
| 日期 | 2026-08-17 |
| 作用 | 当前完成度和后续任务的唯一汇总入口 |

本文汇总当前代码状态和用户真机验证结果。协议细节、接线步骤和分层设计仍分别以
[协议逆向笔记](../architecture/protocol-reverse-notes.md)、[真机验收清单](../guides/bringup-checklist.md)和
[架构总览](../architecture/overview.md)为准。
怎么控桌见 [多种方式控制升降桌](../guides/control-methods.md)。
V1 的执行顺序、证据记录和最终 GO/NO-GO 签署统一使用
[V1 版本验收](v1-release-acceptance.md)，本文不重复记录逐次验收结果。

**Phase 1 已完成。** 网关可以模拟原厂面板，并在局域网 / BLE 上用 Web、REST、串口、
iPhone App、Android App、Watch、键盘、旋钮、GoatRemote、小智 MCP 和 Ulanzi D200H
操作同一张桌子。

2026-08-17 负责人确认下列真机门禁已通过：Phase 2 透传 / 断线 STOP / 仲裁 / 童锁真屏蔽、
异常停止矩阵、Android 真机、iPhone + Apple Watch + Android 三客户端并发、双 ToF 档位 /
最高高度 / 右侧障碍物安全矩阵。后续工作是第 3 节剩余 P1 和第 4 节 Phase 3 生态。

## 1. 状态定义

| 状态 | 含义 |
|---|---|
| ✅ 已完成 | 代码已实现，核心路径已经在当前升降桌或 iPhone 上验证 |
| 🟡 待验收 | 代码已实现或主体已完成，但缺少完整真机、异常或跨平台验证 |
| ⬜ 未实现 | 只有方案文档、接口占位或尚未开始开发 |
| ⛔ 外部阻塞 | 继续工作依赖待到货硬件、抓包或第三方条件 |

“已完成”表示代码已落地，并且对应核心真机路径已由负责人确认通过。
V1 是否发布仍以 [V1 版本验收](v1-release-acceptance.md) 的必选门禁签署为准。

## 2. 已完成

### 2.1 固件与桌子控制

- [x] ESP32-S3 主固件可在 ESP-IDF 6.0.2 下构建、烧录和启动。
- [x] `mxtark` 已恢复 ESP32-S3 硬件 I²C Slave `@0x24` 的稳定控制架构。
- [x] 固件内部 `yourdesk_v1` / `yourdesk` 命名已统一为 `mxtark`，重命名审计未发现业务逻辑差异。
- [x] 提交 `3269faa` 已通过真机验证：按住升高、降低连续运行，松手停止。
- [x] Web、REST、BLE、童锁和来源权限继续通过统一 `desk_core` 控制运动。
- [x] TOF050C / TOF400C 已接入独立 I²C，两路数据经过 5 点中值、3 mm 死区和 1 秒过期处理。
- [x] TOF400C 已直接作为产品高度进入 Web、REST、BLE、OLED、原厂面板、档位闭环和最高高度保护。
- [x] TOF050C 已进入低于 800 mm 时的右侧间距保护；右侧距离未知或小于 80 mm 时禁止上升。
- [x] 最低档位高度、最高安全高度和档位 1 / 4 已持久化，可由 Web 和手机 App 配置和同步；
  `min / max / preset1 / preset4` 默认值为 550 / 940 / 550 / 870 mm。最低高度只约束档位输入，
  不触发下行 STOP。
- [x] 全局童锁和 REST / Bluetooth / Panel 来源权限已经进入统一 `desk_core` 策略。
- [x] Web 可显示固件构建时间，并可请求 ESP32 重启。
- [x] Phase 2 双 RJ45 原厂面板短行程、断线 STOP、仲裁和童锁真屏蔽已在真桌通过。
- [x] 异常停止矩阵已在真桌通过：松手、显式 STOP、断连、重启、掉电和来源关闭都会停桌。
- [x] 双 ToF 档位 550 / 870 mm、最高高度 940 mm 和低位右侧障碍物保护已在真桌通过。

### 2.2 BLE、手机和桌面自动化

- [x] NimBLE GATT Server、加密 Write、状态 Notify、HOLD 短租约和断连停止已实现。
- [x] LightBlue 历史上已完成连接、状态读取和升降/STOP 真机操作；当前固件已恢复闭环档位。
- [x] React Native + Expo Development Build 的 iPhone App 已完成 Home / Settings 正式 UI。
- [x] iPhone App 已通过 BLE 控制真实升降桌，长按升降与松手 STOP 可用。
- [x] 手机 App 已实现 BLE 优先、REST 回退、统一状态和设备配置读写。
- [x] 手机 App 已支持长按升降、STOP、童锁、配置同步、档位和震动反馈。
- [x] 手机 App 已通过 TypeScript、55 项主机测试和通用 iOS 无签名构建门禁。
- [x] Android 真机已完成扫描、配对、Notify、Write 和异常停止验收。
- [x] Apple Watch 真机已完成扫描、配对、Crown 连续运动、反向和 STOP。
- [x] iPhone、Apple Watch、Android 三台真机并发、运动所有权和 Bond 删除矩阵已通过。
- [x] Shell 脚本支持上升、下降、停止和档位命令。
- [x] GoatRemote 与 Karabiner 可复用同一 Shell 脚本。
- [x] Karabiner 旋钮连续升降与停转距离已通过真桌验收。
- [x] 小智 AI / 自动化专用的 `raise-to-max` 有界 REST 入口、Driver 能力门禁和状态字段已完成；
  不支持真实 ToF 闭环的 Driver 不会退化为普通持续上升。
- [x] Ulanzi D200H 插件源码已提供请坐、站立、番茄时刻三个键，共用 Desk Gateway status 轮询。

## 3. 已实现但仍需验收

| 优先级 | 项目 | 当前缺口 | 完成条件 |
|---|---|---|---|
| P1 | BLE / Wi-Fi 自动回退 | BLE 和指定 IP 的 REST 基本路径已使用；超距自动回退和恢复时序未完整验收 | 不重放运动命令，切换后状态和配置一致 |
| P1 | iOS 连接稳定性 | 已完成日常控制和三客户端并发；连续 20 次连接/断开及权限异常矩阵未单独记录 | 无需重启手机或 ESP32 即可恢复 |
| P1 | 自定义档位与 B12 恢复 | 自定义档位、跨端同步、故障建议和 REST/BLE 控制盒重置均已实现 | 真桌验证档位增删改、重启持久化、8 秒重置、提前 STOP 和边界无误报 |
| P1 | OLED 状态显示 | SSD1306 已随当前固件启动，页面、运动态和传感器离线逻辑已实现 | 完成显示一致性、轮播、掉线降级和 30 分钟共存稳定性验收 |
| P1 | 番茄语音提醒 | 本地状态机、I2S、SPIFFS、REST/Web 和语音资源已实现并通过自动化 | 完成功放/扬声器接线、清晰度、爆音、发热、EMI、计时和运动并发真机验收 |
| P1 | 小智 AI 有界最高位控制 | Desk Gateway 固件、REST 契约和小智云 MCP 桥接代码已完成并通过 Mock/MCP 工具注册检查；云端 Endpoint 注册、常驻部署和真桌语音链路尚未执行 | 先用 REST 验证支持字段、幂等触顶、传感器失效和 STOP，再部署桥接并完成云端工具与真桌语音验收 |
| P1 | 移动端发布 | 当前为 Development Build | 完成 TestFlight、Android 内测、签名和发布说明 |

## 4. 尚未实现或未交付

### P1：发布交付

1. TestFlight 与 Android 内测包、正式签名和发布说明。
2. 小智 MCP Bridge 常驻部署、云端 MCP Endpoint 工具注册和小智硬件端到端验收。

BLE/Wi-Fi 超距回退、自定义档位/B12、OLED 显示稳定性和番茄语音的主体代码已经存在，
只是缺少第 3 节列出的剩余真机或发布验收，不再归类为“尚未实现”。

### P3：V1 后续生态和扩展能力

1. MQTT + Home Assistant。
2. Matter、Siri / App Intent 和米家/华为生态接入。
3. OTA 固件升级。
4. Loctek、Jiecang 等其他桌型 Driver。
5. 原厂档位 2 / 3 协议逆向与实现。
6. 原厂“上+下约 5 秒重置”键码抓包与原按键语义复现；当前 `DR=0x7F` 等价重置入口已经实现，但不等同于完成原按键协议逆向。
7. BLE OLED / 独立旋钮参考配件。

## 5. 推荐执行顺序

Phase 1 多入口控桌、Phase 2 透传和 P0 真机安全矩阵已经可用。下面是剩余 V1 工作：

1. 补齐 BLE/Wi-Fi 超距回退、自定义档位/B12 和 OLED 显示稳定性（若纳入发布说明）。
2. 决定番茄语音和小智端到端是否纳入 V1，纳入则按对应清单验收。
3. 制作移动端内测包；P3 能力单独立项。

## 6. 文档维护规则

- 新功能只有在代码完成且核心真机路径通过后，才能从“待验收”移动到“已完成”。
- 仅通过编译、单元测试或模拟器不能替代桌子、手机或传感器真机验收。
- 任务优先级发生变化时先更新本文，再同步 README、[文档索引](../README.md) 和架构总览摘要。
- 详细测试证据写入 `guides/bringup-checklist.md`，本文只保留结论和下一步。
- 使用方式和 REST 契约分别维护在 `guides/control-methods.md` 与 `guides/rest-api.md`。
- V1 发布只以 `status/v1-release-acceptance.md` 的必选门禁和最终签署为准。
