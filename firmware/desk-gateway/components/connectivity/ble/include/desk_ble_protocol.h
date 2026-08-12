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
#define DESK_BLE_CONFIG_VERSION   0x02
#define DESK_BLE_STATE_LENGTH     8
#define DESK_BLE_CONFIG_LENGTH    8
#define DESK_BLE_CONFIG_WRITE_LENGTH 4
#define DESK_BLE_CLIENT_INFO_VERSION 0x01
#define DESK_BLE_CLIENT_INFO_LENGTH  2
#define DESK_BLE_HEIGHT_UNKNOWN   UINT16_C(0xFFFF)

typedef enum {
    DESK_BLE_COMMAND_STOP = 0x00,
    DESK_BLE_COMMAND_HOLD_UP = 0x01,
    DESK_BLE_COMMAND_HOLD_DOWN = 0x02,
    DESK_BLE_COMMAND_PRESET_1 = 0x11,
    DESK_BLE_COMMAND_PRESET_4 = 0x14,
} desk_ble_command_t;

/** Config 写入按字段更新，避免客户端用旧快照覆盖其他入口刚修改的设置。 */
typedef enum {
    DESK_BLE_CONFIG_FIELD_CHILD_LOCK = 0x01,
    DESK_BLE_CONFIG_FIELD_REST_ENABLED = 0x02,
    DESK_BLE_CONFIG_FIELD_BLUETOOTH_ENABLED = 0x03,
    DESK_BLE_CONFIG_FIELD_PANEL_ENABLED = 0x04,
    DESK_BLE_CONFIG_FIELD_MAX_HEIGHT_MM = 0x05,
    DESK_BLE_CONFIG_FIELD_PRESET1_HEIGHT_MM = 0x06,
    DESK_BLE_CONFIG_FIELD_PRESET4_HEIGHT_MM = 0x07,
} desk_ble_config_field_t;

typedef enum {
    DESK_BLE_SYSTEM_COMMAND_RESTART = 0x01,
} desk_ble_system_command_t;

enum {
    DESK_BLE_STATE_FLAG_HEIGHT_KNOWN = UINT8_C(1) << 0,
    DESK_BLE_STATE_FLAG_HEIGHT_SIM = UINT8_C(1) << 1,
    DESK_BLE_STATE_FLAG_CHILD_LOCK = UINT8_C(1) << 2,
    DESK_BLE_STATE_FLAG_BLUETOOTH_ENABLED = UINT8_C(1) << 3,
    DESK_BLE_STATE_FLAG_UPWARD_BLOCKED = UINT8_C(1) << 4,
};

enum {
    DESK_BLE_CONFIG_FLAG_CHILD_LOCK = UINT8_C(1) << 0,
    DESK_BLE_CONFIG_FLAG_REST_ENABLED = UINT8_C(1) << 1,
    DESK_BLE_CONFIG_FLAG_BLUETOOTH_ENABLED = UINT8_C(1) << 2,
    DESK_BLE_CONFIG_FLAG_PANEL_ENABLED = UINT8_C(1) << 3,
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

typedef struct {
    bool child_lock;
    bool rest_enabled;
    bool bluetooth_enabled;
    bool panel_enabled;
    int max_height_mm;
    int preset1_height_mm;
    int preset4_height_mm;
} desk_ble_config_input_t;

typedef struct {
    desk_ble_config_field_t field;
    uint16_t value;
} desk_ble_config_write_t;

typedef struct {
    uint8_t client_kind;
} desk_ble_client_info_t;

/** 解析 Command characteristic 的单字节指令。 */
bool desk_ble_command_decode(const uint8_t *data, size_t len,
                             desk_ble_command_t *out_command);

/** 将桌面状态编码为固定 8 字节、小端序的 State characteristic 数据。 */
size_t desk_ble_state_encode(const desk_ble_state_input_t *input,
                             uint8_t *out, size_t out_len);

/** 将设备设置编码为固定 8 字节 Config v2 快照。 */
size_t desk_ble_config_encode(const desk_ble_config_input_t *input,
                              uint8_t *out, size_t out_len);

/** 解析固定 4 字节 Config 字段更新请求。 */
bool desk_ble_config_write_decode(const uint8_t *data, size_t len,
                                  desk_ble_config_write_t *out_write);

/** 解析 System characteristic 的单字节管理命令。 */
bool desk_ble_system_command_decode(const uint8_t *data, size_t len,
                                    desk_ble_system_command_t *out_command);

/** 解析 Client Info v1；超出 0..3 的客户端类型按 unknown(0) 处理。 */
bool desk_ble_client_info_decode(const uint8_t *data, size_t len,
                                 desk_ble_client_info_t *out_info);

#ifdef __cplusplus
}
#endif
