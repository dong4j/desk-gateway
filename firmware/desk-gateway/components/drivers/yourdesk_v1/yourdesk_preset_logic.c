/**
 * @file yourdesk_preset_logic.c
 * @brief Configurable preset mapping and stop-boundary decisions.
 */
#include "yourdesk_preset_logic.h"

#include <limits.h>

int yourdesk_preset_target_mm(uint8_t preset, int preset1_height_mm,
                              int preset4_height_mm)
{
    if (preset == 1) {
        return preset1_height_mm;
    }
    if (preset == 4) {
        return preset4_height_mm;
    }
    return -1;
}

yourdesk_preset_direction_t yourdesk_preset_bootstrap_direction(uint8_t preset)
{
    return preset == 1 ? YOURDESK_PRESET_DOWN : YOURDESK_PRESET_STOP;
}

yourdesk_preset_direction_t yourdesk_preset_direction(
    int current_mm, int target_mm, int stop_margin_mm)
{
    if (current_mm >= target_mm - stop_margin_mm &&
        current_mm <= target_mm + stop_margin_mm) {
        return YOURDESK_PRESET_STOP;
    }
    return current_mm < target_mm ? YOURDESK_PRESET_UP : YOURDESK_PRESET_DOWN;
}

bool yourdesk_preset_reached(int current_mm, int target_mm, int stop_margin_mm,
                            yourdesk_preset_direction_t direction)
{
    if (direction == YOURDESK_PRESET_UP) {
        return current_mm >= target_mm - stop_margin_mm;
    }
    if (direction == YOURDESK_PRESET_DOWN) {
        return current_mm <= target_mm + stop_margin_mm;
    }
    return true;
}

bool yourdesk_tof_upward_blocked(bool height_known, int height_mm,
                                 bool right_gap_known, int right_gap_mm,
                                 int max_height_mm)
{
    if (!height_known || height_mm >= max_height_mm) {
        return true;
    }
    if (height_mm >= YOURDESK_TOF_OBSTACLE_HEIGHT_MM) {
        return false;
    }
    return !right_gap_known || right_gap_mm < YOURDESK_TOF_MIN_RIGHT_GAP_MM;
}

bool yourdesk_height_transition_valid(
    int previous_mm, int candidate_mm, int elapsed_ms,
    yourdesk_preset_direction_t direction, bool resync_pending,
    int max_speed_mm_per_s, int step_slack_mm)
{
    if (candidate_mm < YOURDESK_HEIGHT_FEEDBACK_MIN_MM ||
        candidate_mm > YOURDESK_HEIGHT_MAX_MM) {
        return false;
    }
    if (previous_mm < 0 || resync_pending) {
        return true;
    }
    if (elapsed_ms < 0 || max_speed_mm_per_s <= 0 || step_slack_mm < 0) {
        return false;
    }

    int delta = candidate_mm - previous_mm;
    if (direction == YOURDESK_PRESET_UP && delta < 0) {
        return false;
    }
    if (direction == YOURDESK_PRESET_DOWN && delta > 0) {
        return false;
    }

    int magnitude = delta < 0 ? -delta : delta;
    int64_t max_delta_64 = (int64_t)step_slack_mm +
                           ((int64_t)elapsed_ms * max_speed_mm_per_s + 999) /
                               1000;
    int max_delta = max_delta_64 > INT_MAX ? INT_MAX : (int)max_delta_64;
    return magnitude <= max_delta;
}
