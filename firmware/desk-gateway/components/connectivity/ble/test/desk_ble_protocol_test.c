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
        .controller_reset_supported = true,
        .controller_reset_active = false,
        .controller_reset_recommended = true,
        .height_mm = 990,
        .max_height_mm = 1020,
    };
    uint8_t state[DESK_BLE_STATE_LENGTH];
    assert(desk_ble_state_encode(&input, state, sizeof(state)) ==
           DESK_BLE_STATE_LENGTH);
    const uint8_t expected[] = {
        0x01, 0x01, 0xad, 0x00, 0xde, 0x03, 0xfc, 0x03,
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
    const uint8_t reset = DESK_BLE_SYSTEM_COMMAND_RESET_CONTROLLER;
    assert(desk_ble_system_command_decode(&reset, 1, &system_command));
    assert(system_command == DESK_BLE_SYSTEM_COMMAND_RESET_CONTROLLER);
    const uint8_t unknown_system_command = 0x7f;
    assert(!desk_ble_system_command_decode(&unknown_system_command, 1,
                                           &system_command));
}

static void test_client_info_decode(void)
{
    const uint8_t watch[] = {0x01, 0x01};
    desk_ble_client_info_t info;
    assert(desk_ble_client_info_decode(watch, sizeof(watch), &info));
    assert(info.client_kind == 0x01);

    const uint8_t future_kind[] = {0x01, 0x7f};
    assert(desk_ble_client_info_decode(future_kind, sizeof(future_kind),
                                       &info));
    assert(info.client_kind == 0x00);

    const uint8_t future_version[] = {0x02, 0x01};
    assert(!desk_ble_client_info_decode(future_version,
                                        sizeof(future_version), &info));
    assert(!desk_ble_client_info_decode(watch, 1, &info));
}

static void test_presence_decode(void)
{
    const uint8_t valid[] = {
        0x01, 'b', 'o', 'n', 'd', '_', '0', '0', '1', '1',
        '2', '2', 'a', 'a', 'b', 'b', 'c', 'c',
    };
    desk_ble_presence_t presence;
    assert(desk_ble_presence_decode(valid, sizeof(valid), &presence));
    assert(strcmp(presence.device_id, "bond_001122aabbcc") == 0);
    uint8_t invalid[sizeof(valid)];
    memcpy(invalid, valid, sizeof(valid));
    invalid[10] = 'G';
    assert(!desk_ble_presence_decode(invalid, sizeof(invalid), &presence));
}

static void test_reminder_protocol(void)
{
    desk_ble_reminder_input_t input = {
        .state = 1,
        .phase = 0,
        .alarm_reason = 0,
        .available = true,
        .audio_available = true,
        .audio_enabled = true,
        .audio_playing = false,
        .volume_percent = 72,
        .focus_minutes = 25,
        .short_break_minutes = 5,
        .long_break_minutes = 15,
        .focuses_per_long_break = 4,
        .remaining_sec = 1499,
        .completed_focus_count = 7,
    };
    uint8_t reminder[DESK_BLE_REMINDER_LENGTH];
    assert(desk_ble_reminder_encode(&input, reminder, sizeof(reminder)) ==
           DESK_BLE_REMINDER_LENGTH);
    const uint8_t expected[] = {
        0x01, 0x01, 0x00, 0x00, 0x07, 72, 25, 5, 15, 4,
        0xdb, 0x05, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    assert(memcmp(reminder, expected, sizeof(expected)) == 0);

    const uint8_t pause = DESK_BLE_REMINDER_ACTION_PAUSE;
    desk_ble_reminder_action_t action;
    assert(desk_ble_reminder_action_decode(&pause, 1, &action));
    assert(action == DESK_BLE_REMINDER_ACTION_PAUSE);
    const uint8_t unknown = 0x7f;
    assert(!desk_ble_reminder_action_decode(&unknown, 1, &action));
}

int main(void)
{
    test_command_decode();
    test_state_encode();
    test_config_protocol();
    test_client_info_decode();
    test_presence_decode();
    test_reminder_protocol();
    puts("desk_ble_protocol_test: OK");
    return 0;
}
