/**
 * @file desk_tof_filter_test.c
 * @brief ToF 距离中值与高度防抖滤波器的主机单元测试。
 */
#include "desk_tof_filter.h"

#include <assert.h>
#include <stdio.h>

/** 55.8～56.4 cm 的静止波动在窗口预热后应稳定为一个值。 */
static void test_height_stationary_jitter_is_stable(void)
{
    desk_tof_stable_filter_t filter = {0};
    const int samples[] = {558, 561, 564, 560, 562, 559, 563, 558};
    int output = 0;
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
        output = desk_tof_stable_filter_push(&filter, samples[i]);
    }
    assert(output == 560);
}

/** 连续变化会跨过 3 mm 死区，不能被静止防抖锁死。 */
static void test_height_motion_continues_to_follow(void)
{
    desk_tof_stable_filter_t filter = {0};
    const int warmup[] = {558, 561, 564, 560, 562};
    for (size_t i = 0; i < sizeof(warmup) / sizeof(warmup[0]); ++i) {
        (void)desk_tof_stable_filter_push(&filter, warmup[i]);
    }
    assert(desk_tof_stable_filter_push(&filter, 565) == 560);
    assert(desk_tof_stable_filter_push(&filter, 568) == 564);
    assert(desk_tof_stable_filter_push(&filter, 571) == 564);
    assert(desk_tof_stable_filter_push(&filter, 574) == 568);
    assert(desk_tof_stable_filter_push(&filter, 577) == 571);
}

/** 控制通道只落后一个测距样本，并拒绝夹在正常样本中的单次尖峰。 */
static void test_control_height_rejects_spike_with_lower_delay(void)
{
    desk_tof_control_filter_t filter = {0};
    assert(desk_tof_control_filter_push(&filter, 850) == 850);
    assert(desk_tof_control_filter_push(&filter, 854) == 854);
    assert(desk_tof_control_filter_push(&filter, 900) == 854);
    assert(desk_tof_control_filter_push(&filter, 858) == 858);
    assert(desk_tof_control_filter_push(&filter, 862) == 862);
}

/** 同一上升序列中，控制高度必须比五点稳定高度更早进入停车边界。 */
static void test_control_height_crosses_target_before_stable_height(void)
{
    desk_tof_control_filter_t control = {0};
    desk_tof_stable_filter_t stable = {0};
    const int samples[] = {850, 854, 858, 862, 866, 870, 874};
    size_t control_crossing = 0U;
    size_t stable_crossing = 0U;
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
        int control_mm = desk_tof_control_filter_push(&control, samples[i]);
        int stable_mm = desk_tof_stable_filter_push(&stable, samples[i]);
        if (control_crossing == 0U && control_mm >= 865) {
            control_crossing = i + 1U;
        }
        if (stable_crossing == 0U && stable_mm >= 865) {
            stable_crossing = i + 1U;
        }
    }
    assert(control_crossing > 0U);
    assert(stable_crossing > control_crossing);
}

/** 9.x cm 的右侧静止距离应保持稳定。 */
static void test_right_gap_stationary_jitter_is_stable(void)
{
    desk_tof_stable_filter_t filter = {0};
    const int samples[] = {94, 96, 93, 94, 95, 92, 96, 93};
    int output = 0;
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
        output = desk_tof_stable_filter_push(&filter, samples[i]);
    }
    assert(output == 94);
}

/** 从 9.x cm 变化到 8.x cm 时，滤波结果仍需及时跟随。 */
static void test_right_gap_real_change_continues_to_follow(void)
{
    desk_tof_stable_filter_t filter = {0};
    const int warmup[] = {94, 96, 93, 94, 95};
    for (size_t i = 0; i < sizeof(warmup) / sizeof(warmup[0]); ++i) {
        (void)desk_tof_stable_filter_push(&filter, warmup[i]);
    }
    assert(desk_tof_stable_filter_push(&filter, 90) == 94);
    assert(desk_tof_stable_filter_push(&filter, 87) == 94);
    assert(desk_tof_stable_filter_push(&filter, 84) == 90);
    assert(desk_tof_stable_filter_push(&filter, 81) == 87);
}

int main(void)
{
    test_height_stationary_jitter_is_stable();
    test_height_motion_continues_to_follow();
    test_control_height_rejects_spike_with_lower_delay();
    test_control_height_crosses_target_before_stable_height();
    test_right_gap_stationary_jitter_is_stable();
    test_right_gap_real_change_continues_to_follow();
    puts("desk_tof_filter_test: ok");
    return 0;
}
