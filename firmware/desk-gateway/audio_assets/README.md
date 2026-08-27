# Desk Gateway local voice pack

**Language:** English · [简体中文](./README.zh-CN.md)

`zh-CN-default/` is packed into a separate `audio` SPIFFS partition at build time. A full `idf.py flash` writes both the app and the voice image. Flashing `app` alone does not update the voice assets.

Fixed format: WAV, PCM signed 16-bit little-endian, 16 kHz, Mono.

Asset IDs:

- `focus_done`: focus session finished.
- `break_done`: break finished.
- `snooze_done`: snooze finished.
- `attention_chime`: short preview chime, loudness matched to the spoken prompts.
- `motion_up` / `motion_down`: continuous motion start.
- `preset1_*` / `preset4_*` / `height_preset_*`: built-in and custom preset start/arrival.
- `raise_to_max_start` / `max_height_reached`: bounded ceiling start/arrival.
- `right_gap_blocked` / `sensor_unavailable`: upward safety reasons.
- `child_lock_*` / `preset_saved` / `controller_reset_*`: control-state changes.

`scripts/check-audio-assets.sh` checks format and required files before build.
Regenerate spoken prompts with `scripts/generate-reminder-voices.sh`
(`edge-tts` female voice); set `DESK_TTS_ONLY_MISSING=1` to preserve existing
files. REST preview remains limited to `focus_done`, `break_done`, `snooze_done`,
and `attention_chime`; motion assets are device-triggered only. There is no
file-path or upload API.

Licensing and redistribution limits: [LICENSE.md](./LICENSE.md).
