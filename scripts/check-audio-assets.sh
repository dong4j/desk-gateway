#!/usr/bin/env bash
# 校验音频分区的固定资源与 WAV 合同，避免格式错误进入烧录镜像。
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ASSET_DIR="${SCRIPT_DIR}/../firmware/desk-gateway/audio_assets/zh-CN-default"
REQUIRED=(focus_done break_done snooze_done attention_chime)

for prompt in "${REQUIRED[@]}"; do
    file="${ASSET_DIR}/${prompt}.wav"
    if [[ ! -f "${file}" ]]; then
        echo "error: missing audio asset: ${file}" >&2
        exit 1
    fi
    description="$(file -b "${file}")"
    if [[ "${description}" != *"WAVE audio"* ||
          "${description}" != *"Microsoft PCM"* ||
          "${description}" != *"16 bit"* ||
          "${description}" != *"mono 16000 Hz"* ]]; then
        echo "error: invalid WAV format: ${file}: ${description}" >&2
        exit 1
    fi
done

echo "audio assets: ok"
