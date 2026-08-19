# 本地多端部署清单

| 项 | 内容 |
|---|---|
| 日期 | 2026-08-19 |
| 目的 | 在自己的局域网把 Desk Gateway 烧上、配上网，并让 Web / 脚本 / 手机 / Watch / 键盘 / 语音 / D200H 都能控同一张桌子 |
| 详细用法 | [多种方式控制升降桌](control-methods.md) |
| REST 契约 | [REST API](rest-api.md) |
| 接线与排障 | [真机验收清单](bringup-checklist.md) |

本文只回答一件事：**克隆仓库之后，要改哪些 IP、密码和路径，才能在本地真正控桌。**  
接线细节、BLE 字节协议、各端 UI 说明仍看上表链接，不在这里重复。

测试运动时必须有人站在桌旁，随时可以发 STOP 或切断控制盒电源。所有 HTTP 入口只走局域网，不要做公网端口映射。

**语言：** English 见 [local-multi-client-setup.en.md](local-multi-client-setup.en.md)。

---

## 1. 先填三个值

后面每一端都用这三项。先在纸上或备忘录里写下来：

| 名称 | 你填什么 | 仓库里当前的例子（不要照抄） |
|---|---|---|
| 设备地址 | `http://<设备IP>`，或能解析的 `http://desk-gateway.local` | 脚本里是 `http://192.168.21.65` |
| REST / Web 密码 | 当前 Web 登录密码，也是 `X-Desk-Key` | 出厂默认 `desk-gateway`；脚本里现在是 `1024` |
| 仓库根目录 | 这台电脑上本仓库的**绝对路径** | `/Users/dong4j/Developer/1.AI/ai-incubator/desk-gateway` |

怎么拿到设备 IP：

1. 烧录后看串口日志，或路由器 DHCP 列表。
2. 固件会发 mDNS：`http://desk-gateway.local/`。电脑或手机解析失败时，改用 DHCP IP。
3. IP 是 DHCP 分配的，路由器重启后可能变。变了就要把下面所有 REST 客户端一起改。

密码规则：

- SoftAP / 首次 Web 登录默认都是 `desk-gateway`。
- 登录后应在 Web 设置里改掉。
- **改密之后**，脚本、`X-Desk-Key`、手机、Watch、小智 `.env`、D200H 必须改成**同一个新密码**。
- `scripts/desk-preset.sh` 不会自动跟着 Web 改密；它只认文件里写死的 `DESK_KEY`。

---

## 2. 固件上电（只做一次）

1. 按 [固件 README](../../firmware/desk-gateway/README.zh-CN.md) 接线。Phase 1 至少接控制盒 CLK/DAT 到 GPIO4/5，红线 3.3V **不要**接到 ESP32 `3V3`。
2. 本机安装 [ESP-IDF v6.0.2](https://docs.espressif.com/projects/esp-idf/)，目标芯片 `esp32s3`。不要混用其他 IDF 版本。
3. 完整烧录（含语音分区，保留 NVS）：

```bash
./scripts/flash-firmware.sh /dev/cu.usbmodemXXXX
```

没有这条包装脚本时，先激活 IDF 再 `cd firmware/desk-gateway && idf.py -p 串口 flash monitor`。只跑 `app-flash` 不会更新 `audio.bin`。

4. 无 Wi-Fi 凭证时设备开热点：

| 项 | 值 |
|---|---|
| SSID | `DeskGateway` |
| 密码 | `desk-gateway` |
| 配网页 | http://192.168.4.1/ |

配家里的 **2.4 GHz** Wi-Fi。配上网后浏览器打开 `http://<设备IP>/`，用默认密码 `desk-gateway` 登录，然后改密。

5. 串口先确认能停、能升、能降（整行输入再回车）：`stop`、`up`、`down`。Web 升/降是**按住运动、松手停止**。
6. Web 设置里确认：童锁关闭；`REST` / `Bluetooth` / `Panel` 来源按你要用的入口打开。关掉某个来源会先停桌。

到这里，局域网 Web 已经能控桌。下面把其他入口指到**同一台设备、同一套密码**。

---

## 3. 配置对照表（按这个改）

把第 1 节的三个值填进下表对应位置。没打算用的端可以跳过，但 **REST 脚本是 Karabiner / 旋钮 / GoatRemote 的共用入口，用其中任一端都必须改脚本**。

| 端 | 改哪里 | 改什么 | 必须等于 |
|---|---|---|---|
| 共用脚本 | [`scripts/desk-preset.sh`](../../scripts/desk-preset.sh) 第 8–9 行 | `DESK_BASE_URL`、`DESK_KEY` | `http://<设备IP>`、当前 Web 密码 |
| Karabiner | [`integrations/karabiner/desk-gateway.json`](../../integrations/karabiner/desk-gateway.json) | 全部 `shell_command`（6 处）里的仓库路径 | `<仓库根目录>/scripts/desk-preset.sh …` |
| GoatRemote 命令 | GoatRemote 自定义命令 | 两条 `shell` 命令的路径 | 同上脚本的 `1` / `4` |
| GoatRemote 提示词 | [`integrations/goatremote/prompt_template_completion_xml_desk.txt`](../../integrations/goatremote/prompt_template_completion_xml_desk.txt) | 文中示例绝对路径 | 改成你的仓库路径后再导入 |
| 手机 App | App → 设置 | REST 地址、`X-Desk-Key` | IP 或 `desk-gateway.local`、Web 密码 |
| Apple Watch | 连接设置 → Wi-Fi | 网关地址、REST 密码 | 同上；密码进 Watch Keychain |
| 小智 MCP | [`integrations/xiaozhi-mcp/.env`](../../integrations/xiaozhi-mcp/.env.example) | `DESK_GATEWAY_URL`、`DESK_GATEWAY_KEY`，以及 MCP 管道路径 | URL/Key 与 Web 一致；`.env` 不要提交 |
| 小智常驻 | `launchd` plist | `__PROJECT_ROOT__`、`__HOME__` | 本机绝对路径 |
| Ulanzi D200H | 任意按键的属性面板 | 网关地址、`X-Desk-Key` | 三个键共享；不写进源码 |
| BLE 手机 / Watch / LightBlue | Web 或 App 设置 | 打开 120 秒配对窗口 | 广播名 `DeskGateway`；不填 IP 也能控桌 |
| curl / 自己的脚本 | 请求头 | `X-Desk-Key` | 当前 Web 密码 |

仓库里的 IP、密码、绝对路径是维护者局域网的可用值，**克隆后必须改成你的**。不是安全脱敏问题，是 DHCP 和路径在每台电脑上都不一样。

---

## 4. 按端落地

### 4.1 共用脚本（必做，只要用键盘 / 旋钮 / 语音）

编辑 `scripts/desk-preset.sh`：

```sh
DESK_BASE_URL='http://<设备IP>'
DESK_KEY='<当前 Web 密码>'
```

先验证 REST，再去接 Karabiner：

```bash
curl -s -H "X-Desk-Key: <当前 Web 密码>" "http://<设备IP>/api/v1/desk/status"
./scripts/desk-preset.sh stop
```

`status` 里 `height_known` / `tof_height_known` 应为 `true`，`child_lock` 应为 `false`，`control_sources.rest` 应为 `true`。再试档位（桌旁有人）：

```bash
./scripts/desk-preset.sh 1    # 坐姿 550 mm
./scripts/desk-preset.sh 4    # 站姿 870 mm
./scripts/desk-preset.sh stop
```

`up` / `down` 是旋钮 jog，不是 Web 长按；单次调用往往只待命，连续刻度才会明显运动。

### 4.2 Karabiner 快捷键和旋钮

1. 先完成 4.1。Karabiner 只负责按键，真正的 IP/密码在脚本里。
2. 把 JSON 里 6 处 `/Users/dong4j/Developer/1.AI/ai-incubator/desk-gateway/scripts/desk-preset.sh` 换成 `<仓库根目录>/scripts/desk-preset.sh`。
3. 复制到 Karabiner：

```bash
cp integrations/karabiner/desk-gateway.json \
  ~/.config/karabiner/assets/complex_modifications/desk-gateway.json
```

4. 在 **Complex Modifications** 里启用两组规则：档位 `⌃⌥⇧ + 1/2/3`，以及 `F18`/`F17`/`F16` 旋钮升/降/停。
5. 旋钮本身配置为重复发送这些键，不要模拟按住。

细节：[键盘、旋钮与语音控制](keyboard-voice-control.md)、[Karabiner README](../../integrations/karabiner/README.zh-CN.md)。

### 4.3 GoatRemote

1. 先完成 4.1。
2. 两条自定义命令，Action 选 `shell`：

| When I say | 命令 |
|---|---|
| 桌子坐姿 | `<仓库根目录>/scripts/desk-preset.sh 1` |
| 桌子站姿 | `<仓库根目录>/scripts/desk-preset.sh 4` |

3. 若使用仓库里的提示词模板，把文件中的绝对路径改成你的仓库路径再导入。

### 4.4 局域网 Web

浏览器打开 `http://<设备IP>/` 或 `http://desk-gateway.local/`。升/降按住，松手停止。设置页改高度、童锁、来源权限、Bond。登录用的密码就是所有 REST 客户端的 Key。

### 4.5 iPhone / Android

1. 不能用 Expo Go。按 [iOS 真机部署](mobile-ios-device-deployment.md) 或 [Android 真机部署](mobile-android-device-deployment.md) 装 Development Build。
2. 手机和网关同一局域网（REST 回退和 Bond 管理需要）。
3. 设置里填写 REST 地址和 `X-Desk-Key`（当前 Web 密码）。自动模式优先 BLE，BLE 失败才走 REST。
4. 首次 BLE 配对：在已登录 Web 或已有 App 里打开 **120 秒配对窗口**，扫描 `DeskGateway`，允许系统配对。iPhone 写 Client Info `01 02`，Android 写 `01 03`，不要用 STOP 当握手。
5. 长按升降、松手 STOP。Bond 删除、配对窗口、番茄时长等管理动作走 REST，所以即使平时用 BLE 控桌，设置里的 IP 和密码仍要正确。

### 4.6 Apple Watch

1. 按 [Watch README](../../mobile/watch/README.zh-CN.md) 签名安装到真机，不要用模拟器 Mock 当验收。
2. BLE：同样先开 120 秒配对窗口；Watch 写 Client Info `01 01`。
3. 若用 Wi-Fi：Watch 能访问网关所在局域网，连接设置里填地址和 REST 密码。Crown 走 jog 短租约。
4. 三台同时在线时，非所有者会看到「另一台设备正在控制」，STOP 仍然有效。

### 4.7 小智 AI（可选）

桥接代码在 `integrations/xiaozhi-mcp/`。Desk Gateway **不需要公网地址**。

```bash
cp integrations/xiaozhi-mcp/.env.example integrations/xiaozhi-mcp/.env
chmod 600 integrations/xiaozhi-mcp/.env
```

至少改：

| 变量 | 填什么 |
|---|---|
| `MCP_ENDPOINT` | 小智控制台里该智能体的完整 WebSocket 地址（含 token） |
| `MCP_PIPE_DIR` / `MCP_PYTHON` | 本机 `78/mcp-calculator` 源码和 venv |
| `DESK_GATEWAY_URL` | `http://<设备IP>` |
| `DESK_GATEWAY_KEY` | 当前 Web 密码 |

运动前确认 `GET /api/v1/desk/status`：`height_known`、`tof_height_known`、`raise_to_max_supported` 为真，童锁关，REST 来源开。五个工具是固定的，不要自造「升到 N mm」。

常驻的话，复制 `launchd` 模板，替换 `__PROJECT_ROOT__` 和 `__HOME__`。步骤见 [小智 MCP README](../../integrations/xiaozhi-mcp/README.zh-CN.md)。

### 4.8 Ulanzi D200H（可选）

源码不能直接丢进 UlanziStudio，要按 [插件 README](../../integrations/ulanzi-d200h/README.zh-CN.md) 用官方 SDK 编译。安装后在属性面板填网关地址和 `X-Desk-Key`。三个键共享这份配置。电脑、UlanziStudio 和网关必须能互通，UlanziStudio 保持运行。

### 4.9 原厂面板（Phase 2）

接线见固件 README 的双 RJ45 表：控制盒 GPIO4/5，面板 GPIO6/7，**不要**把两边 CLK/DAT 短接。面板键走同一套童锁和仲裁；档位键 2 / 3 仍不是已验收路径。

### 4.10 Home Assistant / MQTT（可选）

操作说明见 [用 Home Assistant 控制升降桌](home-assistant-mqtt.md)。在 Web 设置页填写局域网 Broker（推荐 HA Mosquitto）。MQTT 用户不要复用 Web 密码，也不要把 Broker 或 1883/8883 映射到公网。默认不连接、也不允许 MQTT 控桌。打开 Client 后 HA 应能 Discovery 到 Cover；打开「允许 MQTT 控制桌子」后，Cover 打开/关闭/停止对应起立/请坐/STOP。

---

## 5. 一次跑通验收

有人在桌旁时按顺序勾：

- [ ] `GET /api/v1/desk/status` 返回当前高度，且 `child_lock=false`、`control_sources.rest=true`
- [ ] Web 按住上升/下降，松手立即停
- [ ] `./scripts/desk-preset.sh 1` / `4` / `stop` 与 Web 看到同一高度
- [ ] 改过 Karabiner 路径后，快捷键或旋钮能停、能档位（若启用）
- [ ] 手机或 Watch BLE 扫描到 `DeskGateway`，配对后长按/Crown 能停
- [ ] 童锁打开后，除 STOP 外各入口都不能再启动运动
- [ ] 未做公网端口映射

任一端 401，先对一下该端密码是不是已经改成当前 Web 密码。403 先看童锁和来源开关。

---

## 6. 改完仍不动时

| 现象 | 先查 |
|---|---|
| 脚本或 curl `401` | `DESK_KEY` / `X-Desk-Key` 不是当前 Web 密码 |
| `403` `child_lock` / `source_disabled` | Web 关掉童锁，打开对应来源 |
| `.local` 打不开 | 改用 DHCP IP，并同步改脚本、App、Watch、`.env`、D200H |
| Karabiner 没反应 | JSON 仍指向别人的仓库路径；Complex Modifications 未启用；脚本本身 REST 已失败 |
| 手机扫描不到 | 未开 120 秒窗口、已有 3 个 Central、权限/定位、Bond 冲突 |
| 「另一台设备正在控制」 | 当前端不是运动所有者；可 STOP，不要再抢 HOLD |
| 桌子升不起来 | ToF 高度未知、已到最高、或低于 800 mm 且右侧间距未知/过近；下降和 STOP 应仍可用 |
| DHCP 换 IP 后全家失效 | 把第 3 节所有 REST 地址改成新 IP |

集成总览：[integrations/README.zh-CN.md](../../integrations/README.zh-CN.md)。
