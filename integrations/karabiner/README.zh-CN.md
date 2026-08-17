# Desk Gateway Karabiner-Elements

**语言：** [English](README.md) · 简体中文

这是调用 [`scripts/desk-preset.sh`](../../scripts/desk-preset.sh) 的 Complex Modifications，不需要先登录。坐姿是档位 1，停止是 `stop`，站姿是档位 4。旋钮刻度发送 jog 升/降。

用法细节见 [`docs/guides/keyboard-voice-control.md`](../../docs/guides/keyboard-voice-control.md)。上级目录见 [`../README.zh-CN.md`](../README.zh-CN.md)。

启用规则前先改 `scripts/desk-preset.sh` 里的 `DESK_BASE_URL` 和 `DESK_KEY`。JSON 里的 `shell_command` 目前是绝对路径，复制后改成这台机器上的仓库路径。

## 安装

```bash
cp integrations/karabiner/desk-gateway.json \
  ~/.config/karabiner/assets/complex_modifications/desk-gateway.json
```

然后在 Karabiner-Elements 的 **Complex Modifications** 里启用下面两组规则。

## 档位快捷键

| 快捷键 | 动作 |
|---|---|
| `Right Control + Right Option + Right Shift + 1` | 档位 1（坐姿） |
| `Right Control + Right Option + Right Shift + 2` | 停止 |
| `Right Control + Right Option + Right Shift + 3` | 档位 4（站姿） |

## 旋钮

| 旋钮 | 按键 | 脚本 |
|---|---|---|
| 顺时针 | `F18` | `./scripts/desk-preset.sh up` |
| 逆时针 | `F17` | `./scripts/desk-preset.sh down` |
| 按下 | `F16` | `./scripts/desk-preset.sh stop` |

启用 “F17/F18 旋转升降，F16 立即停止”。旋钮本身配置为重复发送这些键，不要模拟按住和松开。

每个刻度是一次 REST jog（`/api/v1/desk/jog/up|down`）。第一格只待命，`700 ms` 内同方向第二格才启动。之后每格刷新 ESP32 的 `500 ms` 租约。停转后大约 `500 ms` 自动停止。反向时第一格在 `desk_core` 内 STOP，`700 ms` 内第二格才开新方向。Mac 端不保存方向锁文件。
