/**
 * @file desk_ble_bond_storage.h
 * @brief BLE Bond 匿名元数据的 NVS 持久化适配器。
 */
#pragma once

#include "desk_ble_bond_registry.h"

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** NVS 中无记录时返回空注册表和 ESP_OK。 */
esp_err_t desk_ble_bond_storage_load(desk_ble_bond_registry_t *registry);

/** 以单个版本化 blob 原子保存最多三条元数据。 */
esp_err_t desk_ble_bond_storage_save(
    const desk_ble_bond_registry_t *registry);

#ifdef __cplusplus
}
#endif
