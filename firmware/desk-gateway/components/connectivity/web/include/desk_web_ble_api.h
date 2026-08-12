/**
 * @file desk_web_ble_api.h
 * @brief BLE 管理 REST 的纯 C 状态码和路径策略。
 */
#pragma once

#include "desk_ble_management.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int desk_web_ble_result_status(desk_ble_management_result_t result);
const char *desk_web_ble_delete_state_name(uint8_t state);

/** 只接受 `/api/v1/bluetooth/bonds/bond_<12位小写十六进制>`。 */
bool desk_web_ble_extract_bond_id(const char *uri, char *out_id,
                                  size_t out_size);

#ifdef __cplusplus
}
#endif
