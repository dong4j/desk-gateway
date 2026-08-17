# 多种方式控制升降桌

| 项 | 内容 |
|---|---|
| 日期 | 2026-08-17 |
| 适用阶段 | Phase 1 与 Phase 2 透传已完成；V1 发布还差内测包等 P1 门禁 |
| REST 细节 | [REST API](rest-api.md) |
| 本地把多端跑起来 | [本地多端部署清单](local-multi-client-setup.md) |
| 接线烧录 | [真机验收清单](bringup-checklist.md)、[固件 README](../../firmware/desk-gateway/README.zh-CN.md) |

Desk Gateway 把厂商协议收进 `mxtark` Driver。下面这些入口都走同一套 `desk_core`：急停、童锁、来源权限、ToF 上升保护对所有入口生效。

测试运动时必须有人站在桌旁，随时可以发 STOP 或切断控制盒电源。Web 和 REST 只用于局域网，不要做公网端口映射。

## 入口一览

| 入口 | 通道 | 适合做什么 | 详细文档 |
|---|---|---|---|
| 局域网 Web | REST | 日常升降、档位、童锁、设置 | 下文 |
| `desk-preset.sh` / curl | REST | 脚本、快捷键、旋钮 | [REST API](rest-api.md) |
| USB 串口 | UART | 烧录后立刻验证、排障 | 下文 |
| iPhone App | BLE 优先，REST 回退 | 随身控制、配对管理 | [手机 App](../../mobile/app/README.zh-CN.md) |
| Android App | 同上 | 扫描、配对、长按控制和异常停止 | [Android 部署](mobile-android-device-deployment.md) |
| Apple Watch | BLE 或 REST | Crown 连续升降 | [Watch README](../../mobile/watch/README.zh-CN.md) |
| LightBlue | BLE GATT | 协议调试 | [BLE Profile](../architecture/ble-accessory-profile.md) |
| Karabiner 快捷键 | REST | 键盘切坐姿 / 站姿 / 停止 | [键盘与旋钮](keyboard-voice-control.md) |
| 桌面旋钮 | REST jog | 手不离键微调高度 | [键盘与旋钮](keyboard-voice-control.md) |
| GoatRemote | REST 档位 | Mac 语音切坐站 | [键盘与旋钮](keyboard-voice-control.md) |
| 小智 AI | MCP → REST | 「站立」「坐姿」「停下」 | [小智控桌](xiaozhi-ai-desk-control.md) |
| Ulanzi D200H | REST | 实体键坐 / 站 / 番茄 | [D200H 插件](../../integrations/ulanzi-d200h/README.zh-CN.md) |
| 原厂面板 | I²C 代理 | 桌边按键（Phase 2） | [固件 README](../../firmware/desk-gateway/README.zh-CN.md) |
| OLED | I²C 显示 | 只看高度和状态，不控桌 | [OLED](../architecture/oled-status-display.md) |

默认档位：坐姿 `550 mm`（档位 1），站姿 `870 mm`（档位 4），最高安全高度 `940 mm`。高度来自 TOF400C，不是控制盒数码管。

## 共同规则

童锁开启后，除 STOP 和解锁外，所有入口都不能启动或维持运动。REST、Bluetooth、Panel 还可以各自关掉。关掉某个来源会先停止当前运动。STOP 始终放行。

上升还会被 ToF 拦住：高度未知、已经到最高高度，或者高度低于 `800 mm` 且右侧间距未知/小于 `80 mm` 时，固件拒绝上升。下降和 STOP 不受这条限制。

运动语义不要混用：

| 动作 | 行为 | 典型入口 |
|---|---|---|
| Hold 升 / 降 | 按住续期，松手或断连后停止 | Web、手机长按、串口 `up`/`down` |
| Jog 升 / 降 | 约 `500 ms` 短租约，停转后自动停 | 旋钮、Watch Crown |
| 档位 1 / 4 | ToF 闭环走到目标高度 | Web、App、脚本、语音、D200H |
| `raise-to-max` | 有界升到最高安全高度，不会退化成长按上升 | 小智「最高」 |
| STOP | 立即停止 | 所有入口 |

## 局域网 Web

固件连上 2.4 GHz Wi-Fi 后，浏览器打开 `http://<设备IP>/`。无 Wi-Fi 凭证时设备会开 SoftAP：

| 项 | 值 |
|---|---|
| SSID | `DeskGateway` |
| 密码 | `desk-gateway` |
| 配网页 | http://192.168.4.1/ |
| 默认 Web 密码 | `desk-gateway` |

首次登录后应改密码。页面上的升/降是按住运动、松手停止，不是单击一次走到底。坐姿、站姿走闭环档位。设置页可以改最低/最高高度、来源权限、童锁、自定义档位和 Bond。

## REST 与脚本

自动化、快捷键、旋钮都调用同一组 REST。脚本入口是 `scripts/desk-preset.sh`，使用前改文件里的 `DESK_BASE_URL` 和 `DESK_KEY`，使它们等于当前网关地址和 Web 密码：

```bash
./scripts/desk-preset.sh 1      # 坐姿
./scripts/desk-preset.sh 4      # 站姿
./scripts/desk-preset.sh stop
./scripts/desk-preset.sh up     # 旋钮单刻度，不是 Web 长按
./scripts/desk-preset.sh down
```

直接 curl 时带 `X-Desk-Key`：

```bash
curl -s -H "X-Desk-Key: $DESK_KEY" "http://$DESK_IP/api/v1/desk/status"
curl -s -X POST -H "X-Desk-Key: $DESK_KEY" \
  "http://$DESK_IP/api/v1/desk/preset/4/goto"
curl -s -X POST -H "X-Desk-Key: $DESK_KEY" \
  "http://$DESK_IP/api/v1/desk/stop"
```

完整路径、鉴权和错误码见 [REST API](rest-api.md)。

## USB 串口

`idf.py -p PORT monitor` 进入 REPL 后，整行输入再回车。输入法切到英文，避免单个字母被当成一行。

| 命令 | 作用 |
|---|---|
| `help` | 打印命令 |
| `status` | 运动状态、童锁、高度是否已知 |
| `up` / `down` | Hold 升 / 降 |
| `stop` 或 `idle` | 停止 |
| `p1` / `p4` | 档位 1 / 4 |
| `save1` / `save4` | 保存档位（若当前 Driver 支持） |
| `lock` / `unlock` | 童锁 |
| `wifi <ssid> <pass>` | 写 STA 凭证 |
| `wifi status` | 查看 IP |

日常配网优先用 SoftAP，串口 `wifi` 只作后备。

## 手机与手表

iPhone App 不能用 Expo Go，需要 Development Build。自动模式优先连广播名 `DeskGateway` 的 BLE；BLE 失败或断开后回退局域网 REST。设置页可改成仅 BLE 或仅 Wi-Fi，并填写 `X-Desk-Key`。

Home 页长按升降、松手 STOP，并显示 ToF 高度和童锁。另一台设备正在运动时，本机收到 Desk Busy `0x80`，连接保持，不能抢升降，STOP 仍然有效。

Android 与 iPhone 使用同一套代码，真机已完成扫描、配对、Notify、Write 和异常停止。Apple Watch 是独立 watchOS App，Crown 走 jog 短租约，反向或松手由固件停止。iPhone、Watch、Android 三台可以同时在线，同一时刻只有一个运动所有者，非所有者收到 Desk Busy，STOP 仍然有效。

安装步骤：

- [iOS 真机部署](mobile-ios-device-deployment.md)
- [Android 真机部署](mobile-android-device-deployment.md)
- [Watch README](../../mobile/watch/README.zh-CN.md)

用 LightBlue 验证 GATT 时，按 [BLE Accessory Profile](../architecture/ble-accessory-profile.md) 的 UUID 和加密 Write 流程操作。未打开配对窗口时，新设备不能下发运动命令。

## 键盘、旋钮与 Mac 语音

Karabiner-Elements 读取 `integrations/karabiner/desk-gateway.json`。启用后：

| 输入 | 效果 |
|---|---|
| 右 Control + 右 Option + 右 Shift + 1 / 2 / 3 | 档位 1 / 停止 / 档位 4 |
| F18 / F17 / F16 | 旋钮升 / 降 / 立即停止 |

旋钮每个刻度只发一次 `/api/v1/desk/jog/up|down`。第一个刻度待命，`700 ms` 内第二个同向刻度才启动；停转后 ESP32 约 `500 ms` 自动停止。反向第一个刻度是 STOP。

GoatRemote 把「桌子坐姿」「桌子站姿」指到 `scripts/desk-preset.sh 1` 和 `4`。安装细节见 [键盘、旋钮与语音控制](keyboard-voice-control.md)。

## 小智 AI 与 Ulanzi

小智硬件继续用官方固件。本仓库的 MCP 桥接只暴露五个固定工具：查状态、升到最高、坐姿、站姿、停止。工具不接受任意目标高度，也不把「最高」做成普通持续上升。部署见 [小智 MCP 桥接](../../integrations/xiaozhi-mcp/README.zh-CN.md)。

Ulanzi D200H 插件提供请坐、站立、番茄时刻三个键，共享一次 status 轮询。倒计时在 ESP32 上跑，插件不另起一套计时器。源码在 `integrations/ulanzi-d200h/`，不能直接复制进 UlanziStudio，需要按插件 README 借助官方 SDK 编译。

## 原厂面板与 OLED

Phase 2 双 RJ45 代理已经写进默认固件：控制盒仍走 GPIO4/5 硬件 Slave `@0x24`，原厂面板走 GPIO6/7。童锁 OFF 且 Panel 来源开启时，面板按键优先于 Web/BLE。拔掉右侧 RJ45 会立即停止。短行程、真屏蔽和断线 STOP 已在真桌通过。原厂面板档位 2 / 3 仍未做安全高度验证，不要当已验收功能。

OLED 与两颗 ToF 共用 GPIO10/11，只显示高度、侧距和运动状态。接线：

![YD-ESP32-S3 与 0.91 英寸 OLED 接线](../architecture/images/oled-wiring.png)
