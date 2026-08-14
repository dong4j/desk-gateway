/**
 * @file desk_reminder.c
 * @brief NVS 配置、esp_timer 单次定时器和到期语音编排。
 *
 * esp_timer 回调只投递 generation；状态转换和音频请求由普通 FreeRTOS
 * 任务处理，避免在高优先级 timer task 中做文件或 I2S 操作。
 */
#include "desk_reminder.h"

#include "desk_audio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#include <stdio.h>

static const char *TAG = "desk_reminder";
static const char *NVS_NAMESPACE = "desk_reminder";

static const desk_reminder_config_t DEFAULT_CONFIG = {
    .focus_minutes = 25,
    .short_break_minutes = 5,
    .long_break_minutes = 15,
    .focuses_per_long_break = 4,
    .snooze_minutes = 5,
};

static SemaphoreHandle_t s_mutex;
static QueueHandle_t s_expiry_queue;
static esp_timer_handle_t s_timer;
static desk_reminder_model_t s_model;
static desk_reminder_config_t s_config;
static volatile uint32_t s_timer_generation;
static bool s_available;
static char s_last_error[64];

static void load_config(void)
{
    s_config = DEFAULT_CONFIG;
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return;
    (void)nvs_get_u16(handle, "focus_min", &s_config.focus_minutes);
    (void)nvs_get_u16(handle, "short_min", &s_config.short_break_minutes);
    (void)nvs_get_u16(handle, "long_min", &s_config.long_break_minutes);
    (void)nvs_get_u8(handle, "long_every", &s_config.focuses_per_long_break);
    nvs_close(handle);
    if (!desk_reminder_config_valid(&s_config)) s_config = DEFAULT_CONFIG;
}

static esp_err_t save_config(const desk_reminder_config_t *config)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_u16(handle, "focus_min", config->focus_minutes);
    if (err == ESP_OK) err = nvs_set_u16(handle, "short_min", config->short_break_minutes);
    if (err == ESP_OK) err = nvs_set_u16(handle, "long_min", config->long_break_minutes);
    if (err == ESP_OK) err = nvs_set_u8(handle, "long_every", config->focuses_per_long_break);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static void timer_callback(void *arg)
{
    (void)arg;
    uint32_t generation = s_timer_generation;
    (void)xQueueSend(s_expiry_queue, &generation, 0);
}

/** 调用方持有 s_mutex；旧 callback 即使已入队也会被 generation 拦截。 */
static esp_err_t reschedule_timer_locked(int64_t now_us)
{
    (void)esp_timer_stop(s_timer);
    if (s_model.state != DESK_REMINDER_STATE_RUNNING &&
        s_model.state != DESK_REMINDER_STATE_SNOOZED) {
        return ESP_OK;
    }
    int64_t delay_us = s_model.deadline_us - now_us;
    if (delay_us < 1) delay_us = 1;
    s_timer_generation = s_model.generation;
    return esp_timer_start_once(s_timer, (uint64_t)delay_us);
}

static void play_effect(desk_reminder_effect_t effect)
{
    desk_audio_prompt_t prompt;
    switch (effect) {
    case DESK_REMINDER_EFFECT_FOCUS_DONE:
        prompt = DESK_AUDIO_PROMPT_FOCUS_DONE;
        break;
    case DESK_REMINDER_EFFECT_BREAK_DONE:
        prompt = DESK_AUDIO_PROMPT_BREAK_DONE;
        break;
    case DESK_REMINDER_EFFECT_SNOOZE_DONE:
        prompt = DESK_AUDIO_PROMPT_SNOOZE_DONE;
        break;
    default:
        return;
    }
    esp_err_t err = desk_audio_play(prompt, DESK_AUDIO_PRIORITY_ALARM);
    if (err != ESP_OK) {
        /* 计时转换已经完成；音频降级只记录，不得回滚提醒状态。 */
        ESP_LOGW(TAG, "alarm audio unavailable: %s", esp_err_to_name(err));
    }
}

static void reminder_task(void *arg)
{
    (void)arg;
    uint32_t generation;
    for (;;) {
        if (xQueueReceive(s_expiry_queue, &generation, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        int64_t now_us = esp_timer_get_time();
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        desk_reminder_effect_t effect = desk_reminder_logic_expire(
            &s_model, generation, &s_config, now_us);
        xSemaphoreGive(s_mutex);
        play_effect(effect);
    }
}

esp_err_t desk_reminder_init(void)
{
    if (s_mutex) return s_available ? ESP_OK : ESP_ERR_INVALID_STATE;
    s_mutex = xSemaphoreCreateMutex();
    s_expiry_queue = xQueueCreate(4, sizeof(uint32_t));
    if (!s_mutex || !s_expiry_queue) return ESP_ERR_NO_MEM;
    load_config();
    desk_reminder_logic_reset(&s_model);

    const esp_timer_create_args_t timer_args = {
        .callback = timer_callback,
        .name = "desk_reminder",
    };
    esp_err_t err = esp_timer_create(&timer_args, &s_timer);
    if (err == ESP_OK &&
        xTaskCreate(reminder_task, "desk_reminder", 3072, NULL,
                    tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        err = ESP_ERR_NO_MEM;
    }
    s_available = err == ESP_OK;
    if (err != ESP_OK) {
        snprintf(s_last_error, sizeof(s_last_error), "reminder_init_failed");
    }
    return err;
}

esp_err_t desk_reminder_perform(desk_reminder_action_t action)
{
    if (!s_mutex || !s_available) return ESP_ERR_INVALID_STATE;
    int64_t now_us = esp_timer_get_time();
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    desk_reminder_model_t previous = s_model;
    bool changed = desk_reminder_logic_apply(&s_model, action, &s_config, now_us);
    esp_err_t err = changed ? reschedule_timer_locked(now_us)
                            : ESP_ERR_INVALID_STATE;
    if (changed && err != ESP_OK) {
        /* 不留下“页面在倒计时、实际没有 timer”的半成功状态。 */
        s_model = previous;
        (void)reschedule_timer_locked(now_us);
    }
    xSemaphoreGive(s_mutex);
    if (err == ESP_OK) {
        /* 用户作出新决定后，不允许上一条到期语音继续播放。 */
        (void)desk_audio_stop();
    }
    return err;
}

esp_err_t desk_reminder_update_config(
    const desk_reminder_config_patch_t *patch)
{
    if (!s_mutex || !s_available || !patch) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    desk_reminder_config_t next = s_config;
    if (patch->has_focus_minutes) next.focus_minutes = patch->focus_minutes;
    if (patch->has_short_break_minutes) next.short_break_minutes = patch->short_break_minutes;
    if (patch->has_long_break_minutes) next.long_break_minutes = patch->long_break_minutes;
    if (patch->has_focuses_per_long_break) {
        next.focuses_per_long_break = patch->focuses_per_long_break;
    }
    if (!desk_reminder_config_valid(&next)) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_ARG;
    }
    bool unchanged = next.focus_minutes == s_config.focus_minutes &&
                     next.short_break_minutes == s_config.short_break_minutes &&
                     next.long_break_minutes == s_config.long_break_minutes &&
                     next.focuses_per_long_break ==
                         s_config.focuses_per_long_break;
    if (unchanged) {
        xSemaphoreGive(s_mutex);
        return ESP_OK;
    }
    esp_err_t err = save_config(&next);
    if (err == ESP_OK) s_config = next;
    xSemaphoreGive(s_mutex);
    return err;
}

desk_reminder_snapshot_t desk_reminder_snapshot(void)
{
    desk_reminder_snapshot_t snapshot = {0};
    if (!s_mutex) {
        snapshot.last_error = "reminder_not_initialized";
        return snapshot;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    snapshot.available = s_available;
    snapshot.state = s_model.state;
    snapshot.phase = s_model.phase;
    snapshot.alarm_reason = s_model.alarm_reason;
    snapshot.remaining_sec = desk_reminder_logic_remaining_sec(
        &s_model, esp_timer_get_time());
    snapshot.completed_focus_count = s_model.completed_focus_count;
    snapshot.config = s_config;
    snapshot.last_error = s_last_error[0] ? s_last_error : NULL;
    xSemaphoreGive(s_mutex);
    return snapshot;
}
