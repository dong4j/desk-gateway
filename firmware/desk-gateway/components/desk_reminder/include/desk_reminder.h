/**
 * @file desk_reminder.h
 * @brief ESP 单调时钟驱动的番茄提醒服务。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "desk_reminder_logic.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool available;
    desk_reminder_state_t state;
    desk_reminder_phase_t phase;
    desk_reminder_alarm_reason_t alarm_reason;
    uint32_t remaining_sec;
    uint32_t completed_focus_count;
    desk_reminder_config_t config;
    const char *last_error;
} desk_reminder_snapshot_t;

typedef struct {
    bool has_focus_minutes;
    bool has_short_break_minutes;
    bool has_long_break_minutes;
    bool has_focuses_per_long_break;
    uint16_t focus_minutes;
    uint16_t short_break_minutes;
    uint16_t long_break_minutes;
    uint8_t focuses_per_long_break;
} desk_reminder_config_patch_t;

esp_err_t desk_reminder_init(void);
esp_err_t desk_reminder_perform(desk_reminder_action_t action);
esp_err_t desk_reminder_update_config(
    const desk_reminder_config_patch_t *patch);
desk_reminder_snapshot_t desk_reminder_snapshot(void);

#ifdef __cplusplus
}
#endif
