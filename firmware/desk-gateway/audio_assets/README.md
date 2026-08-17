# Desk Gateway local voice pack

**Language:** English · [简体中文](./README.zh-CN.md)

`zh-CN-default/` is packed into a separate `audio` SPIFFS partition at build time. A full `idf.py flash` writes both the app and the voice image. Flashing `app` alone does not update the voice assets.

Fixed format: WAV, PCM signed 16-bit little-endian, 16 kHz, Mono.

Asset IDs:

- `focus_done`: focus session finished.
- `break_done`: break finished.
- `snooze_done`: snooze finished.
- `attention_chime`: quiet preview chime.

`scripts/check-audio-assets.sh` checks format and required files before build. Firmware only accepts these IDs. There is no file-path or upload API.

Licensing and redistribution limits: [LICENSE.md](./LICENSE.md).
