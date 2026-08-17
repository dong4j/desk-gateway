/**
 * @file desk_status_led_logic.c
 * @brief 把 desk_core / Wi-Fi 快照翻译成三颗灯的开关。
 *
 * 约束：这里不读写 GPIO，也不调用 desk_core。驱动失败时调用方应保持灯灭。
 */
#include "desk_status_led_logic.h"

#include <stddef.h>

void desk_status_led_evaluate(const desk_status_led_input_t *in,
                              desk_status_led_output_t *out)
{
    if (out == NULL) {
        return;
    }
    out->red = false;
    out->yellow = false;
    out->blue = false;
    if (in == NULL) {
        return;
    }
    out->red = in->child_lock || in->fault || in->upward_blocked;
    out->yellow = in->wifi_ap || !in->wifi_connected;
    out->blue = in->moving;
}
