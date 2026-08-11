/**
 * @file desk_ble.h
 * @brief Desk Gateway BLE GATT Server 启动接口。
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 NimBLE、注册 Desk Accessory Service 并开始广播。 */
esp_err_t desk_ble_start(void);

#ifdef __cplusplus
}
#endif
