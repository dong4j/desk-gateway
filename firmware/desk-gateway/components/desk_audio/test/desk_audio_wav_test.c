/**
 * @file desk_audio_wav_test.c
 * @brief 固定 WAV 契约和 PCM 音量的 Host tests。
 */
#include "desk_audio_wav.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put_le16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put_le32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static size_t make_wav(uint8_t *wav, uint32_t rate, uint16_t channels,
                       uint16_t bits)
{
    memset(wav, 0, 48);
    memcpy(wav, "RIFF", 4);
    put_le32(wav + 4, 40);
    memcpy(wav + 8, "WAVEfmt ", 8);
    put_le32(wav + 16, 16);
    put_le16(wav + 20, 1);
    put_le16(wav + 22, channels);
    put_le32(wav + 24, rate);
    put_le16(wav + 34, bits);
    memcpy(wav + 36, "data", 4);
    put_le32(wav + 40, 4);
    return 48;
}

/** 扫描 data chunk 的绝对峰值，给提示音响度门禁用。 */
static int16_t pcm_peak(const uint8_t *bytes, const desk_audio_wav_info_t *info)
{
    const int16_t *pcm = (const int16_t *)(bytes + info->data_offset);
    size_t count = info->data_size / sizeof(int16_t);
    int32_t peak = 0;
    for (size_t i = 0; i < count; ++i) {
        int32_t sample = pcm[i];
        if (sample < 0) {
            sample = -sample;
        }
        if (sample > peak) {
            peak = sample;
        }
    }
    return (int16_t)peak;
}

static void verify_asset(const char *path)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    long size = ftell(file);
    assert(size > 0 && fseek(file, 0, SEEK_SET) == 0);
    uint8_t *bytes = malloc((size_t)size);
    assert(bytes && fread(bytes, 1, (size_t)size, file) == (size_t)size);
    fclose(file);
    desk_audio_wav_info_t info;
    assert(desk_audio_wav_parse(bytes, (size_t)size, &info) ==
           DESK_AUDIO_WAV_OK);
    assert(info.data_size > 0);
    /* 旧提示音峰值约 -32 dBFS，100% 音量仍远小于语音；门禁避免再回归。 */
    if (strstr(path, "attention_chime") != NULL) {
        assert(pcm_peak(bytes, &info) >= 8000);
    }
    free(bytes);
}

int main(int argc, char **argv)
{
    uint8_t wav[48];
    desk_audio_wav_info_t info;
    size_t size = make_wav(wav, 16000, 1, 16);
    assert(desk_audio_wav_parse(wav, size, &info) == DESK_AUDIO_WAV_OK);
    assert(info.data_offset == 44 && info.data_size == 4);

    make_wav(wav, 44100, 1, 16);
    assert(desk_audio_wav_parse(wav, size, &info) ==
           DESK_AUDIO_WAV_UNSUPPORTED_FORMAT);
    make_wav(wav, 16000, 2, 16);
    assert(desk_audio_wav_parse(wav, size, &info) ==
           DESK_AUDIO_WAV_UNSUPPORTED_FORMAT);
    make_wav(wav, 16000, 1, 8);
    assert(desk_audio_wav_parse(wav, size, &info) ==
           DESK_AUDIO_WAV_UNSUPPORTED_FORMAT);
    assert(desk_audio_wav_parse(wav, 20, &info) == DESK_AUDIO_WAV_TRUNCATED);

    int16_t samples[] = {INT16_MIN, -1000, 0, 1000, INT16_MAX};
    desk_audio_scale_pcm16(samples, 5, 0);
    for (size_t i = 0; i < 5; ++i) assert(samples[i] == 0);
    int16_t full[] = {INT16_MIN, INT16_MAX};
    desk_audio_scale_pcm16(full, 2, 100);
    assert(full[0] == INT16_MIN && full[1] == INT16_MAX);

    for (int i = 1; i < argc; ++i) verify_asset(argv[i]);

    puts("desk_audio_wav_test: ok");
    return 0;
}
