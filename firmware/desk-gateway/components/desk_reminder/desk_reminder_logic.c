/**
 * @file desk_reminder_logic.c
 * @brief 番茄时钟纯状态转换与单调时间计算。
 */
#include "desk_reminder_logic.h"

#include <limits.h>
#include <string.h>

#define US_PER_SECOND 1000000LL

void desk_reminder_logic_reset(desk_reminder_model_t *model)
{
    if (!model) return;
    uint32_t next_generation = model->generation + 1U;
    *model = (desk_reminder_model_t){
        .state = DESK_REMINDER_STATE_IDLE,
        .phase = DESK_REMINDER_PHASE_FOCUS,
        .alarm_reason = DESK_REMINDER_ALARM_NONE,
        .generation = next_generation,
        .auto_cycle = false,
    };
}

bool desk_reminder_config_valid(const desk_reminder_config_t *config)
{
    return config && config->focus_minutes >= 1 && config->focus_minutes <= 180 &&
           config->short_break_minutes >= 1 && config->short_break_minutes <= 60 &&
           config->long_break_minutes >= 1 && config->long_break_minutes <= 120 &&
           config->focuses_per_long_break >= 1 &&
           config->focuses_per_long_break <= 12 &&
           config->snooze_minutes >= 1 && config->snooze_minutes <= 30;
}

static uint32_t phase_duration_sec(desk_reminder_phase_t phase,
                                   const desk_reminder_config_t *config)
{
    uint32_t minutes = phase == DESK_REMINDER_PHASE_FOCUS
                           ? config->focus_minutes
                           : phase == DESK_REMINDER_PHASE_LONG_BREAK
                                 ? config->long_break_minutes
                                 : config->short_break_minutes;
    return minutes * 60U;
}

static uint32_t remaining_until(int64_t deadline_us, int64_t now_us)
{
    int64_t remaining_us = deadline_us - now_us;
    if (remaining_us <= 0) return 0;
    int64_t rounded = (remaining_us + US_PER_SECOND - 1) / US_PER_SECOND;
    return rounded > UINT32_MAX ? UINT32_MAX : (uint32_t)rounded;
}

/** 自动循环在 waiting 上挂 15 秒空窗；手动模式保持 deadline=0。 */
static void arm_auto_advance(desk_reminder_model_t *model, int64_t now_us)
{
    if (!model->auto_cycle) {
        model->deadline_us = 0;
        return;
    }
    model->deadline_us = now_us +
                         (int64_t)DESK_REMINDER_AUTO_ADVANCE_SEC * US_PER_SECOND;
}

static void start_countdown(desk_reminder_model_t *model, uint32_t seconds,
                            int64_t now_us, desk_reminder_state_t state)
{
    model->state = state;
    model->alarm_reason = DESK_REMINDER_ALARM_NONE;
    model->paused_remaining_sec = 0;
    model->generation++;
    model->deadline_us = now_us + (int64_t)seconds * US_PER_SECOND;
}

uint32_t desk_reminder_logic_auto_advance_sec(const desk_reminder_model_t *model,
                                              int64_t now_us)
{
    if (!model || !model->auto_cycle ||
        model->state != DESK_REMINDER_STATE_WAITING ||
        model->deadline_us == 0) {
        return 0;
    }
    return remaining_until(model->deadline_us, now_us);
}

uint32_t desk_reminder_logic_remaining_sec(const desk_reminder_model_t *model,
                                           int64_t now_us)
{
    if (!model) return 0;
    if (model->state == DESK_REMINDER_STATE_PAUSED) {
        return model->paused_remaining_sec;
    }
    if (model->state != DESK_REMINDER_STATE_RUNNING &&
        model->state != DESK_REMINDER_STATE_SNOOZED) {
        return 0;
    }
    return remaining_until(model->deadline_us, now_us);
}

bool desk_reminder_logic_apply(desk_reminder_model_t *model,
                               desk_reminder_action_t action,
                               const desk_reminder_config_t *config,
                               int64_t now_us)
{
    if (!model || !desk_reminder_config_valid(config)) return false;
    switch (action) {
    case DESK_REMINDER_ACTION_START_FOCUS:
        if (model->state != DESK_REMINDER_STATE_IDLE &&
            !(model->state == DESK_REMINDER_STATE_WAITING &&
              model->phase == DESK_REMINDER_PHASE_FOCUS)) return false;
        if (model->state == DESK_REMINDER_STATE_IDLE) model->auto_cycle = false;
        model->phase = DESK_REMINDER_PHASE_FOCUS;
        start_countdown(model, phase_duration_sec(model->phase, config), now_us,
                        DESK_REMINDER_STATE_RUNNING);
        return true;
    case DESK_REMINDER_ACTION_START_AUTO:
        if (model->state != DESK_REMINDER_STATE_IDLE) return false;
        model->auto_cycle = true;
        model->phase = DESK_REMINDER_PHASE_FOCUS;
        start_countdown(model, phase_duration_sec(model->phase, config), now_us,
                        DESK_REMINDER_STATE_RUNNING);
        return true;
    case DESK_REMINDER_ACTION_START_BREAK:
        if (model->state != DESK_REMINDER_STATE_WAITING ||
            model->phase == DESK_REMINDER_PHASE_FOCUS) return false;
        start_countdown(model, phase_duration_sec(model->phase, config), now_us,
                        DESK_REMINDER_STATE_RUNNING);
        return true;
    case DESK_REMINDER_ACTION_PAUSE:
        if (model->state != DESK_REMINDER_STATE_RUNNING) return false;
        model->paused_remaining_sec =
            desk_reminder_logic_remaining_sec(model, now_us);
        if (model->paused_remaining_sec == 0) return false;
        model->state = DESK_REMINDER_STATE_PAUSED;
        model->generation++;
        model->deadline_us = 0;
        return true;
    case DESK_REMINDER_ACTION_RESUME:
        if (model->state != DESK_REMINDER_STATE_PAUSED ||
            model->paused_remaining_sec == 0) return false;
        start_countdown(model, model->paused_remaining_sec, now_us,
                        DESK_REMINDER_STATE_RUNNING);
        return true;
    case DESK_REMINDER_ACTION_SKIP:
        if (model->state != DESK_REMINDER_STATE_RUNNING &&
            model->state != DESK_REMINDER_STATE_PAUSED) return false;
        model->phase = model->phase == DESK_REMINDER_PHASE_FOCUS
                           ? DESK_REMINDER_PHASE_SHORT_BREAK
                           : DESK_REMINDER_PHASE_FOCUS;
        model->state = DESK_REMINDER_STATE_WAITING;
        model->alarm_reason = DESK_REMINDER_ALARM_NONE;
        model->paused_remaining_sec = 0;
        model->generation++;
        arm_auto_advance(model, now_us);
        return true;
    case DESK_REMINDER_ACTION_STOP:
        desk_reminder_logic_reset(model);
        model->completed_focus_count = 0;
        return true;
    case DESK_REMINDER_ACTION_SNOOZE:
        if (model->state != DESK_REMINDER_STATE_WAITING ||
            model->alarm_reason == DESK_REMINDER_ALARM_NONE) return false;
        desk_reminder_alarm_reason_t pending_alarm = model->alarm_reason;
        start_countdown(model, config->snooze_minutes * 60U, now_us,
                        DESK_REMINDER_STATE_SNOOZED);
        /* 延后结束后仍保留原到期语义，允许用户再次延后或开始下一阶段。 */
        model->alarm_reason = pending_alarm;
        return true;
    default:
        return false;
    }
}

desk_reminder_effect_t desk_reminder_logic_expire(
    desk_reminder_model_t *model, uint32_t generation,
    const desk_reminder_config_t *config, int64_t now_us)
{
    if (!model || !desk_reminder_config_valid(config) ||
        generation != model->generation) {
        return DESK_REMINDER_EFFECT_NONE;
    }
    if (model->state == DESK_REMINDER_STATE_WAITING && model->auto_cycle &&
        model->deadline_us > 0 && now_us >= model->deadline_us) {
        start_countdown(model, phase_duration_sec(model->phase, config), now_us,
                        DESK_REMINDER_STATE_RUNNING);
        return DESK_REMINDER_EFFECT_NONE;
    }
    if ((model->state != DESK_REMINDER_STATE_RUNNING &&
         model->state != DESK_REMINDER_STATE_SNOOZED) ||
        now_us < model->deadline_us) {
        return DESK_REMINDER_EFFECT_NONE;
    }
    if (model->state == DESK_REMINDER_STATE_SNOOZED) {
        model->state = DESK_REMINDER_STATE_WAITING;
        model->generation++;
        arm_auto_advance(model, now_us);
        return DESK_REMINDER_EFFECT_SNOOZE_DONE;
    }

    desk_reminder_effect_t effect;
    if (model->phase == DESK_REMINDER_PHASE_FOCUS) {
        model->completed_focus_count++;
        bool long_break = model->completed_focus_count %
                              config->focuses_per_long_break == 0;
        model->phase = long_break ? DESK_REMINDER_PHASE_LONG_BREAK
                                  : DESK_REMINDER_PHASE_SHORT_BREAK;
        model->alarm_reason = DESK_REMINDER_ALARM_FOCUS_DONE;
        effect = DESK_REMINDER_EFFECT_FOCUS_DONE;
    } else {
        model->phase = DESK_REMINDER_PHASE_FOCUS;
        model->alarm_reason = DESK_REMINDER_ALARM_BREAK_DONE;
        effect = DESK_REMINDER_EFFECT_BREAK_DONE;
    }
    model->state = DESK_REMINDER_STATE_WAITING;
    model->generation++;
    arm_auto_advance(model, now_us);
    return effect;
}

const char *desk_reminder_state_name(desk_reminder_state_t state)
{
    static const char *const names[] = {
        "idle", "running", "paused", "waiting", "snoozed",
    };
    return (unsigned)state < sizeof(names) / sizeof(names[0])
               ? names[state] : "unknown";
}

const char *desk_reminder_phase_name(desk_reminder_phase_t phase)
{
    static const char *const names[] = {
        "focus", "short_break", "long_break",
    };
    return (unsigned)phase < sizeof(names) / sizeof(names[0])
               ? names[phase] : "unknown";
}

const char *desk_reminder_alarm_name(desk_reminder_alarm_reason_t reason)
{
    static const char *const names[] = {"none", "focus_done", "break_done"};
    return (unsigned)reason < sizeof(names) / sizeof(names[0])
               ? names[reason] : "unknown";
}

bool desk_reminder_action_from_name(const char *name,
                                    desk_reminder_action_t *out_action)
{
    static const char *const names[] = {
        "start_focus", "start_break", "pause", "resume",
        "skip", "stop", "snooze", "start_auto",
    };
    if (!name || !out_action) return false;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (strcmp(name, names[i]) == 0) {
            *out_action = (desk_reminder_action_t)i;
            return true;
        }
    }
    return false;
}
