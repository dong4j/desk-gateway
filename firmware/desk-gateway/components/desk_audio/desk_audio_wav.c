/**
 * @file desk_audio_wav.c
 * @brief Desk Gateway 固定格式 WAV 解析器。
 */
#include "desk_audio_wav.h"

#include <stdbool.h>
#include <string.h>

static uint16_t read_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

desk_audio_wav_result_t desk_audio_wav_parse(const uint8_t *bytes, size_t size,
                                              desk_audio_wav_info_t *out_info)
{
    if (!bytes || !out_info) {
        return DESK_AUDIO_WAV_INVALID_ARGUMENT;
    }
    if (size < 12) {
        return DESK_AUDIO_WAV_TRUNCATED;
    }
    if (memcmp(bytes, "RIFF", 4) != 0 || memcmp(bytes + 8, "WAVE", 4) != 0) {
        return DESK_AUDIO_WAV_UNSUPPORTED_FORMAT;
    }

    bool found_fmt = false;
    bool found_data = false;
    uint16_t audio_format = 0;
    desk_audio_wav_info_t info = {0};
    size_t offset = 12;
    while (offset + 8 <= size) {
        const uint8_t *chunk = bytes + offset;
        uint32_t chunk_size = read_le32(chunk + 4);
        size_t payload = offset + 8;
        if (chunk_size > size - payload) {
            return DESK_AUDIO_WAV_TRUNCATED;
        }
        if (memcmp(chunk, "fmt ", 4) == 0) {
            if (chunk_size < 16) {
                return DESK_AUDIO_WAV_TRUNCATED;
            }
            audio_format = read_le16(bytes + payload);
            info.channels = read_le16(bytes + payload + 2);
            info.sample_rate = read_le32(bytes + payload + 4);
            info.bits_per_sample = read_le16(bytes + payload + 14);
            found_fmt = true;
        } else if (memcmp(chunk, "data", 4) == 0) {
            info.data_offset = payload;
            info.data_size = chunk_size;
            found_data = true;
        }

        /* RIFF chunk payload 按偶数字节对齐，奇数长度后包含一个 pad byte。 */
        size_t advance = 8U + (size_t)chunk_size + (chunk_size & 1U);
        if (advance > size - offset) {
            return DESK_AUDIO_WAV_TRUNCATED;
        }
        offset += advance;
    }

    if (!found_fmt || !found_data) {
        return DESK_AUDIO_WAV_MISSING_DATA;
    }
    if (audio_format != 1 || info.channels != 1 ||
        info.sample_rate != 16000 || info.bits_per_sample != 16 ||
        (info.data_size & 1U) != 0) {
        return DESK_AUDIO_WAV_UNSUPPORTED_FORMAT;
    }
    *out_info = info;
    return DESK_AUDIO_WAV_OK;
}

void desk_audio_scale_pcm16(int16_t *samples, size_t sample_count,
                            unsigned volume_percent)
{
    if (!samples) {
        return;
    }
    unsigned volume = volume_percent > 100 ? 100 : volume_percent;
    for (size_t i = 0; i < sample_count; ++i) {
        int32_t scaled = (int32_t)samples[i] * (int32_t)volume;
        samples[i] = (int16_t)(scaled / 100);
    }
}
