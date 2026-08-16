/**
 * @file desk_tof_filter.c
 * @brief ToF 距离与高度防抖滤波器实现。
 */
#include "desk_tof_filter.h"

/** 三点窗口用于控制通道，只消除单次尖峰，避免五点窗口的额外延迟。 */
static int median3(int a, int b, int c)
{
    if (a > b) {
        int tmp = a;
        a = b;
        b = tmp;
    }
    if (b > c) {
        int tmp = b;
        b = c;
        c = tmp;
    }
    return a > b ? a : b;
}

/** 五点窗口很小，使用原地插入排序可避免动态内存和通用排序回调。 */
static int median5(const int values[5])
{
    int sorted[5];
    for (size_t i = 0; i < 5U; ++i) {
        sorted[i] = values[i];
        size_t j = i;
        while (j > 0U && sorted[j - 1U] > sorted[j]) {
            int tmp = sorted[j - 1U];
            sorted[j - 1U] = sorted[j];
            sorted[j] = tmp;
            --j;
        }
    }
    return sorted[2];
}

int desk_tof_stable_filter_push(desk_tof_stable_filter_t *filter, int value)
{
    if (!filter) {
        return value;
    }

    filter->values[filter->next] = value;
    filter->next = (filter->next + 1U) % 5U;
    if (filter->count < 5U) {
        filter->count++;
    }

    int candidate = filter->count < 5U ? value : median5(filter->values);
    if (!filter->output_valid) {
        filter->output = candidate;
        filter->output_valid = true;
    } else if (candidate - filter->output >= 3 ||
               filter->output - candidate >= 3) {
        filter->output = candidate;
    }
    return filter->output;
}

int desk_tof_control_filter_push(desk_tof_control_filter_t *filter, int value)
{
    if (!filter) {
        return value;
    }

    filter->values[filter->next] = value;
    filter->next = (filter->next + 1U) % 3U;
    if (filter->count < 3U) {
        filter->count++;
    }
    if (filter->count < 3U) {
        return value;
    }
    return median3(filter->values[0], filter->values[1], filter->values[2]);
}
