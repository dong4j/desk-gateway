/**
 * @file desk_ble_management.h
 * @brief BLE Bond 管理入口与匿名快照的跨组件数据契约。
 *
 * 本头文件不依赖 ESP-IDF，Web 状态映射可以在主机环境独立测试。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DESK_BLE_MANAGEMENT_MAX_DEVICES 3
#define DESK_BLE_MANAGEMENT_ID_LENGTH 18
#define DESK_BLE_MANAGEMENT_KIND_LENGTH 8
#define DESK_BLE_MANAGEMENT_ALIAS_LENGTH 49
#define DESK_BLE_MANAGEMENT_LABEL_LENGTH 64
#define DESK_BLE_MANAGEMENT_ERROR_LENGTH 48

typedef enum {
    DESK_BLE_MANAGEMENT_OK = 0,
    DESK_BLE_MANAGEMENT_ACCEPTED,
    DESK_BLE_MANAGEMENT_NOT_FOUND,
    DESK_BLE_MANAGEMENT_CONFLICT,
    DESK_BLE_MANAGEMENT_INVALID_ARGUMENT,
    DESK_BLE_MANAGEMENT_INTERNAL_ERROR,
} desk_ble_management_result_t;

typedef struct {
    char id[DESK_BLE_MANAGEMENT_ID_LENGTH];
    char kind[DESK_BLE_MANAGEMENT_KIND_LENGTH];
    char label[DESK_BLE_MANAGEMENT_LABEL_LENGTH];
    char alias[DESK_BLE_MANAGEMENT_ALIAS_LENGTH];
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

#ifdef __cplusplus
}
#endif
