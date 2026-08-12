/**
 * @file desk_ble_session_test.c
 * @brief BLE 多连接会话状态机主机回归测试。
 */
#include "desk_ble_session.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_capacity_and_generation(void)
{
    desk_ble_session_t session;
    desk_ble_session_init(&session);

    desk_ble_connection_slot_t *first =
        desk_ble_session_connect(&session, 10);
    desk_ble_connection_slot_t *second =
        desk_ble_session_connect(&session, 11);
    desk_ble_connection_slot_t *third =
        desk_ble_session_connect(&session, 12);
    assert(first && second && third);
    assert(desk_ble_session_connection_count(&session) == 3);
    assert(desk_ble_session_connect(&session, 13) == NULL);
    assert(desk_ble_session_connect(&session, 10) == NULL);

    uint32_t old_generation = second->generation;
    bool was_owner = true;
    assert(desk_ble_session_disconnect(&session, 11, old_generation,
                                       &was_owner));
    assert(!was_owner);
    desk_ble_connection_slot_t *replacement =
        desk_ble_session_connect(&session, 11);
    assert(replacement);
    assert(replacement->generation != old_generation);
    assert(!desk_ble_session_disconnect(&session, 11, old_generation, NULL));
    assert(desk_ble_session_find(&session, 11) == replacement);
}

static void test_motion_ownership(void)
{
    desk_ble_session_t session;
    desk_ble_session_init(&session);
    desk_ble_connection_slot_t *owner =
        desk_ble_session_connect(&session, 20);
    desk_ble_connection_slot_t *other =
        desk_ble_session_connect(&session, 21);
    assert(owner && other);

    assert(desk_ble_session_claim_motion(&session, 20));
    assert(desk_ble_session_claim_motion(&session, 20));
    assert(!desk_ble_session_claim_motion(&session, 21));
    assert(desk_ble_session_is_motion_owner(&session, 20,
                                            owner->generation));

    bool was_owner = false;
    assert(desk_ble_session_disconnect(&session, 21, other->generation,
                                       &was_owner));
    assert(!was_owner);
    assert(session.motion_owner.valid);
    assert(desk_ble_session_disconnect(&session, 20, owner->generation,
                                       &was_owner));
    assert(was_owner);
    assert(!session.motion_owner.valid);
}

static void test_pairing_window(void)
{
    desk_ble_session_t session;
    desk_ble_session_init(&session);
    const uint32_t opened_at = UINT32_MAX - UINT32_C(1000);
    desk_ble_session_open_pairing_window(&session, opened_at);
    assert(desk_ble_session_allows_new_pairing(&session, opened_at, 2, 3));
    assert(!desk_ble_session_allows_new_pairing(&session, opened_at, 3, 3));
    assert(desk_ble_session_pairing_window_remaining_ms(&session, opened_at) ==
           DESK_BLE_PAIRING_WINDOW_MS);

    uint32_t before_deadline =
        opened_at + DESK_BLE_PAIRING_WINDOW_MS - 1;
    assert(desk_ble_session_pairing_window_is_open(&session,
                                                   before_deadline));
    uint32_t at_deadline = opened_at + DESK_BLE_PAIRING_WINDOW_MS;
    assert(!desk_ble_session_pairing_window_is_open(&session, at_deadline));
    assert(!session.pairing_window_open);
}

static void test_delete_state(void)
{
    desk_ble_session_t session;
    desk_ble_session_init(&session);
    desk_ble_connection_slot_t *slot =
        desk_ble_session_connect(&session, 30);
    assert(slot);

    desk_ble_session_mark_delete_pending(slot);
    assert(slot->delete_state == DESK_BLE_DELETE_PENDING);
    assert(slot->delete_error[0] == '\0');
    assert(!desk_ble_session_claim_motion(&session, 30));

    desk_ble_session_mark_delete_failed(
        slot, "terminate timeout after five seconds");
    assert(slot->delete_state == DESK_BLE_DELETE_FAILED);
    assert(strcmp(slot->delete_error,
                  "terminate timeout after five seconds") == 0);
    assert(desk_ble_session_claim_motion(&session, 30));
}

int main(void)
{
    test_capacity_and_generation();
    test_motion_ownership();
    test_pairing_window();
    test_delete_state();
    puts("desk_ble_session_test: OK");
    return 0;
}
