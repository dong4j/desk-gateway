/**
 * @file desk_ble_protocol_test.c
 * @brief BLE Accessory Profile 纯 C 协议回归测试。
 */
#include "desk_ble_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_command_decode(void)
{
    const uint8_t valid[] = {
        DESK_BLE_COMMAND_STOP,
        DESK_BLE_COMMAND_HOLD_UP,
        DESK_BLE_COMMAND_HOLD_DOWN,
        DESK_BLE_COMMAND_PRESET_1,
        DESK_BLE_COMMAND_PRESET_4,
    };
    for (size_t i = 0; i < sizeof(valid); ++i) {
        desk_ble_command_t command = DESK_BLE_COMMAND_STOP;
        assert(desk_ble_command_decode(&valid[i], 1, &command));
        assert((uint8_t)command == valid[i]);
    }

    const uint8_t unknown = 0x7f;
    desk_ble_command_t command;
    assert(!desk_ble_command_decode(&unknown, 1, &command));
    assert(!desk_ble_command_decode(&unknown, 0, &command));
    assert(!desk_ble_command_decode(NULL, 1, &command));
}

static void test_state_encode(void)
{
    desk_ble_state_input_t input = {
        .status = 1,
        .height_known = true,
        .height_sim = false,
        .child_lock = true,
        .bluetooth_enabled = true,
        .upward_blocked = false,
        .height_mm = 990,
        .max_height_mm = 1020,
    };
    uint8_t state[DESK_BLE_STATE_LENGTH];
    assert(desk_ble_state_encode(&input, state, sizeof(state)) ==
           DESK_BLE_STATE_LENGTH);
    const uint8_t expected[] = {
        0x01, 0x01, 0x0d, 0x00, 0xde, 0x03, 0xfc, 0x03,
    };
    assert(memcmp(state, expected, sizeof(expected)) == 0);

    input.height_known = false;
    input.height_mm = -1;
    assert(desk_ble_state_encode(&input, state, sizeof(state)) ==
           DESK_BLE_STATE_LENGTH);
    assert(state[4] == 0xff && state[5] == 0xff);
}

static void test_config_protocol(void)
{
    desk_ble_config_input_t input = {
        .child_lock = true,
        .rest_enabled = true,
        .bluetooth_enabled = false,
        .panel_enabled = true,
        .max_height_mm = 1100,
        .preset1_height_mm = 650,
        .preset4_height_mm = 1050,
    };
    uint8_t config[DESK_BLE_CONFIG_LENGTH];
    assert(desk_ble_config_encode(&input, config, sizeof(config)) ==
           DESK_BLE_CONFIG_LENGTH);
    const uint8_t expected[] = {
        0x02, 0x0b, 0x4c, 0x04, 0x8a, 0x02, 0x1a, 0x04,
    };
    assert(memcmp(config, expected, sizeof(expected)) == 0);

    const uint8_t write_max[] = {0x01, 0x05, 0xfc, 0x03};
    desk_ble_config_write_t write;
    assert(desk_ble_config_write_decode(write_max, sizeof(write_max),
                                        &write));
    assert(write.field == DESK_BLE_CONFIG_FIELD_MAX_HEIGHT_MM);
    assert(write.value == 1020);

    const uint8_t write_preset1[] = {0x02, 0x06, 0x8a, 0x02};
    assert(desk_ble_config_write_decode(write_preset1,
                                         sizeof(write_preset1), &write));
    assert(write.field == DESK_BLE_CONFIG_FIELD_PRESET1_HEIGHT_MM);
    assert(write.value == 650);

    const uint8_t legacy_preset[] = {0x01, 0x06, 0x8a, 0x02};
    assert(!desk_ble_config_write_decode(legacy_preset,
                                          sizeof(legacy_preset), &write));

    const uint8_t invalid_bool[] = {0x01, 0x01, 0x02, 0x00};
    assert(!desk_ble_config_write_decode(invalid_bool, sizeof(invalid_bool),
                                         &write));
    const uint8_t unknown_field[] = {0x01, 0x7f, 0x00, 0x00};
    assert(!desk_ble_config_write_decode(unknown_field,
                                         sizeof(unknown_field), &write));

    const uint8_t restart = DESK_BLE_SYSTEM_COMMAND_RESTART;
    desk_ble_system_command_t system_command;
    assert(desk_ble_system_command_decode(&restart, 1, &system_command));
    const uint8_t unknown_system_command = 0x7f;
    assert(!desk_ble_system_command_decode(&unknown_system_command, 1,
                                           &system_command));
}

int main(void)
{
    test_command_decode();
    test_state_encode();
    test_config_protocol();
    puts("desk_ble_protocol_test: OK");
    return 0;
}
