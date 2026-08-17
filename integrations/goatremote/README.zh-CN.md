# Desk Gateway GoatRemote

**语言：** [English](README.md) · 简体中文

GoatRemote 的坐姿 / 站姿语音快捷方式，和 Karabiner 一样调用 [`scripts/desk-preset.sh`](../../scripts/desk-preset.sh)，没有另一套运动协议。

`prompt_template_completion_xml_desk.txt` 是给 GoatRemote 的补充系统提示词，要求把注入的自定义命令当权威映射，不要再发明第二条 shell 路径。

用法细节见 [`docs/guides/keyboard-voice-control.md`](../../docs/guides/keyboard-voice-control.md)。上级目录见 [`../README.zh-CN.md`](../README.zh-CN.md)。

## 自定义命令

添加两个 Custom command example，Action 选 `shell`（Terminal 以外 GoatRemote 会包成 `osascript` / `do shell script`）：

| When I say | Shell command |
|---|---|
| 桌子坐姿 | `<仓库根目录>/scripts/desk-preset.sh 1` |
| 桌子站姿 | `<仓库根目录>/scripts/desk-preset.sh 4` |

把 `<仓库根目录>` 换成这台机器的绝对路径。先改脚本里的 `DESK_BASE_URL` 和 `DESK_KEY`。

不要加「升到 N mm」这种自由高度命令。高度闭环和 ToF 策略留在网关侧。
