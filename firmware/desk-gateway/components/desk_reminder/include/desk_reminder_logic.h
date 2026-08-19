/**
 * @file desk_reminder_logic.h
 * @brief 与 ESP-IDF 无关的番茄时钟状态机。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 自动循环在 waiting 停留的空窗，给语音和延后留时间。 */
#define DESK_REMINDER_AUTO_ADVANCE_SEC 15U

typedef enum {
    DESK_REMINDER_STATE_IDLE = 0,
    DESK_REMINDER_STATE_RUNNING,
    DESK_REMINDER_STATE_PAUSED,
    DESK_REMINDER_STATE_WAITING,
    DESK_REMINDER_STATE_SNOOZED,
} desk_reminder_state_t;

typedef enum {
    DESK_REMINDER_PHASE_FOCUS = 0,
    DESK_REMINDER_PHASE_SHORT_BREAK,
    DESK_REMINDER_PHASE_LONG_BREAK,
} desk_reminder_phase_t;

typedef enum {
    DESK_REMINDER_ALARM_NONE = 0,
    DESK_REMINDER_ALARM_FOCUS_DONE,
    DESK_REMINDER_ALARM_BREAK_DONE,
} desk_reminder_alarm_reason_t;

typedef enum {
    DESK_REMINDER_ACTION_START_FOCUS = 0,
    DESK_REMINDER_ACTION_START_BREAK,
    DESK_REMINDER_ACTION_PAUSE,
    DESK_REMINDER_ACTION_RESUME,
    DESK_REMINDER_ACTION_SKIP,
    DESK_REMINDER_ACTION_STOP,
    DESK_REMINDER_ACTION_SNOOZE,
    DESK_REMINDER_ACTION_START_AUTO,
} desk_reminder_action_t;

typedef enum {
    DESK_REMINDER_EFFECT_NONE = 0,
    DESK_REMINDER_EFFECT_FOCUS_DONE,
    DESK_REMINDER_EFFECT_BREAK_DONE,
    DESK_REMINDER_EFFECT_SNOOZE_DONE,
} desk_reminder_effect_t;

typedef struct {
    uint16_t focus_minutes;
    uint16_t short_break_minutes;
    uint16_t long_break_minutes;
    uint8_t focuses_per_long_break;
    uint8_t snooze_minutes;
} desk_reminder_config_t;

typedef struct {
    desk_reminder_state_t state;
    desk_reminder_phase_t phase;
    desk_reminder_alarm_reason_t alarm_reason;
    uint32_t completed_focus_count;
    uint32_t generation;
    uint32_t paused_remaining_sec;
    int64_t deadline_us;
    /* 只存在本次上电会话；重启回 idle，避免 NVS 把未完成循环写穿。 */
    bool auto_cycle;
} desk_reminder_model_t;

void desk_reminder_logic_reset(desk_reminder_model_t *model);
bool desk_reminder_config_valid(const desk_reminder_config_t *config);
bool desk_reminder_logic_apply(desk_reminder_model_t *model,
                               desk_reminder_action_t action,
                               const desk_reminder_config_t *config,
                               int64_t now_us);
desk_reminder_effect_t desk_reminder_logic_expire(
    desk_reminder_model_t *model, uint32_t generation,
    const desk_reminder_config_t *config, int64_t now_us);
uint32_t desk_reminder_logic_remaining_sec(const desk_reminder_model_t *model,
                                           int64_t now_us);
uint32_t desk_reminder_logic_auto_advance_sec(const desk_reminder_model_t *model,
                                              int64_t now_us);

const char *desk_reminder_state_name(desk_reminder_state_t state);
const char *desk_reminder_phase_name(desk_reminder_phase_t phase);
const char *desk_reminder_alarm_name(desk_reminder_alarm_reason_t reason);
bool desk_reminder_action_from_name(const char *name,
                                    desk_reminder_action_t *out_action);

#ifdef __cplusplus
}
#endif
