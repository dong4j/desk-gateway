/**
 * @file desk_tof.h
 * @brief 双 ToF 传感器的只读测距服务。
 *
 * TOF050C/VL6180X 提供桌面右侧到墙面的距离，TOF400C/VL53L1X
 * 提供传感器到地面的距离。当前组件只发布实时数据，不参与运动控制。
 */
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /** 五点稳定高度，供状态展示和停车后的最终误差判断使用。 */
    int height_mm;
    /** 最近一帧通过传感器状态检查的原始高度。 */
    int raw_height_mm;
    /** 三点中值高度，供运动中的低延迟停车判断使用。 */
    int control_height_mm;
    /** 每发布一组新高度递增，用于避免控制循环重复消费旧样本。 */
    uint32_t height_sample_id;
    bool height_known;
    int right_gap_mm;
    bool right_gap_known;
} desk_tof_snapshot_t;

/** 启动后台初始化与采样任务；传感器离线不会阻止其他网关功能启动。 */
esp_err_t desk_tof_start(i2c_master_bus_handle_t bus);

/** 返回最近一组未过期的测距结果；未知高度字段统一为 -1。 */
desk_tof_snapshot_t desk_tof_snapshot(void);

#ifdef __cplusplus
}
#endif
