/**
 * @file desk_ble.h
 * @brief Desk Gateway BLE GATT Server 启动接口。
 */
#pragma once

#include "desk_ble_management.h"

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 NimBLE、注册 Desk Accessory Service 并开始广播。 */
esp_err_t desk_ble_start(void);

/** 读取跨任务保护的匿名 Bond 快照。BLE 尚未就绪时返回 false。 */
bool desk_ble_get_management_snapshot(
    desk_ble_management_snapshot_t *out_snapshot);

/** 下列入口只向 NimBLE Host 有界队列提交命令，不直接调用 GAP 或 Bond Store。 */
desk_ble_management_result_t desk_ble_open_pairing_window(void);
desk_ble_management_result_t desk_ble_close_pairing_window(void);
desk_ble_management_result_t desk_ble_delete_bond(const char *bond_id);
desk_ble_management_result_t desk_ble_delete_all_bonds(void);

#ifdef __cplusplus
}
#endif
