/**
 * @file desk_audio_wav.h
 * @brief 16 kHz / 16-bit / Mono PCM WAV 的无平台解析与音量处理。
 *
 * 本文件刻意不依赖 ESP-IDF，构建脚本可在 Host tests 中先拦截损坏资源，
 * 避免把格式错误留到烧录后才发现。
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DESK_AUDIO_WAV_OK = 0,
    DESK_AUDIO_WAV_INVALID_ARGUMENT,
    DESK_AUDIO_WAV_TRUNCATED,
    DESK_AUDIO_WAV_UNSUPPORTED_FORMAT,
    DESK_AUDIO_WAV_MISSING_DATA,
} desk_audio_wav_result_t;

typedef struct {
    size_t data_offset;
    uint32_t data_size;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
} desk_audio_wav_info_t;

/** 解析 RIFF chunks，并且只接受首版固件唯一支持的 PCM 格式。 */
desk_audio_wav_result_t desk_audio_wav_parse(const uint8_t *bytes, size_t size,
                                              desk_audio_wav_info_t *out_info);

/**
 * 原地缩放 signed 16-bit PCM。
 * volume_percent 会被限制在 0..100，使用 32-bit 中间值避免溢出。
 */
void desk_audio_scale_pcm16(int16_t *samples, size_t sample_count,
                            unsigned volume_percent);

#ifdef __cplusplus
}
#endif
