/**
 * @file desk_motion_watch.c
 * @brief 桌体无位移诊断的纯状态机实现。
 */
#include "desk_motion_watch.h"

#include <stdlib.h>
#include <string.h>

/* 过滤 ToF 静态抖动；正常桌体在诊断窗口内应明显超过该位移。 */
#define DESK_MOTION_PROGRESS_MM 8
#define DESK_MOTION_STALL_WINDOW_MS 2500U
#define DESK_MOTION_BOUNDARY_MARGIN_MM 10

void desk_motion_watch_reset(desk_motion_watch_t *watch)
{
    if (watch) {
        memset(watch, 0, sizeof(*watch));
    }
}

static bool at_manual_boundary(desk_motion_watch_kind_t kind, int height_mm,
                               int minimum_height_mm, int maximum_height_mm)
{
    return (kind == DESK_MOTION_WATCH_UP &&
            height_mm >= maximum_height_mm - DESK_MOTION_BOUNDARY_MARGIN_MM) ||
           (kind == DESK_MOTION_WATCH_DOWN &&
            height_mm <= minimum_height_mm + DESK_MOTION_BOUNDARY_MARGIN_MM);
}

static bool made_progress(desk_motion_watch_kind_t kind, int baseline_mm,
                          int height_mm)
{
    if (kind == DESK_MOTION_WATCH_UP) {
        return height_mm - baseline_mm >= DESK_MOTION_PROGRESS_MM;
    }
    if (kind == DESK_MOTION_WATCH_DOWN) {
        return baseline_mm - height_mm >= DESK_MOTION_PROGRESS_MM;
    }
    return abs(height_mm - baseline_mm) >= DESK_MOTION_PROGRESS_MM;
}

desk_motion_watch_result_t desk_motion_watch_update(
    desk_motion_watch_t *watch, desk_motion_watch_kind_t kind,
    bool height_known, int height_mm, int minimum_height_mm,
    int maximum_height_mm, uint32_t now_ms)
{
    if (!watch) {
        return DESK_MOTION_WATCH_NO_CHANGE;
    }
    if (kind == DESK_MOTION_WATCH_IDLE || !height_known ||
        at_manual_boundary(kind, height_mm, minimum_height_mm,
                           maximum_height_mm)) {
        desk_motion_watch_reset(watch);
        return DESK_MOTION_WATCH_NO_CHANGE;
    }

    if (!watch->tracking || watch->kind != kind) {
        watch->kind = kind;
        watch->tracking = true;
        watch->baseline_height_mm = height_mm;
        watch->started_ms = now_ms;
        watch->progress_seen = false;
        watch->stalled_reported = false;
        return DESK_MOTION_WATCH_NO_CHANGE;
    }

    if (watch->progress_seen) {
        return DESK_MOTION_WATCH_PROGRESS;
    }
    if (made_progress(kind, watch->baseline_height_mm, height_mm)) {
        watch->progress_seen = true;
        return DESK_MOTION_WATCH_PROGRESS;
    }
    if (!watch->stalled_reported &&
        now_ms - watch->started_ms >= DESK_MOTION_STALL_WINDOW_MS) {
        watch->stalled_reported = true;
        return DESK_MOTION_WATCH_STALLED;
    }
    return DESK_MOTION_WATCH_NO_CHANGE;
}
