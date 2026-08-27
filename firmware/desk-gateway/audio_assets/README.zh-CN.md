# Desk Gateway 本地语音包

**语言：** [English](./README.md) · 简体中文

目录 `zh-CN-default/` 会在构建时打包为独立 `audio` SPIFFS 分区。完整执行
`idf.py flash` 会同时烧录应用和语音镜像；只烧录 `app` 不会更新语音资源。

固定格式：WAV、PCM signed 16-bit little-endian、16 kHz、Mono。

资源 ID：

- `focus_done`：专注结束语音。
- `break_done`：休息结束语音。
- `snooze_done`：延后结束语音。
- `attention_chime`：短提示音，响度与本地语音接近。
- `motion_up` / `motion_down`：持续运动开始。
- `preset1_*` / `preset4_*` / `height_preset_*`：内置和自定义档位开始、到达。
- `raise_to_max_start` / `max_height_reached`：有界最高位开始、到达。
- `right_gap_blocked` / `sensor_unavailable`：上升保护原因。
- `child_lock_*` / `preset_saved` / `controller_reset_*`：控制状态变化。

构建前由 `scripts/check-audio-assets.sh` 检查格式与必需文件。中文语音可用
`scripts/generate-reminder-voices.sh` 以 `edge-tts` 女声重生成；设置
`DESK_TTS_ONLY_MISSING=1` 时只生成缺失文件。REST 试听仍只接受原有
`focus_done`、`break_done`、`snooze_done`、`attention_chime`，运动资源只能由
设备状态触发，不开放文件路径或上传入口。

授权与再分发限制见 [LICENSE.md](./LICENSE.md)。
