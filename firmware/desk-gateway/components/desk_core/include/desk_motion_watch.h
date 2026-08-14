/**
 * @file desk_motion_watch.h
 * @brief 基于真实高度样本判断运动指令是否产生了有效位移。
 *
 * 该模块不依赖 ESP-IDF，便于在主机测试中覆盖计时、边界和方向规则。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DESK_MOTION_WATCH_IDLE = 0,
    DESK_MOTION_WATCH_UP,
    DESK_MOTION_WATCH_DOWN,
    DESK_MOTION_WATCH_TARGET,
} desk_motion_watch_kind_t;

typedef enum {
    DESK_MOTION_WATCH_NO_CHANGE = 0,
    DESK_MOTION_WATCH_PROGRESS,
    DESK_MOTION_WATCH_STALLED,
} desk_motion_watch_result_t;

typedef struct {
    desk_motion_watch_kind_t kind;
    bool tracking;
    bool progress_seen;
    bool stalled_reported;
    int baseline_height_mm;
    uint32_t started_ms;
} desk_motion_watch_t;

void desk_motion_watch_reset(desk_motion_watch_t *watch);

/**
 * 合并一个高度样本，并在持续无有效位移时只报告一次 STALLED。
 *
 * 手动向上/向下在对应物理边界附近不会报错；目标档位由驱动先判断是否
 * 已到达，因此仍参与无位移诊断。
 */
desk_motion_watch_result_t desk_motion_watch_update(
    desk_motion_watch_t *watch, desk_motion_watch_kind_t kind,
    bool height_known, int height_mm, int minimum_height_mm,
    int maximum_height_mm, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
