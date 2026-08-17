/**
 * @file desk_audio.c
 * @brief SPIFFS WAV 资源、I2S DMA 和 MAX98357A 单流播放服务。
 *
 * HTTP 与提醒任务只投递短命令。文件读取和 I2S 写入全部留在本组件的
 * 单独任务中，确保扬声器缺失或播放较慢时不会阻塞 STOP 与桌控链路。
 */
#include "desk_audio.h"
#include "desk_audio_wav.h"

#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>

#define AUDIO_MOUNT_POINT "/audio"
#define AUDIO_PARTITION_LABEL "audio"
#define AUDIO_HEADER_SCAN_LIMIT 1024U
#define AUDIO_MONO_SAMPLES_PER_CHUNK 512U
/* IDF 例程用 1000ms；50ms 在 Wi-Fi/BLE 下不够等第一块 DMA 回队列。 */
#define AUDIO_I2S_WRITE_TIMEOUT_MS 1000
#define AUDIO_I2S_PRELOAD_SAMPLES 256U

static const char *TAG = "desk_audio";
static const char *NVS_NAMESPACE = "desk_audio";
static const char *NVS_ENABLED = "enabled";
static const char *NVS_VOLUME = "volume";

typedef enum {
    AUDIO_COMMAND_PLAY,
    AUDIO_COMMAND_STOP,
} audio_command_kind_t;

typedef struct {
    audio_command_kind_t kind;
    desk_audio_prompt_t prompt;
    desk_audio_priority_t priority;
    uint32_t generation;
} audio_command_t;

typedef struct {
    bool available;
    bool enabled;
    bool playing;
    uint8_t volume_percent;
    desk_audio_prompt_t current_prompt;
    desk_audio_priority_t current_priority;
    uint32_t generation;
    char last_error[64];
} audio_state_t;

static const char *const PROMPT_NAMES[] = {
    [DESK_AUDIO_PROMPT_FOCUS_DONE] = "focus_done",
    [DESK_AUDIO_PROMPT_BREAK_DONE] = "break_done",
    [DESK_AUDIO_PROMPT_SNOOZE_DONE] = "snooze_done",
    [DESK_AUDIO_PROMPT_ATTENTION_CHIME] = "attention_chime",
};

static SemaphoreHandle_t s_mutex;
static QueueHandle_t s_commands;
static i2s_chan_handle_t s_tx_channel;
static audio_state_t s_state = {
    .enabled = true,
    .volume_percent = CONFIG_DESK_AUDIO_DEFAULT_VOLUME_PERCENT,
};

const char *desk_audio_prompt_name(desk_audio_prompt_t prompt)
{
    if ((unsigned)prompt >= sizeof(PROMPT_NAMES) / sizeof(PROMPT_NAMES[0])) {
        return NULL;
    }
    return PROMPT_NAMES[prompt];
}

bool desk_audio_prompt_from_name(const char *name, desk_audio_prompt_t *out)
{
    if (!name || !out) {
        return false;
    }
    for (size_t i = 0; i < sizeof(PROMPT_NAMES) / sizeof(PROMPT_NAMES[0]); ++i) {
        if (strcmp(name, PROMPT_NAMES[i]) == 0) {
            *out = (desk_audio_prompt_t)i;
            return true;
        }
    }
    return false;
}

static void set_error_locked(const char *error)
{
    snprintf(s_state.last_error, sizeof(s_state.last_error), "%s",
             error ? error : "unknown_error");
}

static void load_config(void)
{
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }
    uint8_t enabled = 1;
    uint8_t volume = CONFIG_DESK_AUDIO_DEFAULT_VOLUME_PERCENT;
    if (nvs_get_u8(handle, NVS_ENABLED, &enabled) == ESP_OK) {
        s_state.enabled = enabled != 0;
    }
    if (nvs_get_u8(handle, NVS_VOLUME, &volume) == ESP_OK && volume <= 100) {
        s_state.volume_percent = volume;
    }
    nvs_close(handle);
}

static esp_err_t init_i2s(void)
{
    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&channel_config, &s_tx_channel, NULL);
    if (err != ESP_OK) {
        return err;
    }

    i2s_std_config_t config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_DESK_AUDIO_I2S_BCLK_GPIO,
            .ws = CONFIG_DESK_AUDIO_I2S_WS_GPIO,
            .dout = CONFIG_DESK_AUDIO_I2S_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    err = i2s_channel_init_std_mode(s_tx_channel, &config);
    if (err != ESP_OK) {
        i2s_del_channel(s_tx_channel);
        s_tx_channel = NULL;
    }
    return err;
}

/**
 * 只读取 chunk header 并跳过 PCM，不把整段语音载入 RAM。
 * file_size 用来校验 data chunk 宣称的长度确实存在。
 */
static esp_err_t parse_wav_file(FILE *file, long file_size,
                                desk_audio_wav_info_t *out)
{
    uint8_t riff[12];
    if (file_size < 12 || fread(riff, 1, sizeof(riff), file) != sizeof(riff) ||
        memcmp(riff, "RIFF", 4) != 0 || memcmp(riff + 8, "WAVE", 4) != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    bool found_fmt = false;
    bool found_data = false;
    uint16_t format = 0;
    desk_audio_wav_info_t info = {0};
    while (ftell(file) >= 0 && ftell(file) + 8 <= file_size &&
           ftell(file) < (long)AUDIO_HEADER_SCAN_LIMIT) {
        uint8_t chunk[8];
        if (fread(chunk, 1, sizeof(chunk), file) != sizeof(chunk)) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        uint32_t size = (uint32_t)chunk[4] | ((uint32_t)chunk[5] << 8) |
                        ((uint32_t)chunk[6] << 16) | ((uint32_t)chunk[7] << 24);
        long payload = ftell(file);
        if (payload < 0 || (uint64_t)payload + size > (uint64_t)file_size) {
            return ESP_ERR_INVALID_SIZE;
        }
        if (memcmp(chunk, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            if (size < sizeof(fmt) || fread(fmt, 1, sizeof(fmt), file) != sizeof(fmt)) {
                return ESP_ERR_INVALID_RESPONSE;
            }
            format = (uint16_t)fmt[0] | ((uint16_t)fmt[1] << 8);
            info.channels = (uint16_t)fmt[2] | ((uint16_t)fmt[3] << 8);
            info.sample_rate = (uint32_t)fmt[4] | ((uint32_t)fmt[5] << 8) |
                               ((uint32_t)fmt[6] << 16) | ((uint32_t)fmt[7] << 24);
            info.bits_per_sample = (uint16_t)fmt[14] | ((uint16_t)fmt[15] << 8);
            found_fmt = true;
        } else if (memcmp(chunk, "data", 4) == 0) {
            info.data_offset = (size_t)payload;
            info.data_size = size;
            found_data = true;
        }
        long next = payload + (long)size + (long)(size & 1U);
        if (fseek(file, next, SEEK_SET) != 0) {
            return ESP_ERR_INVALID_RESPONSE;
        }
        if (found_fmt && found_data) {
            break;
        }
    }

    if (!found_fmt || !found_data || format != 1 || info.channels != 1 ||
        info.sample_rate != 16000 || info.bits_per_sample != 16 ||
        (info.data_size & 1U) != 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    *out = info;
    return ESP_OK;
}

static void mark_playback(bool playing, desk_audio_prompt_t prompt,
                          desk_audio_priority_t priority, const char *error)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.playing = playing;
    s_state.current_prompt = prompt;
    s_state.current_priority = priority;
    if (error) {
        set_error_locked(error);
    } else if (playing) {
        s_state.last_error[0] = '\0';
    }
    xSemaphoreGive(s_mutex);
}

/** 使能前填满 DMA。不预载时第一笔 write 会空等 ISR，50ms 内容易变成 i2s_write_failed。 */
static esp_err_t preload_and_enable_i2s(void)
{
    static const int16_t silence[AUDIO_I2S_PRELOAD_SAMPLES] = {0};
    size_t loaded = sizeof(silence);
    while (loaded == sizeof(silence)) {
        esp_err_t err = i2s_channel_preload_data(
            s_tx_channel, silence, sizeof(silence), &loaded);
        if (err != ESP_OK) {
            return err;
        }
    }
    return i2s_channel_enable(s_tx_channel);
}

/** 写入前后静音并在播放结束时 disable，以减少旧 DMA 数据和复位爆音。 */
static esp_err_t write_silence(void)
{
    static const int16_t silence[128] = {0};
    size_t written = 0;
    return i2s_channel_write(s_tx_channel, silence, sizeof(silence), &written,
                             pdMS_TO_TICKS(AUDIO_I2S_WRITE_TIMEOUT_MS));
}

static bool receive_preempting_command(audio_command_t *out)
{
    return xQueueReceive(s_commands, out, 0) == pdTRUE;
}

/** 播放一个资源；返回 true 表示 out_next 中有抢占命令需要立即处理。 */
static bool play_one(const audio_command_t *command, audio_command_t *out_next)
{
    const char *name = desk_audio_prompt_name(command->prompt);
    char path[128];
    snprintf(path, sizeof(path), AUDIO_MOUNT_POINT "/" DESK_AUDIO_VOICE_PACK
             "/%s.wav", name ? name : "invalid");
    FILE *file = fopen(path, "rb");
    if (!file) {
        mark_playback(false, command->prompt, command->priority,
                      "audio_resource_missing");
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        mark_playback(false, command->prompt, command->priority,
                      "audio_resource_seek_failed");
        return false;
    }
    long file_size = ftell(file);
    rewind(file);
    desk_audio_wav_info_t info;
    esp_err_t err = parse_wav_file(file, file_size, &info);
    if (err != ESP_OK || fseek(file, (long)info.data_offset, SEEK_SET) != 0) {
        fclose(file);
        mark_playback(false, command->prompt, command->priority,
                      "audio_resource_invalid");
        return false;
    }

    uint8_t volume;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool allowed = s_state.available && s_state.enabled &&
                   s_state.volume_percent > 0 &&
                   command->generation == s_state.generation;
    volume = s_state.volume_percent;
    xSemaphoreGive(s_mutex);
    if (!allowed) {
        fclose(file);
        return false;
    }

    err = preload_and_enable_i2s();
    if (err != ESP_OK) {
        char detail[64];
        snprintf(detail, sizeof(detail), "i2s_enable:%s", esp_err_to_name(err));
        ESP_LOGE(TAG, "%s", detail);
        fclose(file);
        mark_playback(false, command->prompt, command->priority, detail);
        return false;
    }
    mark_playback(true, command->prompt, command->priority, NULL);
    if (write_silence() != ESP_OK) {
        ESP_LOGW(TAG, "leading silence write failed");
    }

    int16_t mono[AUDIO_MONO_SAMPLES_PER_CHUNK];
    int16_t stereo[AUDIO_MONO_SAMPLES_PER_CHUNK * 2U];
    uint32_t remaining = info.data_size;
    bool preempted = false;
    while (remaining > 0) {
        size_t requested = remaining < sizeof(mono) ? remaining : sizeof(mono);
        size_t received = fread(mono, 1, requested, file);
        if (received != requested || (received & 1U) != 0) {
            mark_playback(false, command->prompt, command->priority,
                          "audio_read_failed");
            break;
        }
        size_t samples = received / sizeof(mono[0]);
        desk_audio_scale_pcm16(mono, samples, volume);
        /* MAX98357A 模块的声道选择电阻不统一，复制到 L/R 可直接兼容。 */
        for (size_t i = 0; i < samples; ++i) {
            stereo[i * 2] = mono[i];
            stereo[i * 2 + 1] = mono[i];
        }
        size_t written = 0;
        size_t expected = samples * 2U * sizeof(stereo[0]);
        err = i2s_channel_write(s_tx_channel, stereo, expected, &written,
                                pdMS_TO_TICKS(AUDIO_I2S_WRITE_TIMEOUT_MS));
        if (err != ESP_OK || written != expected) {
            char detail[64];
            snprintf(detail, sizeof(detail), "i2s_write:%s",
                     esp_err_to_name(err != ESP_OK ? err : ESP_ERR_INVALID_SIZE));
            ESP_LOGE(TAG, "%s written=%u expected=%u", detail,
                     (unsigned)written, (unsigned)expected);
            mark_playback(false, command->prompt, command->priority, detail);
            break;
        }
        remaining -= (uint32_t)received;
        if (receive_preempting_command(out_next)) {
            preempted = true;
            break;
        }
    }
    (void)write_silence();
    (void)i2s_channel_disable(s_tx_channel);
    fclose(file);
    mark_playback(false, command->prompt, command->priority, NULL);
    return preempted;
}

static void audio_task(void *arg)
{
    (void)arg;
    audio_command_t command;
    for (;;) {
        if (xQueueReceive(s_commands, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        do {
            if (command.kind == AUDIO_COMMAND_STOP) {
                mark_playback(false, command.prompt, command.priority, NULL);
                break;
            }
        } while (play_one(&command, &command));
    }
}

esp_err_t desk_audio_init(void)
{
    if (s_mutex) {
        return s_state.available ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    s_mutex = xSemaphoreCreateMutex();
    s_commands = xQueueCreate(4, sizeof(audio_command_t));
    if (!s_mutex || !s_commands) {
        return ESP_ERR_NO_MEM;
    }
    load_config();

    esp_vfs_spiffs_conf_t fs_config = {
        .base_path = AUDIO_MOUNT_POINT,
        .partition_label = AUDIO_PARTITION_LABEL,
        .max_files = 2,
        .format_if_mount_failed = false,
    };
    esp_err_t err = esp_vfs_spiffs_register(&fs_config);
    if (err == ESP_OK) {
        err = init_i2s();
    }
    if (err == ESP_OK &&
        xTaskCreate(audio_task, "desk_audio", 6144, NULL,
                    tskIDLE_PRIORITY + 2, NULL) != pdPASS) {
        err = ESP_ERR_NO_MEM;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.available = err == ESP_OK;
    if (err != ESP_OK) {
        set_error_locked(err == ESP_ERR_NOT_FOUND ? "audio_partition_missing" :
                                                  "audio_init_failed");
    }
    xSemaphoreGive(s_mutex);
    return err;
}

esp_err_t desk_audio_play(desk_audio_prompt_t prompt,
                          desk_audio_priority_t priority)
{
    if (!desk_audio_prompt_name(prompt) || !s_mutex || !s_commands) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (!s_state.available || !s_state.enabled || s_state.volume_percent == 0) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    if (priority == DESK_AUDIO_PRIORITY_PREVIEW && s_state.playing &&
        s_state.current_priority == DESK_AUDIO_PRIORITY_ALARM) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    uint32_t generation = ++s_state.generation;
    audio_command_t command = {
        .kind = AUDIO_COMMAND_PLAY,
        .prompt = prompt,
        .priority = priority,
        .generation = generation,
    };
    /* 新请求替换未开始的旧请求；当前流会在至多一个 DMA chunk 后被抢占。 */
    xQueueReset(s_commands);
    bool queued = xQueueSendToFront(s_commands, &command, 0) == pdTRUE;
    xSemaphoreGive(s_mutex);
    return queued ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t desk_audio_stop(void)
{
    if (!s_mutex || !s_commands) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    uint32_t generation = ++s_state.generation;
    audio_command_t command = {
        .kind = AUDIO_COMMAND_STOP,
        .generation = generation,
    };
    xQueueReset(s_commands);
    bool queued = xQueueSendToFront(s_commands, &command, 0) == pdTRUE;
    xSemaphoreGive(s_mutex);
    return queued ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t desk_audio_set_config(bool enabled, uint8_t volume_percent)
{
    if (!s_mutex || volume_percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool unchanged = s_state.enabled == enabled &&
                     s_state.volume_percent == volume_percent;
    xSemaphoreGive(s_mutex);
    if (unchanged) {
        return ESP_OK;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(handle, NVS_ENABLED, enabled ? 1 : 0);
    if (err == ESP_OK) err = nvs_set_u8(handle, NVS_VOLUME, volume_percent);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) {
        return err;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_state.enabled = enabled;
    s_state.volume_percent = volume_percent;
    xSemaphoreGive(s_mutex);
    if (!enabled || volume_percent == 0) {
        (void)desk_audio_stop();
    }
    return ESP_OK;
}

desk_audio_snapshot_t desk_audio_snapshot(void)
{
    desk_audio_snapshot_t snapshot = {
        .voice_pack = DESK_AUDIO_VOICE_PACK,
    };
    if (!s_mutex) {
        snapshot.last_error = "audio_not_initialized";
        return snapshot;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    snapshot.available = s_state.available;
    snapshot.enabled = s_state.enabled;
    snapshot.playing = s_state.playing;
    snapshot.volume_percent = s_state.volume_percent;
    snapshot.current_prompt = s_state.playing
                                  ? desk_audio_prompt_name(s_state.current_prompt)
                                  : NULL;
    snapshot.last_error = s_state.last_error[0] ? s_state.last_error : NULL;
    xSemaphoreGive(s_mutex);
    return snapshot;
}
