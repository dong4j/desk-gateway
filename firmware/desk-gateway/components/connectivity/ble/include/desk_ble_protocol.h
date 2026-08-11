/**
 * @file desk_ble_protocol.h
 * @brief Desk Gateway BLE Accessory Profile 的稳定字节协议。
 *
 * 协议刻意保持为固定长度二进制数据，便于使用 LightBlue 直接输入 Hex，
 * 也避免把第三方 App 的私有串口协议带入固件。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DESK_BLE_PROTOCOL_VERSION 0x01
#define DESK_BLE_STATE_LENGTH     8
#define DESK_BLE_HEIGHT_UNKNOWN   UINT16_C(0xFFFF)

typedef enum {
    DESK_BLE_COMMAND_STOP = 0x00,
    DESK_BLE_COMMAND_HOLD_UP = 0x01,
    DESK_BLE_COMMAND_HOLD_DOWN = 0x02,
    DESK_BLE_COMMAND_PRESET_1 = 0x11,
    DESK_BLE_COMMAND_PRESET_4 = 0x14,
} desk_ble_command_t;

enum {
    DESK_BLE_STATE_FLAG_HEIGHT_KNOWN = UINT8_C(1) << 0,
    DESK_BLE_STATE_FLAG_HEIGHT_SIM = UINT8_C(1) << 1,
    DESK_BLE_STATE_FLAG_CHILD_LOCK = UINT8_C(1) << 2,
    DESK_BLE_STATE_FLAG_BLUETOOTH_ENABLED = UINT8_C(1) << 3,
    DESK_BLE_STATE_FLAG_UPWARD_BLOCKED = UINT8_C(1) << 4,
};

typedef struct {
    uint8_t status;
    bool height_known;
    bool height_sim;
    bool child_lock;
    bool bluetooth_enabled;
    bool upward_blocked;
    int height_mm;
    int max_height_mm;
} desk_ble_state_input_t;

/** 解析 Command characteristic 的单字节指令。 */
bool desk_ble_command_decode(const uint8_t *data, size_t len,
                             desk_ble_command_t *out_command);

/** 将桌面状态编码为固定 8 字节、小端序的 State characteristic 数据。 */
size_t desk_ble_state_encode(const desk_ble_state_input_t *input,
                             uint8_t *out, size_t out_len);

#ifdef __cplusplus
}
#endif
