/**
 * @file desk_peripheral_i2c.h
 * @brief GPIO10/11 外设 I2C 总线的单一所有者。
 *
 * ToF 与 OLED 必须复用同一个 ESP-IDF Bus Handle，不能分别在 I2C1 上
 * 创建总线，否则会产生控制器占用冲突。
 */
#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 创建共享总线并返回其句柄；重复调用返回同一句柄。 */
esp_err_t desk_peripheral_i2c_start(i2c_master_bus_handle_t *out_bus);

#ifdef __cplusplus
}
#endif
