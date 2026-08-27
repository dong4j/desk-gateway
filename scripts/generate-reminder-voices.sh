#!/usr/bin/env bash
# 用已安装的 edge-tts 女声重生成中文提醒与运动提示，再转成固件合同 WAV。
#
# attention_chime 是本项目正弦波，不走 TTS。输出必须是 16 kHz / 16-bit / mono PCM。
set -euo pipefail

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ASSET_DIR="${SCRIPT_DIR}/../firmware/desk-gateway/audio_assets/zh-CN-default"
VOICE="${DESK_TTS_VOICE:-zh-CN-XiaoxiaoNeural}"

EDGE_TTS=()
if command -v edge-tts >/dev/null; then
    EDGE_TTS=(edge-tts)
elif python3 -c 'import edge_tts' >/dev/null 2>&1; then
    EDGE_TTS=(python3 -m edge_tts)
elif [[ -x "${HOME}/Library/Python/3.9/bin/edge-tts" ]]; then
    EDGE_TTS=("${HOME}/Library/Python/3.9/bin/edge-tts")
else
    echo "error: edge-tts is not installed" >&2
    exit 1
fi
if ! command -v ffmpeg >/dev/null; then
    echo "error: ffmpeg is required to convert TTS output to 16 kHz mono PCM" >&2
    exit 1
fi

synthesize() {
    local id="$1"
    local text="$2"
    local tmp
    if [[ "${DESK_TTS_ONLY_MISSING:-0}" == "1" &&
          -f "${ASSET_DIR}/${id}.wav" ]]; then
        return
    fi
    tmp="$(mktemp -t desk-tts.XXXXXX.mp3)"
    "${EDGE_TTS[@]}" --voice "${VOICE}" --text "${text}" --write-media "${tmp}"
    ffmpeg -y -hide_banner -loglevel error \
        -i "${tmp}" -acodec pcm_s16le -ac 1 -ar 16000 \
        "${ASSET_DIR}/${id}.wav"
    rm -f "${tmp}"
}

synthesize focus_done "专注时间结束啦，起来活动一下吧。"
synthesize break_done "休息时间结束，准备开始下一轮专注。"
synthesize snooze_done "休息提醒到了，记得起来走一走。"
synthesize motion_up "桌面正在上升。"
synthesize motion_down "桌面正在下降。"
synthesize preset1_start "正在调整到坐姿高度。"
synthesize preset1_reached "已到达坐姿高度。"
synthesize preset4_start "正在调整到站姿高度。"
synthesize preset4_reached "已到达站姿高度。"
synthesize height_preset_start "正在调整桌面高度。"
synthesize height_preset_reached "已到达目标高度。"
synthesize raise_to_max_start "正在上升到最高高度。"
synthesize max_height_reached "已到达最高安全高度。"
synthesize right_gap_blocked "右侧空间不足，无法上升。"
synthesize sensor_unavailable "测距异常，无法上升。"
synthesize child_lock_enabled "童锁已开启。"
synthesize child_lock_disabled "童锁已关闭。"
synthesize preset_saved "档位已保存。"
synthesize controller_reset_started "正在重置控制盒。"
synthesize controller_reset_completed "控制盒重置完成。"

echo "local voices: ${VOICE}"
"${SCRIPT_DIR}/check-audio-assets.sh"
