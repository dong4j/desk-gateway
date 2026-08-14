/**
 * @file desk_oled.h
 * @brief 0.91 英寸 SSD1306 128x32 状态屏入口。
 */
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 在现有共享总线上启动 OLED；屏幕缺失不会影响总线上的 ToF。 */
esp_err_t desk_oled_start(i2c_master_bus_handle_t bus);

#ifdef __cplusplus
}
#endif
