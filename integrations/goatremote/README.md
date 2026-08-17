# Desk Gateway GoatRemote

**Language:** English · [简体中文](./README.zh-CN.md)

GoatRemote spoken shortcuts for sit and stand. They run [`scripts/desk-preset.sh`](../../scripts/desk-preset.sh) the same way Karabiner does. There is no extra motion protocol.

`prompt_template_completion_xml_desk.txt` is extra system prompt text for GoatRemote. It tells the model to treat injected custom commands as authoritative and not to invent a second shell path.

Usage detail: [`docs/keyboard-voice-control.md`](../../docs/keyboard-voice-control.md). Parent index: [`../README.md`](../README.md).

## Custom commands

Add two Custom command examples. Action type is `shell` (GoatRemote will wrap it as `osascript` / `do shell script` outside Terminal):

| When I say | Shell command |
|---|---|
| 桌子坐姿 | `<repo-root>/scripts/desk-preset.sh 1` |
| 桌子站姿 | `<repo-root>/scripts/desk-preset.sh 4` |

Replace `<repo-root>` with this machine’s absolute path. Edit `DESK_BASE_URL` and `DESK_KEY` in the script first.

Do not add a free-form “move to N mm” command. Height closed-loop and ToF policy stay on the gateway.
