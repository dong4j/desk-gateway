/**
 * @file desk_status_led_logic.h
 * @brief 红黄蓝状态灯语义，与 GPIO / ESP-IDF 无关，便于主机测试。
 *
 * 三灯可同时亮。驱动层只负责把结果写到 GPIO，不得在这里控桌。
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool moving;
    bool fault;
    bool child_lock;
    bool upward_blocked;
    bool wifi_ap;
    bool wifi_connected;
} desk_status_led_input_t;

typedef struct {
    bool red;
    bool yellow;
    bool blue;
} desk_status_led_output_t;

/**
 * 按已冻结语义计算灯态：
 * 红 = 童锁 / 故障 / 上升被拦；黄 = SoftAP 或 STA 未连；蓝 = 桌子正在升降。
 * in 或 out 为 NULL 时保持 out 不变（若 out 非 NULL 则先清成全灭）。
 */
void desk_status_led_evaluate(const desk_status_led_input_t *in,
                              desk_status_led_output_t *out);

#ifdef __cplusplus
}
#endif
