# Desk Gateway 真机接线、排障与验收清单

按顺序勾选；移动测试时必须有人在桌旁。2026-08-10 已验证补齐总线上拉后，Web 按住升/降
可以驱动真桌，松手可以停止，但这不代表档位、超时、童锁和真实高度已经通过。

## A. 软件（可不接线）

- [ ] `get-idf` → `idf.py build` / `flash` 成功（目标 esp32s3）
- [ ] 无凭证时出现 SoftAP：`DeskGateway` / `desk-gateway`
- [ ] 手机打开 `http://192.168.4.1/` 能配 2.4G WiFi
- [ ] Flash 后 NVS 保留，一般**不必**重配 WiFi
- [ ] 浏览器登录 Web（默认密码 `desk-gateway`）
- [ ] 按住升/降：状态 `moving_*`，松手回 `idle`；点「停」可停
- [ ] 有 **SIM 高度** 徽章时，高度数字随按住变化（演示用，非实测）
- [ ] 改密：设置里保存后，重新登录用新密码
- [ ] 断网提示：拔掉板子 USB 或断 WiFi 后，页面出现连接失败横幅

## B. 接线（yourdesk_v1 + RJ45）

电源与安全：

- [ ] ESP32 **仅 USB 供电**，桌子 3.3V **不接到** ESP32 供电
- [ ] 主机与 ESP32 **共地（GND）**
- [ ] 人在旁，首次升降行程短测

信号（面板模拟 / Phase 1，原厂面板断开）：

| RJ45 | 信号 | 连接 |
|------|------|------|
| pin 1 / 红线 | 控制盒 3.3V | 仅作为两个上拉电阻的电源端 |
| pin 2 / 白线 | CLK | GPIO4 |
| pin 3 / 绿线 | GND | ESP32 GND |
| pin 4 / 黑线 | DAT | GPIO5 |

原厂面板断电、完全断开后的实测值：

```text
红线 3.3V ↔ 白线 CLK = 1.99 kΩ
红线 3.3V ↔ 黑线 DAT = 1.99 kΩ
```

拔掉原厂面板也会拔掉这两只上拉。ESP32 替换面板时必须补回：

```text
红线 3.3V ── 2 kΩ（可用 2.2 kΩ）── 白线 CLK / GPIO4
红线 3.3V ── 2 kΩ（可用 2.2 kΩ）── 黑线 DAT / GPIO5
绿线 GND ─────────────────────────── ESP32 GND
```

两个电阻是并联到信号线的上拉，不是串联电阻；白线和黑线仍分别直接连接 GPIO4/GPIO5。
红线不得直接连接 ESP32 `3V3`，避免两路 3.3V 电源互相反灌。

### B.1 断电检查

- [ ] 红↔白约 `2 kΩ`
- [ ] 红↔黑约 `2 kΩ`
- [ ] 白↔黑约 `4 kΩ`
- [ ] 红↔绿不短路

### B.2 通电检查

- [x] 红↔绿约 `3.3V`
- [ ] 用逻辑分析仪确认 CLK/DAT 高电平、`0x24` 地址 ACK 和实际 `DR`

活动总线上万用表显示的是平均值。本次故障排查曾测得 `CLK≈2V`、`DAT≈1.4V`，只能说明线上
存在活动，不能据此认定上拉和 I²C 应答正常。

### B.3 动作验收

- [x] 上电后桌子静止（期望 DR 空闲 `0x2E`）
- [x] Web 按住升：串口见 `DR=0x47`，桌子上升；松手 `DR=0x2E` 停止
- [x] 按住降：`DR=0x4F`，下降；松手停止
- [ ] 约 15s 超时自动停（可不松手验证一次）
- [ ] 档位 1 / 4 goto 有协议动作（当前点击无效果，待排查保持时长与总线应答）
- [ ] 童锁开关写入 status，重启后 NVS 状态仍保留
- [ ] 真实高度来自 digit `0x34–0x37`，Web 不显示 `SIM`

2026-08-10 真机已经否决“硬件 `0x24` Slave + 同引脚 GPIO 边沿嗅探”：启用后控桌失效且没有
高度帧。稳定固件必须保持 `CONFIG_DESK_YOURDESK_HEIGHT_SNIFFER_EXPERIMENTAL=n`，启动应见：

```text
I (...) yourdesk_v1: experimental GPIO height sniffer disabled
```

此时 Web 若显示高度，必须同时显示 `SIM`；真实高度仍待软件多地址 I²C Slave。

Phase 1 原厂面板已拔掉，没有可被童锁屏蔽的面板；只能验证 Web/API 状态和 NVS 持久化。
“童锁 ON 后原厂面板不能控桌”必须等 Phase 2 MITM 才能验收。

## C. 已解决故障：DR 日志变化但桌子不动

| 项 | 记录 |
|---|---|
| 现象 | Web 操作时串口出现 `DR=0x47/0x4F`，桌子不动作 |
| 容易误判 | `DR` 日志只是 ESP32 本地状态变化，不能证明控制盒已轮询、ACK 或读到该字节 |
| 电压 | 红线 `3.3V`；CLK 约 `2V`；DAT 约 `1.4V`（活动总线平均值） |
| 关键测量 | 原厂面板 `3.3V↔CLK`、`3.3V↔DAT` 均为 `1.99 kΩ` |
| 根因 | 原厂面板拔掉后，两只总线上拉也被移除 |
| 解决 | 红线分别经 `2 kΩ` 上拉到白线 CLK、黑线 DAT |
| 结果 | 2026-08-10 Web 按住升/降及松手停止真机通过 |

若再次出现“日志变化但桌子不动”，按以下顺序排查：

1. 断电检查两只上拉的电阻值和红↔绿短路。
2. 确认 ESP32 已启动为 `I2C slave @0x24 SCL=4 SDA=5`，再给控制盒上电。
3. 逻辑分析仪接 `D0=CLK`、`D1=DAT`、`GND=绿线`，分析仪 VCC 不接。
4. 以 12 MHz 从 idle 开始采集，覆盖按住和松开；检查 `0x48/0x49`、ACK 和返回 `DR`。

### C.1 已解决故障：高度固件启动后循环重启、Web 不可用

| 项 | 记录 |
|---|---|
| 现象 | 启动到 Wi-Fi 初始化后反复打印 boot log，Web 始终无法访问 |
| Panic | `Cache disabled but cached memory region accessed`，PC 指向 `gpio_get_level` |
| 根因 | 高度嗅探 ISR 在 Wi-Fi/NVS 关闭 Flash cache 期间调用了未驻留 IRAM 的 GPIO 函数 |
| 解决 | `sdkconfig.defaults` 启用 `CONFIG_GPIO_CTRL_FUNC_IN_IRAM=y`；驱动增加编译期门禁 |
| 二次现象 | monitor 的 `Device not configured` 是芯片重启造成 USB 串口断开，不是独立故障 |
| 自动验证 | ELF 中 ISR 与 `gpio_get_level` 均位于 `0x4037xxxx` IRAM 地址段 |

如果再次见到同类 panic，不要只看 `IRAM_ATTR`：ISR 调用链上的函数也必须全部 IRAM-safe。

### C.2 已解决回归：高度实验启用后无法正常升降

| 项 | 记录 |
|---|---|
| 现象 | Web 可访问，但按住升/降无法稳定驱动桌子，高度仍为未知 |
| 根因 1 | SCL 每个上升沿和 SDA 边沿产生高频 GPIO ISR，与 GPIO4/5 上的硬件 I²C Slave 竞争 |
| 根因 2 | 控制盒很可能在 `0x34–0x37` 地址 NACK 后终止写入，纯监听无法得到段码 data |
| 结论 | 当前 GPIO 被动嗅探方案 **NO-GO**，不能进入稳定固件 |
| 恢复 | 实验开关默认关闭；不安装 GPIO ISR、不创建高度任务、Driver 不声明真实高度能力 |
| 后续 | 设计统一软件 I²C Slave，同时 ACK `0x24` 与 `0x34–0x37` |

## D. 验收通过后再排期

- [ ] 软件多地址 I²C Slave：同时处理键通道与 digit 高度通道
- [ ] Phase 2 双 RJ45 MITM
- [ ] BLE 配件 / Matter（按需）

## E. 配网备忘

| 方式 | 说明 |
|------|------|
| **SoftAP（主）** | 热点 `DeskGateway` → `http://192.168.4.1/` |
| 串口（辅） | `wifi <ssid> <pass>`；VS Code 监视器输入常不可靠 |

激活环境：`get-idf`（勿每次写长 source 路径）。
