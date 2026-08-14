/**
 * @file desk_tof_filter.c
 * @brief ToF 三点中值滤波器实现。
 */
#include "desk_tof_filter.h"

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

int desk_tof_filter_push(desk_tof_filter_t *filter, int value)
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
