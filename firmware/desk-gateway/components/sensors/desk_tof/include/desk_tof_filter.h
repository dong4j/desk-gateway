/**
 * @file desk_tof_filter.h
 * @brief ToF 三点中值滤波器，隔离单帧跳变。
 */
#pragma once

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
