/**
 * @file yourdesk_preset_logic_test.c
 * @brief Host regression vectors for fixed-height preset control.
 */
#include "yourdesk_preset_logic.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    /* Feedback accepts the imperial endpoint without lowering preset limits. */
    assert(YOURDESK_HEIGHT_FEEDBACK_MIN_MM == 635);
    assert(YOURDESK_HEIGHT_MIN_MM == 640);
    assert(yourdesk_height_transition_valid(
        -1, 635, 0, YOURDESK_PRESET_STOP, false, 35, 20));
    assert(yourdesk_height_transition_valid(
        635, 648, 400, YOURDESK_PRESET_UP, false, 35, 20));
    assert(!yourdesk_height_transition_valid(
        -1, 634, 0, YOURDESK_PRESET_STOP, false, 35, 20));

    assert(yourdesk_preset_target_mm(1, 650, 1050) == 650);
    assert(yourdesk_preset_target_mm(4, 650, 1050) == 1050);
    assert(yourdesk_preset_target_mm(2, 650, 1050) == -1);
    assert(yourdesk_preset_bootstrap_direction(1) == YOURDESK_PRESET_DOWN);
    assert(yourdesk_preset_bootstrap_direction(4) == YOURDESK_PRESET_STOP);

    assert(yourdesk_preset_direction(700, 1020, 5) == YOURDESK_PRESET_UP);
    assert(yourdesk_preset_direction(800, 640, 5) == YOURDESK_PRESET_DOWN);
    assert(yourdesk_preset_direction(638, 640, 5) == YOURDESK_PRESET_STOP);

    assert(!yourdesk_preset_reached(1014, 1020, 5, YOURDESK_PRESET_UP));
    assert(yourdesk_preset_reached(1015, 1020, 5, YOURDESK_PRESET_UP));
    assert(!yourdesk_preset_reached(646, 640, 5, YOURDESK_PRESET_DOWN));
    assert(yourdesk_preset_reached(645, 640, 5, YOURDESK_PRESET_DOWN));

    /* The captured upward 89 -> 87 regression must never replace height. */
    assert(!yourdesk_height_transition_valid(
        800, 890, 1150, YOURDESK_PRESET_UP, false, 35, 20));
    assert(yourdesk_height_transition_valid(
        800, 870, 2680, YOURDESK_PRESET_UP, false, 35, 20));
    assert(!yourdesk_height_transition_valid(
        890, 870, 1530, YOURDESK_PRESET_UP, false, 35, 20));
    assert(!yourdesk_height_transition_valid(
        670, 690, 400, YOURDESK_PRESET_DOWN, false, 35, 20));

    /*
     * Regression from the real desk: a false 64 cm baseline must not reject
     * the first real frame after a new motion, then normal DOWN tracking resumes.
     */
    assert(yourdesk_height_transition_valid(
        640, 1030, 7340, YOURDESK_PRESET_UP, true, 35, 20));
    assert(yourdesk_height_transition_valid(
        1030, 1050, 1320, YOURDESK_PRESET_DOWN, true, 35, 20));
    assert(yourdesk_height_transition_valid(
        1050, 1040, 310, YOURDESK_PRESET_DOWN, false, 35, 20));
    assert(!yourdesk_height_transition_valid(
        1030, 500, 100, YOURDESK_PRESET_DOWN, true, 35, 20));

    puts("yourdesk preset logic vectors: OK");
    return 0;
}
