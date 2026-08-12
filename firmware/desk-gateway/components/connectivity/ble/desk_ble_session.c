/**
 * @file desk_ble_session.c
 * @brief BLE 多连接会话纯状态机实现。
 */
#include "desk_ble_session.h"

#include <string.h>

static uint32_t next_generation(desk_ble_session_t *session)
{
    session->next_generation++;
    if (session->next_generation == 0) {
        /* 0 保留为无效代次，整数回绕后从 1 重新开始。 */
        session->next_generation = 1;
    }
    return session->next_generation;
}

static bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    /* 有符号差值让 32-bit 毫秒计数器回绕时仍保持正确的短时限判断。 */
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

void desk_ble_session_init(desk_ble_session_t *session)
{
    if (!session) {
        return;
    }
    memset(session, 0, sizeof(*session));
}

desk_ble_connection_slot_t *desk_ble_session_find(
    desk_ble_session_t *session, uint16_t conn_handle)
{
    if (!session) {
        return NULL;
    }
    for (size_t i = 0; i < DESK_BLE_MAX_CONNECTIONS; ++i) {
        if (session->slots[i].in_use &&
            session->slots[i].conn_handle == conn_handle) {
            return &session->slots[i];
        }
    }
    return NULL;
}

const desk_ble_connection_slot_t *desk_ble_session_find_const(
    const desk_ble_session_t *session, uint16_t conn_handle)
{
    if (!session) {
        return NULL;
    }
    for (size_t i = 0; i < DESK_BLE_MAX_CONNECTIONS; ++i) {
        if (session->slots[i].in_use &&
            session->slots[i].conn_handle == conn_handle) {
            return &session->slots[i];
        }
    }
    return NULL;
}

desk_ble_connection_slot_t *desk_ble_session_connect(
    desk_ble_session_t *session, uint16_t conn_handle)
{
    if (!session || desk_ble_session_find(session, conn_handle)) {
        return NULL;
    }
    for (size_t i = 0; i < DESK_BLE_MAX_CONNECTIONS; ++i) {
        desk_ble_connection_slot_t *slot = &session->slots[i];
        if (!slot->in_use) {
            uint32_t generation = next_generation(session);
            memset(slot, 0, sizeof(*slot));
            slot->in_use = true;
            slot->conn_handle = conn_handle;
            slot->generation = generation;
            slot->client_kind = DESK_BLE_CLIENT_UNKNOWN;
            return slot;
        }
    }
    return NULL;
}

size_t desk_ble_session_connection_count(const desk_ble_session_t *session)
{
    if (!session) {
        return 0;
    }
    size_t count = 0;
    for (size_t i = 0; i < DESK_BLE_MAX_CONNECTIONS; ++i) {
        count += session->slots[i].in_use ? 1U : 0U;
    }
    return count;
}

bool desk_ble_session_is_motion_owner(const desk_ble_session_t *session,
                                      uint16_t conn_handle,
                                      uint32_t generation)
{
    return session && session->motion_owner.valid &&
           session->motion_owner.conn_handle == conn_handle &&
           session->motion_owner.generation == generation;
}

bool desk_ble_session_disconnect(desk_ble_session_t *session,
                                 uint16_t conn_handle,
                                 uint32_t generation,
                                 bool *out_was_owner)
{
    if (out_was_owner) {
        *out_was_owner = false;
    }
    desk_ble_connection_slot_t *slot =
        desk_ble_session_find(session, conn_handle);
    if (!slot || slot->generation != generation) {
        return false;
    }

    bool was_owner = desk_ble_session_is_motion_owner(
        session, conn_handle, generation);
    if (was_owner) {
        desk_ble_session_release_motion(session);
    }
    if (out_was_owner) {
        *out_was_owner = was_owner;
    }
    memset(slot, 0, sizeof(*slot));
    return true;
}

bool desk_ble_session_claim_motion(desk_ble_session_t *session,
                                   uint16_t conn_handle)
{
    desk_ble_connection_slot_t *slot =
        desk_ble_session_find(session, conn_handle);
    if (!slot || slot->delete_state == DESK_BLE_DELETE_PENDING) {
        return false;
    }
    if (session->motion_owner.valid &&
        !desk_ble_session_is_motion_owner(session, conn_handle,
                                          slot->generation)) {
        return false;
    }
    session->motion_owner.valid = true;
    session->motion_owner.conn_handle = conn_handle;
    session->motion_owner.generation = slot->generation;
    return true;
}

bool desk_ble_session_release_motion(desk_ble_session_t *session)
{
    if (!session || !session->motion_owner.valid) {
        return false;
    }
    memset(&session->motion_owner, 0, sizeof(session->motion_owner));
    return true;
}

void desk_ble_session_open_pairing_window(desk_ble_session_t *session,
                                          uint32_t now_ms)
{
    if (!session) {
        return;
    }
    session->pairing_window_open = true;
    session->pairing_window_deadline_ms =
        now_ms + DESK_BLE_PAIRING_WINDOW_MS;
}

void desk_ble_session_close_pairing_window(desk_ble_session_t *session)
{
    if (!session) {
        return;
    }
    session->pairing_window_open = false;
    session->pairing_window_deadline_ms = 0;
}

bool desk_ble_session_pairing_window_is_open(desk_ble_session_t *session,
                                             uint32_t now_ms)
{
    if (!session || !session->pairing_window_open) {
        return false;
    }
    if (deadline_reached(now_ms, session->pairing_window_deadline_ms)) {
        desk_ble_session_close_pairing_window(session);
        return false;
    }
    return true;
}

uint32_t desk_ble_session_pairing_window_remaining_ms(
    desk_ble_session_t *session, uint32_t now_ms)
{
    if (!desk_ble_session_pairing_window_is_open(session, now_ms)) {
        return 0;
    }
    return session->pairing_window_deadline_ms - now_ms;
}

bool desk_ble_session_allows_new_pairing(desk_ble_session_t *session,
                                         uint32_t now_ms,
                                         size_t bond_count,
                                         size_t bond_capacity)
{
    return bond_capacity > 0 && bond_count < bond_capacity &&
           desk_ble_session_pairing_window_is_open(session, now_ms);
}

void desk_ble_session_mark_delete_pending(desk_ble_connection_slot_t *slot)
{
    if (!slot) {
        return;
    }
    slot->delete_state = DESK_BLE_DELETE_PENDING;
    slot->delete_error[0] = '\0';
}

void desk_ble_session_mark_delete_failed(desk_ble_connection_slot_t *slot,
                                         const char *reason)
{
    if (!slot) {
        return;
    }
    slot->delete_state = DESK_BLE_DELETE_FAILED;
    if (!reason) {
        reason = "unknown";
    }
    size_t length = strlen(reason);
    if (length >= sizeof(slot->delete_error)) {
        length = sizeof(slot->delete_error) - 1;
    }
    memcpy(slot->delete_error, reason, length);
    slot->delete_error[length] = '\0';
}
