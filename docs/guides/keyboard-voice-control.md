# 键盘、旋钮与语音控制

GoatRemote 与 Karabiner 共用 [`scripts/desk-preset.sh`](../../scripts/desk-preset.sh)，不需要先调用
登录接口。当前硬件 I²C 产品固件支持手动升、降、STOP 和档位 1/4。档位使用 TOF400C
高度闭环，并由最高高度和低位右侧间距策略统一保护；高度未知或不满足上升条件时不会盲目运动。

脚本已经直接配置局域网参数，换机器或改密码时改这两个值。完整对照表（脚本、Karabiner 路径、手机、Watch、小智、D200H）见 [本地多端部署清单](local-multi-client-setup.md)。

```sh
DESK_BASE_URL='http://<设备IP>'
DESK_KEY='<与 Web 登录密码一致>'
```

## 直接测试

烧录支持 `X-Desk-Key` 的新固件后运行：

```bash
./scripts/desk-preset.sh up
./scripts/desk-preset.sh down
./scripts/desk-preset.sh stop
```

`up` / `down` 是旋钮的单刻度 jog 请求，不等同于 Web 长按。单次调用只让固件
进入待命；连续调用会让真桌运动，应使用旋钮按照下文步骤验收。

成功时接口返回：

```json
{"ok":true,"err":"ESP_OK"}
```

## GoatRemote

添加两个 Custom command example，Action 均选择 `shell`：

| When I say | Shell command |
|---|---|
| 桌子坐姿 | `<仓库根目录>/scripts/desk-preset.sh 1` |
| 桌子站姿 | `<仓库根目录>/scripts/desk-preset.sh 4` |

## Karabiner-Elements

复制配置文件：

```bash
cp integrations/karabiner/desk-gateway.json \
  ~/.config/karabiner/assets/complex_modifications/desk-gateway.json
```

然后在 Karabiner-Elements 的 **Complex Modifications** 中启用
“⌃⌥⇧ + 1/2/3 控制 Desk Gateway 档位”。

| 快捷键 | 档位 |
|---|---|
| `Right Control + Right Option + Right Shift + 1` | 档位 1 |
| `Right Control + Right Option + Right Shift + 2` | 停止 |
| `Right Control + Right Option + Right Shift + 3` | 档位 4 |

## 旋钮连续升降

旋钮持续旋转时会重复发送快捷键。每个刻度只启动一次 Shell 脚本，
并直接发送一次 `/api/v1/desk/jog/up` 或 `/api/v1/desk/jog/down` REST 请求。

- 第一个刻度只待命，`700 ms` 内的第二个同方向刻度才启动。
- 连续旋转时，每个新刻度直接刷新 ESP32 的 `500 ms` 运动租约。
- 停止旋转后不再发送请求，ESP32 在最后一个刻度后约 `500 ms` 自动停止。
- 运动中突然反向旋转时，第一个反向刻度会让 ESP32 立即执行 STOP；
  `700 ms` 内的第二个反向刻度才启动新方向。
- Shell 端没有后台进程、进程锁或临时状态文件。

单次刻度不会让桌子窜动；停转、Shell 退出或网络中断后，桌子都不会落入
手动下降使用的 15 秒运动超时。

反向时的 STOP 在 ESP32 内部直接调用 `desk_core_stop()`，不需要 Shell 先发送
`/api/v1/desk/stop`。这样由固件根据真实运动方向停止，不依赖 Mac 保存方向状态。

| 旋钮动作 | 快捷键 | 脚本命令 |
|---|---|---|
| 顺时针 | `F18` | `./scripts/desk-preset.sh up` |
| 逆时针 | `F17` | `./scripts/desk-preset.sh down` |
| 按下 | `F16` | `./scripts/desk-preset.sh stop` |

安装或更新 Karabiner 配置后，在 **Complex Modifications** 中启用
“F17/F18 旋转升降，F16 立即停止”。旋钮本身配置为重复发送上述按键，
无需模拟按住和松开事件。
