# Desk Gateway 本地语音包

目录 `zh-CN-default/` 会在构建时打包为独立 `audio` SPIFFS 分区。完整执行
`idf.py flash` 会同时烧录应用和语音镜像；只烧录 `app` 不会更新语音资源。

固定格式：WAV、PCM signed 16-bit little-endian、16 kHz、Mono。

资源 ID：

- `focus_done`：专注结束语音。
- `break_done`：休息结束语音。
- `snooze_done`：延后结束语音。
- `attention_chime`：柔和试听提示音。

构建前由 `scripts/check-audio-assets.sh` 检查格式与必需文件。固件只接受上述
资源 ID，不开放文件路径或上传入口。
