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

int main(void)
{
    test_command_decode();
    test_state_encode();
    puts("desk_ble_protocol_test: OK");
    return 0;
}
