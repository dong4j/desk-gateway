/**
 * @file desk_ble_protocol.c
 * @brief 与 NimBLE 无关的 BLE Accessory Profile 编解码实现。
 */
#include "desk_ble_protocol.h"

static void put_u16_le(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & UINT16_C(0x00FF));
    out[1] = (uint8_t)(value >> 8);
}

bool desk_ble_command_decode(const uint8_t *data, size_t len,
                             desk_ble_command_t *out_command)
{
    if (!data || !out_command || len != 1) {
        return false;
    }

    switch (data[0]) {
    case DESK_BLE_COMMAND_STOP:
    case DESK_BLE_COMMAND_HOLD_UP:
    case DESK_BLE_COMMAND_HOLD_DOWN:
    case DESK_BLE_COMMAND_PRESET_1:
    case DESK_BLE_COMMAND_PRESET_4:
        *out_command = (desk_ble_command_t)data[0];
        return true;
    default:
        return false;
    }
}

size_t desk_ble_state_encode(const desk_ble_state_input_t *input,
                             uint8_t *out, size_t out_len)
{
    if (!input || !out || out_len < DESK_BLE_STATE_LENGTH) {
        return 0;
    }

    uint8_t flags = 0;
    if (input->height_known) {
        flags |= DESK_BLE_STATE_FLAG_HEIGHT_KNOWN;
    }
    if (input->height_sim) {
        flags |= DESK_BLE_STATE_FLAG_HEIGHT_SIM;
    }
    if (input->child_lock) {
        flags |= DESK_BLE_STATE_FLAG_CHILD_LOCK;
    }
    if (input->bluetooth_enabled) {
        flags |= DESK_BLE_STATE_FLAG_BLUETOOTH_ENABLED;
    }
    if (input->upward_blocked) {
        flags |= DESK_BLE_STATE_FLAG_UPWARD_BLOCKED;
    }

    out[0] = DESK_BLE_PROTOCOL_VERSION;
    out[1] = input->status;
    out[2] = flags;
    out[3] = 0; /* 保留给协议兼容扩展，v1 必须写 0。 */

    uint16_t height = DESK_BLE_HEIGHT_UNKNOWN;
    if (input->height_known && input->height_mm >= 0 &&
        input->height_mm < (int)DESK_BLE_HEIGHT_UNKNOWN) {
        height = (uint16_t)input->height_mm;
    }
    put_u16_le(&out[4], height);

    uint16_t max_height = 0;
    if (input->max_height_mm > 0 &&
        input->max_height_mm <= (int)UINT16_MAX) {
        max_height = (uint16_t)input->max_height_mm;
    }
    put_u16_le(&out[6], max_height);
    return DESK_BLE_STATE_LENGTH;
}
