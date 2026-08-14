/**
 * @file desk_tof.h
 * @brief 双 ToF 传感器的只读测距服务。
 *
 * TOF050C/VL6180X 提供桌面右侧到墙面的距离，TOF400C/VL53L1X
 * 提供传感器到地面的距离。当前组件只发布实时数据，不参与运动控制。
 */
#pragma once

#include "esp_err.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int height_mm;
    bool height_known;
    int right_gap_mm;
    bool right_gap_known;
} desk_tof_snapshot_t;

/** 启动后台初始化与采样任务；传感器离线不会阻止其他网关功能启动。 */
esp_err_t desk_tof_start(void);

/** 返回最近一组未过期的测距结果；未知值统一为 -1。 */
desk_tof_snapshot_t desk_tof_snapshot(void);

#ifdef __cplusplus
}
#endif
