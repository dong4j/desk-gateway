/**
 * @file mxtark_preset_logic_test.c
 * @brief Host regression vectors for fixed-height preset control.
 */
#include "mxtark_preset_logic.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    /* Feedback accepts the imperial endpoint without lowering preset limits. */
    assert(MXTARK_HEIGHT_FEEDBACK_MIN_MM == 635);
    assert(MXTARK_HEIGHT_MIN_MM == 640);
    assert(mxtark_height_transition_valid(
        -1, 635, 0, MXTARK_PRESET_STOP, false, 35, 20));
    assert(mxtark_height_transition_valid(
        635, 648, 400, MXTARK_PRESET_UP, false, 35, 20));
    assert(!mxtark_height_transition_valid(
        -1, 634, 0, MXTARK_PRESET_STOP, false, 35, 20));

    assert(mxtark_preset_target_mm(1, 650, 1050) == 650);
    assert(mxtark_preset_target_mm(4, 650, 1050) == 1050);
    assert(mxtark_preset_target_mm(2, 650, 1050) == -1);
    assert(mxtark_preset_bootstrap_direction(1) == MXTARK_PRESET_DOWN);
    assert(mxtark_preset_bootstrap_direction(4) == MXTARK_PRESET_STOP);

    assert(mxtark_preset_direction(700, 1020, 5) == MXTARK_PRESET_UP);
    assert(mxtark_preset_direction(800, 640, 5) == MXTARK_PRESET_DOWN);
    assert(mxtark_preset_direction(638, 640, 5) == MXTARK_PRESET_STOP);
    /* 6 mm 静止抖动可越过旧 5 mm 边界，但不应触发同档位反向点动。 */
    assert(mxtark_preset_direction(876, 870, 5) == MXTARK_PRESET_DOWN);
    assert(mxtark_preset_direction(876, 870, 8) == MXTARK_PRESET_STOP);

    assert(!mxtark_preset_reached(1014, 1020, 5, MXTARK_PRESET_UP));
    assert(mxtark_preset_reached(1015, 1020, 5, MXTARK_PRESET_UP));
    assert(!mxtark_preset_reached(646, 640, 5, MXTARK_PRESET_DOWN));
    assert(mxtark_preset_reached(645, 640, 5, MXTARK_PRESET_DOWN));

    /* 上升过冲后必须先稳定、有限反向校正，再在容差内结束。 */
    mxtark_preset_control_t control;
    mxtark_preset_control_reset(&control);
    mxtark_preset_control_start(&control, 870, MXTARK_PRESET_UP, 10);
    assert(mxtark_preset_control_update(
               &control, 864, 858, 11, 100, 5, 5, 500, 2, 20, 3, 1500) ==
           MXTARK_PRESET_ACTION_NONE);
    assert(mxtark_preset_control_update(
               &control, 866, 860, 12, 200, 5, 5, 500, 2, 20, 3, 1500) ==
           MXTARK_PRESET_ACTION_STOP_AND_SETTLE);
    assert(mxtark_preset_control_update(
               &control, 875, 876, 13, 500, 5, 5, 500, 2, 20, 3, 1500) ==
           MXTARK_PRESET_ACTION_NONE);
    assert(mxtark_preset_control_update(
               &control, 876, 876, 14, 700, 5, 5, 500, 2, 20, 3, 1500) ==
           MXTARK_PRESET_ACTION_MOVE_DOWN);
    assert(mxtark_preset_control_update(
               &control, 873, 875, 15, 800, 5, 5, 500, 2, 20, 3, 1500) ==
           MXTARK_PRESET_ACTION_STOP_AND_SETTLE);
    assert(mxtark_preset_control_update(
               &control, 871, 872, 16, 1300, 5, 5, 500, 2, 20, 3, 1500) ==
           MXTARK_PRESET_ACTION_COMPLETE);
    assert(control.phase == MXTARK_PRESET_PHASE_IDLE);

    /* 同一样本不能被 50 ms 控制循环重复消费并提前结束稳定等待。 */
    mxtark_preset_control_start(&control, 870, MXTARK_PRESET_UP, 20);
    assert(mxtark_preset_control_update(
               &control, 866, 860, 21, 100, 5, 5, 500, 2, 20, 3, 1500) ==
           MXTARK_PRESET_ACTION_STOP_AND_SETTLE);
    assert(mxtark_preset_control_update(
               &control, 866, 870, 21, 700, 5, 5, 500, 2, 20, 3, 1500) ==
           MXTARK_PRESET_ACTION_NONE);

    /* 连续过冲达到校正上限后必须结束，不能在目标两侧无限振荡。 */
    assert(mxtark_preset_control_update(
               &control, 878, 878, 22, 700, 5, 5, 500, 2, 20, 3, 1500) ==
           MXTARK_PRESET_ACTION_MOVE_DOWN);
    assert(mxtark_preset_control_update(
               &control, 874, 878, 23, 800, 5, 5, 500, 2, 20, 3, 1500) ==
           MXTARK_PRESET_ACTION_STOP_AND_SETTLE);
    assert(mxtark_preset_control_update(
               &control, 862, 862, 24, 1300, 5, 5, 500, 2, 20, 3, 1500) ==
           MXTARK_PRESET_ACTION_MOVE_UP);
    assert(mxtark_preset_control_update(
               &control, 866, 862, 25, 1400, 5, 5, 500, 2, 20, 3, 1500) ==
           MXTARK_PRESET_ACTION_STOP_AND_SETTLE);
    assert(mxtark_preset_control_update(
               &control, 879, 879, 26, 1900, 5, 5, 500, 2, 20, 3, 1500) ==
           MXTARK_PRESET_ACTION_CORRECTION_LIMIT);
    assert(control.phase == MXTARK_PRESET_PHASE_IDLE);

    /* 目标附近仍在缓慢下降时持续刷新进展窗口，不能提前误停。 */
    mxtark_preset_control_start(&control, 550, MXTARK_PRESET_DOWN, 90);
    assert(mxtark_preset_control_update(
               &control, 568, 568, 91, 100, 5, 5, 500, 2, 20, 3, 1500) ==
           MXTARK_PRESET_ACTION_NONE);
    assert(mxtark_preset_control_update(
               &control, 565, 565, 92, 1500, 5, 5, 500, 2, 20, 3, 1500) ==
           MXTARK_PRESET_ACTION_NONE);
    assert(mxtark_preset_control_update(
               &control, 562, 562, 93, 2900, 5, 5, 500, 2, 20, 3, 1500) ==
           MXTARK_PRESET_ACTION_NONE);
    assert(mxtark_preset_control_update(
               &control, 559, 559, 94, 4300, 5, 5, 500, 2, 20, 3, 1500) ==
           MXTARK_PRESET_ACTION_NONE);
    assert(mxtark_preset_control_update(
               &control, 554, 554, 95, 5700, 5, 5, 500, 2, 20, 3, 1500) ==
           MXTARK_PRESET_ACTION_STOP_AND_SETTLE);

    /*
     * 真桌最低位稳定在约 559 mm，而坐姿目标仍是 550 mm。第一次停车后
     * 的向下校正若 1.5 秒没有至少 3 mm 进展，必须主动结束，不能等待
     * desk_core 的 15 秒总超时。
     */
    mxtark_preset_control_start(&control, 550, MXTARK_PRESET_DOWN, 100);
    assert(mxtark_preset_control_update(
               &control, 554, 561, 101, 100, 5, 5, 500, 2, 20, 3, 1500) ==
           MXTARK_PRESET_ACTION_STOP_AND_SETTLE);
    assert(mxtark_preset_control_update(
               &control, 559, 561, 102, 600, 5, 5, 500, 2, 20, 3, 1500) ==
           MXTARK_PRESET_ACTION_MOVE_DOWN);
    assert(mxtark_preset_control_update(
               &control, 559, 559, 103, 700, 5, 5, 500, 2, 20, 3, 1500) ==
           MXTARK_PRESET_ACTION_NONE);
    assert(mxtark_preset_control_update(
               &control, 558, 559, 104, 1400, 5, 5, 500, 2, 20, 3, 1500) ==
           MXTARK_PRESET_ACTION_NONE);
    assert(mxtark_preset_control_update(
               &control, 558, 559, 105, 2200, 5, 5, 500, 2, 20, 3, 1500) ==
           MXTARK_PRESET_ACTION_NO_PROGRESS);
    assert(control.phase == MXTARK_PRESET_PHASE_IDLE);

    /* 全行程小于 7 cm 都阻止上升，低于 80 cm 时扩展到 10 cm。 */
    assert(mxtark_tof_upward_blocked(false, -1, true, 100, 900));
    assert(mxtark_tof_upward_block_reason(false, -1, true, 100, 900) ==
           MXTARK_TOF_BLOCK_HEIGHT_UNKNOWN);
    assert(mxtark_tof_upward_blocked(true, 799, true, 99, 900));
    assert(!mxtark_tof_upward_blocked(true, 799, true, 100, 900));
    assert(mxtark_tof_upward_blocked(true, 799, false, -1, 900));
    assert(mxtark_tof_upward_blocked(true, 800, true, 69, 900));
    assert(!mxtark_tof_upward_blocked(true, 800, true, 70, 900));
    assert(mxtark_tof_upward_blocked(true, 800, false, -1, 900));
    assert(mxtark_tof_upward_block_reason(true, 800, false, -1, 900) ==
           MXTARK_TOF_BLOCK_RIGHT_GAP_UNKNOWN);
    assert(mxtark_tof_upward_blocked(true, 899, true, 69, 900));
    assert(mxtark_tof_upward_block_reason(true, 899, true, 69, 900) ==
           MXTARK_TOF_BLOCK_RIGHT_GAP);
    assert(!mxtark_tof_upward_blocked(true, 899, true, 70, 900));
    assert(mxtark_tof_upward_blocked(true, 900, true, 100, 900));
    assert(mxtark_tof_upward_block_reason(true, 900, true, 100, 900) ==
           MXTARK_TOF_BLOCK_MAX_HEIGHT);

    /* The captured upward 89 -> 87 regression must never replace height. */
    assert(!mxtark_height_transition_valid(
        800, 890, 1150, MXTARK_PRESET_UP, false, 35, 20));
    assert(mxtark_height_transition_valid(
        800, 870, 2680, MXTARK_PRESET_UP, false, 35, 20));
    assert(!mxtark_height_transition_valid(
        890, 870, 1530, MXTARK_PRESET_UP, false, 35, 20));
    assert(!mxtark_height_transition_valid(
        670, 690, 400, MXTARK_PRESET_DOWN, false, 35, 20));

    /*
     * Regression from the real desk: a false 64 cm baseline must not reject
     * the first real frame after a new motion, then normal DOWN tracking resumes.
     */
    assert(mxtark_height_transition_valid(
        640, 1030, 7340, MXTARK_PRESET_UP, true, 35, 20));
    assert(mxtark_height_transition_valid(
        1030, 1050, 1320, MXTARK_PRESET_DOWN, true, 35, 20));
    assert(mxtark_height_transition_valid(
        1050, 1040, 310, MXTARK_PRESET_DOWN, false, 35, 20));
    assert(!mxtark_height_transition_valid(
        1030, 500, 100, MXTARK_PRESET_DOWN, true, 35, 20));

    puts("mxtark preset logic vectors: OK");
    return 0;
}
