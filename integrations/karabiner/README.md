# Desk Gateway Karabiner-Elements

**Language:** English · [简体中文](README.zh-CN.md)

Complex Modifications that call [`scripts/desk-preset.sh`](../../scripts/desk-preset.sh). They do not log in first. Sit is preset 1, stop is `stop`, stand is preset 4. Knob ticks send jog up/down.

Usage detail: [`docs/guides/keyboard-voice-control.md`](../../docs/guides/keyboard-voice-control.md). Parent index: [`../README.md`](../README.md).

Edit `DESK_BASE_URL` and `DESK_KEY` inside `scripts/desk-preset.sh` before enabling the rules. The JSON currently embeds an absolute `shell_command` path; change it to this machine’s repo path after copying.

## Install

```bash
cp integrations/karabiner/desk-gateway.json \
  ~/.config/karabiner/assets/complex_modifications/desk-gateway.json
```

Then enable both rules in Karabiner-Elements → **Complex Modifications**.

## Preset shortcuts

| Shortcut | Action |
|---|---|
| `Right Control + Right Option + Right Shift + 1` | Preset 1 (sit) |
| `Right Control + Right Option + Right Shift + 2` | STOP |
| `Right Control + Right Option + Right Shift + 3` | Preset 4 (stand) |

## Knob

| Knob | Key | Script |
|---|---|---|
| Clockwise | `F18` | `./scripts/desk-preset.sh up` |
| Counter-clockwise | `F17` | `./scripts/desk-preset.sh down` |
| Press | `F16` | `./scripts/desk-preset.sh stop` |

Enable “F17/F18 旋转升降，F16 立即停止”. Configure the knob to **repeat** those keys. Do not emulate press-and-hold.

Each tick is one REST jog (`/api/v1/desk/jog/up|down`). The first tick arms; a second same-direction tick within `700 ms` starts motion. Later ticks refresh the ESP32 `500 ms` lease. Stop turning and the desk stops about `500 ms` after the last tick. Reverse: first opposite tick is STOP inside `desk_core`; the second opposite tick within `700 ms` starts the new direction. The Mac does not keep a direction lock file.
