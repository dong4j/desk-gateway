# Desk Gateway 到货 / 真机验收清单

RJ45 模块到齐后按顺序勾选。当前无桌子时可先做「软件项」。

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

信号（面板模拟 / Phase1）：

| 主机侧 | ESP32 |
|--------|--------|
| GND | GND |
| CLK | GPIO4 |
| DAT | GPIO5 |

- [ ] 上电后桌子静止（期望 DR 空闲 `0x2E`）
- [ ] Web 按住升：串口见 `DR=0x47`，桌子上升；松手 `DR=0x2E` 停止
- [ ] 按住降：`DR=0x4F`，下降；松手停止
- [ ] 约 15s 超时自动停（可不松手验证一次）
- [ ] 档位 1 / 4 goto 有协议动作（行程以真桌为准）
- [ ] 童锁 ON：记录「原厂面板是否仍能控」——Phase1 可能仍能控，Phase2 MITM 才真屏蔽

## C. 验收通过后再排期

- [ ] 高度 digit 嗅探（关掉 `CONFIG_DESK_SIM_HEIGHT`）
- [ ] Phase 2 双 RJ45 MITM
- [ ] BLE 配件 / Matter（按需）

## 配网备忘

| 方式 | 说明 |
|------|------|
| **SoftAP（主）** | 热点 `DeskGateway` → `http://192.168.4.1/` |
| 串口（辅） | `wifi <ssid> <pass>`；VS Code 监视器输入常不可靠 |

激活环境：`get-idf`（勿每次写长 source 路径）。
