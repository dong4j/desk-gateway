/**
 * @file mxtark_preset_logic.h
 * @brief Pure preset-height decisions shared by firmware and host tests.
 *
 * This module has no ESP-IDF dependency so target mapping and stop boundaries
 * can be verified without connecting a real desk.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * The metric preset endpoint is 640 mm, while the same physical minimum is
 * displayed as 25.0 in and converts to 635 mm.  Keep feedback validity
 * separate from configurable preset limits so both unit modes remain usable.
 */
#define MXTARK_HEIGHT_FEEDBACK_MIN_MM 635
#define MXTARK_HEIGHT_MIN_MM 640
#define MXTARK_HEIGHT_MAX_MM 1290
/* Fragmented display transitions may legitimately arrive in larger batches. */
#define MXTARK_HEIGHT_TRANSITION_MAX_SPEED_MM_PER_S 35

/* TOF400C 直接读数低于此高度时，TOF050C 才参与右侧障碍判断。 */
#define MXTARK_TOF_OBSTACLE_HEIGHT_MM 800
#define MXTARK_TOF_MIN_RIGHT_GAP_MM   80

typedef enum {
    MXTARK_PRESET_STOP = 0,
    MXTARK_PRESET_UP = 1,
    MXTARK_PRESET_DOWN = -1,
} mxtark_preset_direction_t;

/** Return the configured target in millimetres, or -1 for an unsupported preset. */
int mxtark_preset_target_mm(uint8_t preset, int preset1_height_mm,
                              int preset4_height_mm);

/**
 * Return the only safe bootstrap direction when height is unknown.
 * Preset 1 is the confirmed low display endpoint for this desk. All higher
 * presets must stay blocked until height is known.
 */
mxtark_preset_direction_t mxtark_preset_bootstrap_direction(uint8_t preset);

/** Select initial travel direction; STOP means the current height is in range. */
mxtark_preset_direction_t mxtark_preset_direction(
    int current_mm, int target_mm, int stop_margin_mm);

/** Check the one-way stop boundary without reversing after small overshoot. */
bool mxtark_preset_reached(int current_mm, int target_mm, int stop_margin_mm,
                            mxtark_preset_direction_t direction);

/**
 * 判断当前 TOF 数据是否必须阻止上升。
 *
 * 高度未知时无法保证 94 cm 上限，必须阻止上升。高度低于 80 cm 时，
 * 右侧距离未知或小于 8 cm 也必须阻止；达到 80 cm 后不再使用右侧距离。
 */
bool mxtark_tof_upward_blocked(bool height_known, int height_mm,
                                 bool right_gap_known, int right_gap_mm,
                                 int max_height_mm);

/**
 * Validate one decoded controller height.
 *
 * The first frame of a new motion is an authoritative resynchronisation point;
 * direction and speed checks apply only to subsequent frames in that motion.
 */
bool mxtark_height_transition_valid(
    int previous_mm, int candidate_mm, int elapsed_ms,
    mxtark_preset_direction_t direction, bool resync_pending,
    int max_speed_mm_per_s, int step_slack_mm);
