/**
 * @file desk_status_led.h
 * @brief 红黄蓝状态灯 GPIO 驱动入口。
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 立刻把 GPIO1/2/8 拉低，再轮询 desk_core 与 Wi-Fi 点灯。
 * 初始化失败只让灯不可用，不得阻断控桌或其他入口。
 */
esp_err_t desk_status_led_start(void);

#ifdef __cplusplus
}
#endif
