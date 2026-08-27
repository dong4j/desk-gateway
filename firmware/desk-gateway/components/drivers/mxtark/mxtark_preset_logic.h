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

/* 低位留出更大侧距；全行程仍保留不可绕过的最小安全间距。 */
#define MXTARK_TOF_LOW_HEIGHT_MM            800
#define MXTARK_TOF_MIN_RIGHT_GAP_MM         70
#define MXTARK_TOF_LOW_MIN_RIGHT_GAP_MM     100

typedef enum {
    MXTARK_PRESET_STOP = 0,
    MXTARK_PRESET_UP = 1,
    MXTARK_PRESET_DOWN = -1,
} mxtark_preset_direction_t;

typedef enum {
    MXTARK_PRESET_PHASE_IDLE = 0,
    MXTARK_PRESET_PHASE_MOVING,
    MXTARK_PRESET_PHASE_SETTLING,
} mxtark_preset_phase_t;

typedef enum {
    MXTARK_PRESET_ACTION_NONE = 0,
    MXTARK_PRESET_ACTION_STOP_AND_SETTLE,
    MXTARK_PRESET_ACTION_MOVE_UP,
    MXTARK_PRESET_ACTION_MOVE_DOWN,
    MXTARK_PRESET_ACTION_COMPLETE,
    MXTARK_PRESET_ACTION_CORRECTION_LIMIT,
    MXTARK_PRESET_ACTION_NO_PROGRESS,
} mxtark_preset_action_t;

/** 纯逻辑层的上升保护原因；运行时再映射到厂商无关 Driver 枚举。 */
typedef enum {
    MXTARK_TOF_BLOCK_NONE = 0,
    MXTARK_TOF_BLOCK_HEIGHT_UNKNOWN,
    MXTARK_TOF_BLOCK_RIGHT_GAP_UNKNOWN,
    MXTARK_TOF_BLOCK_MAX_HEIGHT,
    MXTARK_TOF_BLOCK_RIGHT_GAP,
} mxtark_tof_block_reason_t;

/**
 * 档位闭环的纯状态。运行时只在驱动控制任务中推进，主机测试用相同逻辑
 * 回放测距延迟和停车过冲，避免只测试一个静态阈值。
 */
typedef struct {
    int target_mm;
    mxtark_preset_direction_t direction;
    mxtark_preset_phase_t phase;
    unsigned int correction_count;
    uint32_t settle_started_ms;
    uint32_t last_sample_id;
    int progress_height_mm;
    uint32_t progress_started_ms;
} mxtark_preset_control_t;

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

/** 清空目标与校正次数，使后续手动命令不会被旧档位状态停车。 */
void mxtark_preset_control_reset(mxtark_preset_control_t *control);

/** 启动一段已确定方向的档位运动，并跳过启动前的旧测距样本。 */
void mxtark_preset_control_start(mxtark_preset_control_t *control,
                                 int target_mm,
                                 mxtark_preset_direction_t direction,
                                 uint32_t current_sample_id);

/**
 * 消费一组新的控制/稳定高度并返回驱动动作。
 *
 * 首次越过停车边界后进入稳定等待；稳定高度仍超出容差时最多反向校正
 * max_corrections 次。下降进入目标附近后若持续没有有效进展，也必须结束，
 * 避免机械最低位高于配置目标时一直保持下降命令。
 */
mxtark_preset_action_t mxtark_preset_control_update(
    mxtark_preset_control_t *control,
    int control_height_mm,
    int stable_height_mm,
    uint32_t sample_id,
    uint32_t now_ms,
    int stop_margin_mm,
    int settle_tolerance_mm,
    uint32_t settle_ms,
    unsigned int max_corrections,
    int no_progress_near_target_mm,
    int no_progress_min_delta_mm,
    uint32_t no_progress_timeout_ms);

/**
 * 判断当前 TOF 数据是否必须阻止上升。
 *
 * 高度未知时无法保证 90 cm 上限，必须阻止上升。右侧距离未知或全行程
 * 小于 7 cm 时也必须阻止；高度低于 80 cm 时要求至少 10 cm。
 */
bool mxtark_tof_upward_blocked(bool height_known, int height_mm,
                                 bool right_gap_known, int right_gap_mm,
                                 int max_height_mm);

/** 返回与上升门禁完全相同的可解释原因，禁止提示层自行复制阈值。 */
mxtark_tof_block_reason_t mxtark_tof_upward_block_reason(
    bool height_known, int height_mm,
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
