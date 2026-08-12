/**
 * @file desk_web_ble_api.c
 * @brief BLE 管理 REST 纯策略实现。
 */
#include "desk_web_ble_api.h"

#include <string.h>

int desk_web_ble_result_status(desk_ble_management_result_t result)
{
    switch (result) {
    case DESK_BLE_MANAGEMENT_OK:
        return 200;
    case DESK_BLE_MANAGEMENT_ACCEPTED:
        return 202;
    case DESK_BLE_MANAGEMENT_NOT_FOUND:
        return 404;
    case DESK_BLE_MANAGEMENT_CONFLICT:
        return 409;
    case DESK_BLE_MANAGEMENT_INTERNAL_ERROR:
    default:
        return 500;
    }
}

const char *desk_web_ble_delete_state_name(uint8_t state)
{
    switch (state) {
    case 1:
        return "pending";
    case 2:
        return "failed";
    case 0:
    default:
        return "idle";
    }
}

bool desk_web_ble_extract_bond_id(const char *uri, char *out_id,
                                  size_t out_size)
{
    static const char prefix[] = "/api/v1/bluetooth/bonds/";
    static const char id_prefix[] = "bond_";
    if (!uri || !out_id || out_size < DESK_BLE_MANAGEMENT_ID_LENGTH ||
        strncmp(uri, prefix, sizeof(prefix) - 1) != 0) {
        return false;
    }
    const char *id = uri + sizeof(prefix) - 1;
    if (strlen(id) != DESK_BLE_MANAGEMENT_ID_LENGTH - 1 ||
        strncmp(id, id_prefix, sizeof(id_prefix) - 1) != 0) {
        return false;
    }
    for (size_t i = sizeof(id_prefix) - 1;
         i < DESK_BLE_MANAGEMENT_ID_LENGTH - 1; ++i) {
        if (!((id[i] >= '0' && id[i] <= '9') ||
              (id[i] >= 'a' && id[i] <= 'f'))) {
            return false;
        }
    }
    memcpy(out_id, id, DESK_BLE_MANAGEMENT_ID_LENGTH);
    return true;
}
