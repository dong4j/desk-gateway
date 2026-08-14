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
    if (input->controller_reset_supported) {
        flags |= DESK_BLE_STATE_FLAG_CONTROLLER_RESET_SUPPORTED;
    }
    if (input->controller_reset_active) {
        flags |= DESK_BLE_STATE_FLAG_CONTROLLER_RESET_ACTIVE;
    }
    if (input->controller_reset_recommended) {
        flags |= DESK_BLE_STATE_FLAG_CONTROLLER_RESET_RECOMMENDED;
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

size_t desk_ble_config_encode(const desk_ble_config_input_t *input,
                              uint8_t *out, size_t out_len)
{
    if (!input || !out || out_len < DESK_BLE_CONFIG_LENGTH) {
        return 0;
    }

    uint8_t flags = 0;
    if (input->child_lock) {
        flags |= DESK_BLE_CONFIG_FLAG_CHILD_LOCK;
    }
    if (input->rest_enabled) {
        flags |= DESK_BLE_CONFIG_FLAG_REST_ENABLED;
    }
    if (input->bluetooth_enabled) {
        flags |= DESK_BLE_CONFIG_FLAG_BLUETOOTH_ENABLED;
    }
    if (input->panel_enabled) {
        flags |= DESK_BLE_CONFIG_FLAG_PANEL_ENABLED;
    }

    out[0] = DESK_BLE_CONFIG_VERSION;
    out[1] = flags;
    uint16_t max_height = 0;
    if (input->max_height_mm > 0 &&
        input->max_height_mm <= (int)UINT16_MAX) {
        max_height = (uint16_t)input->max_height_mm;
    }
    put_u16_le(&out[2], max_height);
    uint16_t preset1_height = 0;
    if (input->preset1_height_mm > 0 &&
        input->preset1_height_mm <= (int)UINT16_MAX) {
        preset1_height = (uint16_t)input->preset1_height_mm;
    }
    put_u16_le(&out[4], preset1_height);
    uint16_t preset4_height = 0;
    if (input->preset4_height_mm > 0 &&
        input->preset4_height_mm <= (int)UINT16_MAX) {
        preset4_height = (uint16_t)input->preset4_height_mm;
    }
    put_u16_le(&out[6], preset4_height);
    return DESK_BLE_CONFIG_LENGTH;
}

bool desk_ble_config_write_decode(const uint8_t *data, size_t len,
                                  desk_ble_config_write_t *out_write)
{
    if (!data || !out_write || len != DESK_BLE_CONFIG_WRITE_LENGTH ||
        (data[0] != DESK_BLE_PROTOCOL_VERSION &&
         data[0] != DESK_BLE_CONFIG_VERSION)) {
        return false;
    }

    desk_ble_config_field_t field = (desk_ble_config_field_t)data[1];
    uint16_t value = (uint16_t)data[2] | ((uint16_t)data[3] << 8);
    switch (field) {
    case DESK_BLE_CONFIG_FIELD_CHILD_LOCK:
    case DESK_BLE_CONFIG_FIELD_REST_ENABLED:
    case DESK_BLE_CONFIG_FIELD_BLUETOOTH_ENABLED:
    case DESK_BLE_CONFIG_FIELD_PANEL_ENABLED:
        if (value > 1) {
            return false;
        }
        break;
    case DESK_BLE_CONFIG_FIELD_MAX_HEIGHT_MM:
        /* 具体安全范围由 desk_core 统一校验，协议层只负责字节格式。 */
        break;
    case DESK_BLE_CONFIG_FIELD_PRESET1_HEIGHT_MM:
    case DESK_BLE_CONFIG_FIELD_PRESET4_HEIGHT_MM:
        if (data[0] != DESK_BLE_CONFIG_VERSION) {
            return false;
        }
        break;
    default:
        return false;
    }

    out_write->field = field;
    out_write->value = value;
    return true;
}

bool desk_ble_system_command_decode(const uint8_t *data, size_t len,
                                    desk_ble_system_command_t *out_command)
{
    if (!data || !out_command || len != 1 ||
        (data[0] != DESK_BLE_SYSTEM_COMMAND_RESTART &&
         data[0] != DESK_BLE_SYSTEM_COMMAND_RESET_CONTROLLER)) {
        return false;
    }
    *out_command = (desk_ble_system_command_t)data[0];
    return true;
}

bool desk_ble_client_info_decode(const uint8_t *data, size_t len,
                                 desk_ble_client_info_t *out_info)
{
    if (!data || !out_info || len != DESK_BLE_CLIENT_INFO_LENGTH ||
        data[0] != DESK_BLE_CLIENT_INFO_VERSION) {
        return false;
    }
    out_info->client_kind = data[1] <= 3 ? data[1] : 0;
    return true;
}
