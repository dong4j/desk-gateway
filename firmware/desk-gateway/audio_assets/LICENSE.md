# Audio asset license

**Language:** English · [简体中文](#中文)

This directory is packed into the firmware `audio` SPIFFS partition.

## `attention_chime.wav`

Generated locally with FFmpeg as a sine-wave chime. Original to this project.
Licensed under the same MIT License as the rest of the repository ([LICENSE](../../../LICENSE)).

## `focus_done.wav`, `break_done.wav`, `snooze_done.wav`

Generated on macOS with `say -v Tingting` for development and hardware bring-up.
This repository **does not** claim extra redistribution rights in Apple’s system
voices or in the resulting TTS audio.

Do **not** treat these three files as cleared assets for a sold product or a
store-signed firmware image. Replace them with recordings you made or with
audio that has an explicit redistribution license, then update this file with
author, tool, date, and license text.

Format for every file: WAV, PCM signed 16-bit little-endian, 16 kHz, Mono.
`scripts/check-audio-assets.sh` enforces that contract.

---

## 中文

本目录会打进固件 `audio` SPIFFS 分区。

`attention_chime.wav` 用 FFmpeg 正弦波在本机生成，属于本项目原创，跟随仓库
[MIT License](../../../LICENSE)。

`focus_done.wav`、`break_done.wav`、`snooze_done.wav` 用 macOS `say -v Tingting`
生成，只作开发和真机联调占位。本仓库**不声明**对系统语音或 TTS 输出拥有额外再分发权。
销售硬件或分发正式固件前必须换成自录或已取得明确授权的素材，并在本文件补充作者、工具、日期和许可。

固定格式：WAV、PCM signed 16-bit little-endian、16 kHz、Mono。由
`scripts/check-audio-assets.sh` 检查。
