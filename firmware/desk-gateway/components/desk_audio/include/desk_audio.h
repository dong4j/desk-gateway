/**
 * @file desk_audio.h
 * @brief MAX98357A 本地语音播放服务的公开接口。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DESK_AUDIO_VOICE_PACK "zh-CN-default"

typedef enum {
    DESK_AUDIO_PROMPT_FOCUS_DONE = 0,
    DESK_AUDIO_PROMPT_BREAK_DONE,
    DESK_AUDIO_PROMPT_SNOOZE_DONE,
    DESK_AUDIO_PROMPT_ATTENTION_CHIME,
} desk_audio_prompt_t;

typedef enum {
    DESK_AUDIO_PRIORITY_PREVIEW = 0,
    DESK_AUDIO_PRIORITY_ALARM = 1,
} desk_audio_priority_t;

typedef struct {
    bool available;
    bool enabled;
    bool playing;
    uint8_t volume_percent;
    const char *current_prompt;
    const char *voice_pack;
    const char *last_error;
} desk_audio_snapshot_t;

/** 挂载 Audio Data 分区并创建唯一 I2S 播放任务。 */
esp_err_t desk_audio_init(void);

/** 按稳定资源 ID 播放；alarm 可抢占 preview。 */
esp_err_t desk_audio_play(desk_audio_prompt_t prompt,
                          desk_audio_priority_t priority);

/** 中止当前/排队播放并清空 DMA 中的旧声音。 */
esp_err_t desk_audio_stop(void);

/** 保存开关和音量；关闭或音量为 0 时立即停止。 */
esp_err_t desk_audio_set_config(bool enabled, uint8_t volume_percent);

desk_audio_snapshot_t desk_audio_snapshot(void);
const char *desk_audio_prompt_name(desk_audio_prompt_t prompt);
bool desk_audio_prompt_from_name(const char *name, desk_audio_prompt_t *out);

#ifdef __cplusplus
}
#endif
