/**
 * @file mxtark_preset_logic.c
 * @brief Configurable preset mapping and stop-boundary decisions.
 */
#include "mxtark_preset_logic.h"

#include <limits.h>

int mxtark_preset_target_mm(uint8_t preset, int preset1_height_mm,
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

mxtark_preset_direction_t mxtark_preset_bootstrap_direction(uint8_t preset)
{
    return preset == 1 ? MXTARK_PRESET_DOWN : MXTARK_PRESET_STOP;
}

mxtark_preset_direction_t mxtark_preset_direction(
    int current_mm, int target_mm, int stop_margin_mm)
{
    if (current_mm >= target_mm - stop_margin_mm &&
        current_mm <= target_mm + stop_margin_mm) {
        return MXTARK_PRESET_STOP;
    }
    return current_mm < target_mm ? MXTARK_PRESET_UP : MXTARK_PRESET_DOWN;
}

bool mxtark_preset_reached(int current_mm, int target_mm, int stop_margin_mm,
                            mxtark_preset_direction_t direction)
{
    if (direction == MXTARK_PRESET_UP) {
        return current_mm >= target_mm - stop_margin_mm;
    }
    if (direction == MXTARK_PRESET_DOWN) {
        return current_mm <= target_mm + stop_margin_mm;
    }
    return true;
}

void mxtark_preset_control_reset(mxtark_preset_control_t *control)
{
    if (!control) {
        return;
    }
    *control = (mxtark_preset_control_t){
        .target_mm = -1,
        .direction = MXTARK_PRESET_STOP,
        .phase = MXTARK_PRESET_PHASE_IDLE,
    };
}

void mxtark_preset_control_start(mxtark_preset_control_t *control,
                                 int target_mm,
                                 mxtark_preset_direction_t direction,
                                 uint32_t current_sample_id)
{
    if (!control) {
        return;
    }
    *control = (mxtark_preset_control_t){
        .target_mm = target_mm,
        .direction = direction,
        .phase = MXTARK_PRESET_PHASE_MOVING,
        .last_sample_id = current_sample_id,
    };
}

mxtark_preset_action_t mxtark_preset_control_update(
    mxtark_preset_control_t *control,
    int control_height_mm,
    int stable_height_mm,
    uint32_t sample_id,
    uint32_t now_ms,
    int stop_margin_mm,
    int settle_tolerance_mm,
    uint32_t settle_ms,
    unsigned int max_corrections)
{
    if (!control || control->phase == MXTARK_PRESET_PHASE_IDLE ||
        control_height_mm < 0 || stable_height_mm < 0 ||
        stop_margin_mm < 0 || settle_tolerance_mm < 0 ||
        sample_id == control->last_sample_id) {
        return MXTARK_PRESET_ACTION_NONE;
    }
    control->last_sample_id = sample_id;

    if (control->phase == MXTARK_PRESET_PHASE_MOVING) {
        if (!mxtark_preset_reached(control_height_mm, control->target_mm,
                                   stop_margin_mm, control->direction)) {
            return MXTARK_PRESET_ACTION_NONE;
        }
        control->phase = MXTARK_PRESET_PHASE_SETTLING;
        control->direction = MXTARK_PRESET_STOP;
        control->settle_started_ms = now_ms;
        return MXTARK_PRESET_ACTION_STOP_AND_SETTLE;
    }

    if ((uint32_t)(now_ms - control->settle_started_ms) < settle_ms) {
        return MXTARK_PRESET_ACTION_NONE;
    }
    int error_mm = stable_height_mm - control->target_mm;
    int error_magnitude = error_mm < 0 ? -error_mm : error_mm;
    if (error_magnitude <= settle_tolerance_mm) {
        mxtark_preset_control_reset(control);
        return MXTARK_PRESET_ACTION_COMPLETE;
    }
    if (control->correction_count >= max_corrections) {
        mxtark_preset_control_reset(control);
        return MXTARK_PRESET_ACTION_CORRECTION_LIMIT;
    }

    control->correction_count++;
    control->phase = MXTARK_PRESET_PHASE_MOVING;
    control->direction = error_mm < 0 ? MXTARK_PRESET_UP
                                     : MXTARK_PRESET_DOWN;
    return control->direction == MXTARK_PRESET_UP
               ? MXTARK_PRESET_ACTION_MOVE_UP
               : MXTARK_PRESET_ACTION_MOVE_DOWN;
}

bool mxtark_tof_upward_blocked(bool height_known, int height_mm,
                                 bool right_gap_known, int right_gap_mm,
                                 int max_height_mm)
{
    if (!height_known || height_mm >= max_height_mm) {
        return true;
    }
    if (height_mm >= MXTARK_TOF_OBSTACLE_HEIGHT_MM) {
        return false;
    }
    return !right_gap_known || right_gap_mm < MXTARK_TOF_MIN_RIGHT_GAP_MM;
}

bool mxtark_height_transition_valid(
    int previous_mm, int candidate_mm, int elapsed_ms,
    mxtark_preset_direction_t direction, bool resync_pending,
    int max_speed_mm_per_s, int step_slack_mm)
{
    if (candidate_mm < MXTARK_HEIGHT_FEEDBACK_MIN_MM ||
        candidate_mm > MXTARK_HEIGHT_MAX_MM) {
        return false;
    }
    if (previous_mm < 0 || resync_pending) {
        return true;
    }
    if (elapsed_ms < 0 || max_speed_mm_per_s <= 0 || step_slack_mm < 0) {
        return false;
    }

    int delta = candidate_mm - previous_mm;
    if (direction == MXTARK_PRESET_UP && delta < 0) {
        return false;
    }
    if (direction == MXTARK_PRESET_DOWN && delta > 0) {
        return false;
    }

    int magnitude = delta < 0 ? -delta : delta;
    int64_t max_delta_64 = (int64_t)step_slack_mm +
                           ((int64_t)elapsed_ms * max_speed_mm_per_s + 999) /
                               1000;
    int max_delta = max_delta_64 > INT_MAX ? INT_MAX : (int)max_delta_64;
    return magnitude <= max_delta;
}
