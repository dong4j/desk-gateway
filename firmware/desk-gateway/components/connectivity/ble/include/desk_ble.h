/**
 * @file desk_ble.h
 * @brief Desk Gateway BLE GATT Server 启动接口。
 */
#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** REST/Web 只读取匿名管理快照，不暴露底层 BLE Identity。 */
#define DESK_BLE_MANAGEMENT_MAX_DEVICES 3
#define DESK_BLE_MANAGEMENT_ID_LENGTH 18
#define DESK_BLE_MANAGEMENT_KIND_LENGTH 8
#define DESK_BLE_MANAGEMENT_LABEL_LENGTH 32
#define DESK_BLE_MANAGEMENT_ERROR_LENGTH 48

typedef enum {
    DESK_BLE_MANAGEMENT_OK = 0,
    DESK_BLE_MANAGEMENT_ACCEPTED,
    DESK_BLE_MANAGEMENT_NOT_FOUND,
    DESK_BLE_MANAGEMENT_CONFLICT,
    DESK_BLE_MANAGEMENT_INTERNAL_ERROR,
} desk_ble_management_result_t;

typedef struct {
    char id[DESK_BLE_MANAGEMENT_ID_LENGTH];
    char kind[DESK_BLE_MANAGEMENT_KIND_LENGTH];
    char label[DESK_BLE_MANAGEMENT_LABEL_LENGTH];
    bool connected;
    bool controlling;
    uint8_t delete_state;
    char delete_error[DESK_BLE_MANAGEMENT_ERROR_LENGTH];
} desk_ble_bond_view_t;

typedef struct {
    desk_ble_bond_view_t devices[DESK_BLE_MANAGEMENT_MAX_DEVICES];
    size_t device_count;
    size_t capacity;
    bool pairing_window_open;
    uint32_t pairing_window_remaining_seconds;
} desk_ble_management_snapshot_t;

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
