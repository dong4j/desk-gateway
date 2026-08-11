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

旋钮持续旋转时会重复发送快捷键。脚本将离散事件合并到唯一后台续期器：

- 第一个刻度只待命，`600 ms` 内的第二个同方向刻度才启动。
- 启动后每 `150 ms` 续期一次，不再为每个物理刻度单独启动 `curl`。
- `600 ms` 没有新刻度时主动 STOP，旋钮慢速连续转动仍可保持运动。
- ESP32 保留 `500 ms` 租约；脚本崩溃或网络中断时自动停止。

因此旋钮停转的正常停止延迟约为 `600 ms`，异常断联的固件保护上限约为
`500 ms`，都不会落入普通控制使用的 15 秒运动超时。

| 旋钮动作 | 快捷键 | 脚本命令 |
|---|---|---|
| 顺时针 | `F18` | `./scripts/desk-preset.sh up` |
| 逆时针 | `F17` | `./scripts/desk-preset.sh down` |
| 按下 | `F16` | `./scripts/desk-preset.sh stop` |

安装或更新 Karabiner 配置后，在 **Complex Modifications** 中启用
“F17/F18 旋转升降，F16 立即停止”。旋钮本身配置为重复发送上述按键，
无需模拟按住和松开事件。
