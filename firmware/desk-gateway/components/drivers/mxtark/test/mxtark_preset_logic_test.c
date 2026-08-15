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

    assert(!mxtark_preset_reached(1014, 1020, 5, MXTARK_PRESET_UP));
    assert(mxtark_preset_reached(1015, 1020, 5, MXTARK_PRESET_UP));
    assert(!mxtark_preset_reached(646, 640, 5, MXTARK_PRESET_DOWN));
    assert(mxtark_preset_reached(645, 640, 5, MXTARK_PRESET_DOWN));

    /* 右侧障碍仅在高度严格低于 80 cm 时阻止上升。 */
    assert(mxtark_tof_upward_blocked(false, -1, true, 100, 940));
    assert(mxtark_tof_upward_blocked(true, 799, true, 79, 940));
    assert(!mxtark_tof_upward_blocked(true, 799, true, 80, 940));
    assert(mxtark_tof_upward_blocked(true, 799, false, -1, 940));
    assert(!mxtark_tof_upward_blocked(true, 800, true, 79, 940));
    assert(!mxtark_tof_upward_blocked(true, 800, false, -1, 940));
    assert(!mxtark_tof_upward_blocked(true, 939, true, 10, 940));
    assert(mxtark_tof_upward_blocked(true, 940, true, 100, 940));

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
