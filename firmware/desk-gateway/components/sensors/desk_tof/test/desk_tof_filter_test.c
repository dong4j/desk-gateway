/**
 * @file desk_tof_filter_test.c
 * @brief ToF 中值滤波器的主机单元测试。
 */
#include "desk_tof_filter.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    desk_tof_filter_t filter = {0};
    assert(desk_tof_filter_push(&filter, 80) == 80);
    assert(desk_tof_filter_push(&filter, 81) == 81);
    assert(desk_tof_filter_push(&filter, 200) == 81);
    assert(desk_tof_filter_push(&filter, 82) == 82);
    assert(desk_tof_filter_push(&filter, 79) == 82);
    puts("desk_tof_filter_test: ok");
    return 0;
}
