/**
 * @file desk_tof_filter.h
 * @brief ToF 距离与高度防抖滤波器。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    int values[5];
    size_t count;
    size_t next;
    int output;
    bool output_valid;
} desk_tof_stable_filter_t;

typedef struct {
    int values[3];
    size_t count;
    size_t next;
} desk_tof_control_filter_t;

/**
 * 加入有效距离样本并返回稳定值。
 *
 * 窗口未满时以最新样本作为候选；满五个样本后改取中值。所有候选再经过
 * 3 mm 死区，抑制静止时的小数跳动，同时允许真实变化累计后继续更新。
 */
int desk_tof_stable_filter_push(desk_tof_stable_filter_t *filter, int value);

/**
 * 加入一个运动控制高度样本并返回三点中值。
 *
 * 控制通道不复用五点稳定滤波，否则匀速运动时会固定落后两个测距周期。
 * 三点中值只拒绝单次尖峰，窗口未满前直接跟随最新有效值。
 */
int desk_tof_control_filter_push(desk_tof_control_filter_t *filter, int value);
