/**
 * @file desk_reminder_logic_test.c
 * @brief 无真实等待的番茄时钟状态机 Host tests。
 */
#include "desk_reminder_logic.h"

#include <assert.h>
#include <stdio.h>

static const desk_reminder_config_t CONFIG = {
    .focus_minutes = 25,
    .short_break_minutes = 5,
    .long_break_minutes = 15,
    .focuses_per_long_break = 4,
    .snooze_minutes = 5,
};

int main(void)
{
    desk_reminder_model_t model = {0};
    desk_reminder_logic_reset(&model);
    assert(model.state == DESK_REMINDER_STATE_IDLE);
    assert(desk_reminder_logic_apply(&model, DESK_REMINDER_ACTION_START_FOCUS,
                                     &CONFIG, 0));
    assert(desk_reminder_logic_remaining_sec(&model, 1) == 1500);
    uint32_t generation = model.generation;
    assert(desk_reminder_logic_expire(&model, generation, &CONFIG,
                                      1500LL * 1000000) ==
           DESK_REMINDER_EFFECT_FOCUS_DONE);
    assert(model.state == DESK_REMINDER_STATE_WAITING);
    assert(model.phase == DESK_REMINDER_PHASE_SHORT_BREAK);
    assert(model.completed_focus_count == 1);

    assert(desk_reminder_logic_apply(&model, DESK_REMINDER_ACTION_SNOOZE,
                                     &CONFIG, 2000000000LL));
    generation = model.generation;
    assert(desk_reminder_logic_expire(&model, generation - 1, &CONFIG,
                                      model.deadline_us) ==
           DESK_REMINDER_EFFECT_NONE);
    assert(desk_reminder_logic_expire(&model, generation, &CONFIG,
                                      model.deadline_us) ==
           DESK_REMINDER_EFFECT_SNOOZE_DONE);
    assert(model.alarm_reason == DESK_REMINDER_ALARM_FOCUS_DONE);

    assert(desk_reminder_logic_apply(&model, DESK_REMINDER_ACTION_START_BREAK,
                                     &CONFIG, 0));
    assert(desk_reminder_logic_apply(&model, DESK_REMINDER_ACTION_PAUSE,
                                     &CONFIG, 61000000));
    assert(model.paused_remaining_sec == 239);
    assert(desk_reminder_logic_apply(&model, DESK_REMINDER_ACTION_RESUME,
                                     &CONFIG, 100000000LL));
    assert(desk_reminder_logic_remaining_sec(&model, 100000000LL) == 239);
    assert(desk_reminder_logic_apply(&model, DESK_REMINDER_ACTION_SKIP,
                                     &CONFIG, 100000000LL));
    assert(model.phase == DESK_REMINDER_PHASE_FOCUS);

    /* 跳过专注不增加完成数；第 4 次实际到期才选择长休息。 */
    model.completed_focus_count = 3;
    assert(desk_reminder_logic_apply(&model, DESK_REMINDER_ACTION_START_FOCUS,
                                     &CONFIG, 0));
    assert(desk_reminder_logic_apply(&model, DESK_REMINDER_ACTION_SKIP,
                                     &CONFIG, 1));
    assert(model.completed_focus_count == 3);
    model.state = DESK_REMINDER_STATE_WAITING;
    model.phase = DESK_REMINDER_PHASE_FOCUS;
    assert(desk_reminder_logic_apply(&model, DESK_REMINDER_ACTION_START_FOCUS,
                                     &CONFIG, 0));
    generation = model.generation;
    assert(desk_reminder_logic_expire(&model, generation, &CONFIG,
                                      model.deadline_us) ==
           DESK_REMINDER_EFFECT_FOCUS_DONE);
    assert(model.phase == DESK_REMINDER_PHASE_LONG_BREAK);

    assert(desk_reminder_logic_apply(&model, DESK_REMINDER_ACTION_STOP,
                                     &CONFIG, 0));
    assert(model.state == DESK_REMINDER_STATE_IDLE);
    assert(model.completed_focus_count == 0);

    desk_reminder_config_t invalid = CONFIG;
    invalid.focus_minutes = 0;
    assert(!desk_reminder_config_valid(&invalid));
    puts("desk_reminder_logic_test: ok");
    return 0;
}
