#!/usr/bin/env bash
# 校验音频分区的固定资源与 WAV 合同，避免格式错误进入烧录镜像。
#
# 不用 `file`(libmagic)：espressif/idf 容器没有这个命令，CI 会直接 127 退出。
# Python 标准库 wave 在本机和 IDF 镜像里都有，读到的就是固件接受的 PCM 参数。
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ASSET_DIR="${SCRIPT_DIR}/../firmware/desk-gateway/audio_assets/zh-CN-default"
REQUIRED=(focus_done break_done snooze_done attention_chime)

if command -v python3 >/dev/null 2>&1; then
    PYTHON=(python3)
elif command -v python >/dev/null 2>&1; then
    PYTHON=(python)
else
    echo "error: python3 is required to inspect WAV headers" >&2
    exit 1
fi

# 固件 desk_audio_wav_parse 只接受 16 kHz / 16-bit / mono PCM。
inspect_wav() {
    local wav_path="$1"
    "${PYTHON[@]}" - "${wav_path}" <<'PY'
import sys
import wave

path = sys.argv[1]
try:
    with wave.open(path, "rb") as wav:
        channels = wav.getnchannels()
        sample_width = wav.getsampwidth()
        rate = wav.getframerate()
        comptype = wav.getcomptype()
except wave.Error as exc:
    print(f"error: invalid WAV format: {path}: {exc}", file=sys.stderr)
    sys.exit(1)

if channels != 1 or sample_width != 2 or rate != 16000 or comptype != "NONE":
    print(
        f"error: invalid WAV format: {path}: "
        f"channels={channels} width={sample_width} rate={rate} comptype={comptype}",
        file=sys.stderr,
    )
    sys.exit(1)
PY
}

for prompt in "${REQUIRED[@]}"; do
    wav_path="${ASSET_DIR}/${prompt}.wav"
    if [[ ! -f "${wav_path}" ]]; then
        echo "error: missing audio asset: ${wav_path}" >&2
        exit 1
    fi
    inspect_wav "${wav_path}"
done

echo "audio assets: ok"
