/**
 * @file desk_tof_filter.h
 * @brief ToF 距离与高度防抖滤波器。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    int values[3];
    size_t count;
    size_t next;
} desk_tof_filter_t;

/**
 * 加入一个有效样本并返回滤波结果。
 *
 * 样本不足三个时返回最新值；窗口满后返回最近三个样本的中值。
 */
int desk_tof_filter_push(desk_tof_filter_t *filter, int value);

typedef struct {
    int values[5];
    size_t count;
    size_t next;
    int output;
    bool output_valid;
} desk_tof_height_filter_t;

/**
 * 加入 TOF400C 高度样本并返回稳定高度。
 *
 * 窗口未满时以最新样本作为候选；满五个样本后改取中值。所有候选再经过
 * 3 mm 死区，抑制静止时的小数跳动，同时允许缓慢移动累计后继续更新。
 */
int desk_tof_height_filter_push(desk_tof_height_filter_t *filter, int value);
