/**
 * @file yourdesk_preset_logic.h
 * @brief Pure preset-height decisions shared by firmware and host tests.
 *
 * This module has no ESP-IDF dependency so target mapping and stop boundaries
 * can be verified without connecting a real desk.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Confirmed display range of the current YourDesk control box. */
#define YOURDESK_HEIGHT_MIN_MM 640
#define YOURDESK_HEIGHT_MAX_MM 1290

typedef enum {
    YOURDESK_PRESET_STOP = 0,
    YOURDESK_PRESET_UP = 1,
    YOURDESK_PRESET_DOWN = -1,
} yourdesk_preset_direction_t;

/** Return the configured target in millimetres, or -1 for an unsupported preset. */
int yourdesk_preset_target_mm(uint8_t preset, int preset1_height_mm,
                              int preset4_height_mm);

/**
 * Return the only safe bootstrap direction when height is unknown.
 * Preset 1 is the confirmed low display endpoint for this desk. All higher
 * presets must stay blocked until height is known.
 */
yourdesk_preset_direction_t yourdesk_preset_bootstrap_direction(uint8_t preset);

/** Clamp a requested preset to the configured physical ceiling. */
int yourdesk_preset_limit_target_mm(int requested_mm, int max_height_mm);

/** True when upward travel must stop early to avoid crossing the ceiling. */
bool yourdesk_max_height_reached(int current_mm, int max_height_mm,
                                int stop_margin_mm);

/** Release a latched upward block only after DOWN reports a safe height. */
bool yourdesk_up_latch_can_clear(
    int height_mm, int max_height_mm, int stop_margin_mm,
    yourdesk_preset_direction_t direction);

/** Select initial travel direction; STOP means the current height is in range. */
yourdesk_preset_direction_t yourdesk_preset_direction(
    int current_mm, int target_mm, int stop_margin_mm);

/** Check the one-way stop boundary without reversing after small overshoot. */
bool yourdesk_preset_reached(int current_mm, int target_mm, int stop_margin_mm,
                            yourdesk_preset_direction_t direction);

/**
 * Validate one decoded controller height.
 *
 * The first frame of a new motion is an authoritative resynchronisation point;
 * direction and speed checks apply only to subsequent frames in that motion.
 */
bool yourdesk_height_transition_valid(
    int previous_mm, int candidate_mm, int elapsed_ms,
    yourdesk_preset_direction_t direction, bool resync_pending,
    int max_speed_mm_per_s, int step_slack_mm);

/** Conservative upward position envelope used between sparse display frames. */
int yourdesk_projected_up_height_mm(int anchor_mm, int elapsed_ms,
                                   int max_speed_mm_per_s);
