/**
 * @file desk_web_ble_api_test.c
 * @brief BLE 管理 REST 状态码与路径解析回归测试。
 */
#include "desk_web_ble_api.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_result_statuses(void)
{
    assert(desk_web_ble_result_status(DESK_BLE_MANAGEMENT_OK) == 200);
    assert(desk_web_ble_result_status(DESK_BLE_MANAGEMENT_ACCEPTED) == 202);
    assert(desk_web_ble_result_status(DESK_BLE_MANAGEMENT_NOT_FOUND) == 404);
    assert(desk_web_ble_result_status(DESK_BLE_MANAGEMENT_CONFLICT) == 409);
    assert(desk_web_ble_result_status(
               DESK_BLE_MANAGEMENT_INTERNAL_ERROR) == 500);
}

static void test_delete_path(void)
{
    char id[DESK_BLE_MANAGEMENT_ID_LENGTH];
    assert(desk_web_ble_extract_bond_id(
        "/api/v1/bluetooth/bonds/bond_73c98f21a1b2", id, sizeof(id)));
    assert(strcmp(id, "bond_73c98f21a1b2") == 0);
    assert(!desk_web_ble_extract_bond_id(
        "/api/v1/bluetooth/bonds", id, sizeof(id)));
    assert(!desk_web_ble_extract_bond_id(
        "/api/v1/bluetooth/bonds/bond_73C98f21a1b2", id, sizeof(id)));
    assert(!desk_web_ble_extract_bond_id(
        "/api/v1/bluetooth/bonds/bond_73c98f21a1b2/extra", id,
        sizeof(id)));
}

int main(void)
{
    test_result_statuses();
    test_delete_path();
    puts("desk_web_ble_api_test: OK");
    return 0;
}
