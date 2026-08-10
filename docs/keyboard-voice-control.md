# 键盘与语音档位控制

当前集成只做两件事：切换档位 1 和档位 4。GoatRemote 与 Karabiner
共用 [`scripts/desk-preset.sh`](../scripts/desk-preset.sh)，不需要先调用登录接口。

脚本已经直接配置当前局域网参数：

```sh
DESK_BASE_URL='http://192.168.21.90'
DESK_KEY='desk-gateway'
```

如果 ESP32 地址或 Web 登录密码发生变化，直接修改这两个值。

## 直接测试

烧录支持 `X-Desk-Key` 的新固件后运行：

```bash
./scripts/desk-preset.sh 1
./scripts/desk-preset.sh 4
```

成功时接口返回：

```json
{"ok":true,"err":"ESP_OK"}
```

## GoatRemote

添加两个 Custom command example，Action 均选择 `shell`：

| When I say | Shell command |
|---|---|
| 桌子坐姿 | `/Users/dong4j/Developer/1.AI/ai-incubator/desk-gateway/scripts/desk-preset.sh 1` |
| 桌子站姿 | `/Users/dong4j/Developer/1.AI/ai-incubator/desk-gateway/scripts/desk-preset.sh 4` |

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

旋钮持续旋转时会重复发送快捷键。第一次事件只让固件进入待命，`350 ms`
内收到第二个同方向事件才开始运动，因此单次轻拨不会让桌子窜动。运动后
每个事件刷新 `200 ms` 租约；停止旋转、脚本退出或网络中断后，ESP32
都会自动发送停止指令，不会继续运行到普通的 15 秒运动超时。

| 旋钮动作 | 快捷键 | 脚本命令 |
|---|---|---|
| 顺时针 | `F18` | `./scripts/desk-preset.sh up` |
| 逆时针 | `F17` | `./scripts/desk-preset.sh down` |
| 按下 | `F16` | `./scripts/desk-preset.sh stop` |

安装或更新 Karabiner 配置后，在 **Complex Modifications** 中启用
“F17/F18 旋转升降，F16 立即停止”。旋钮本身配置为重复发送上述按键，
无需模拟按住和松开事件。
