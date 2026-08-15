/**
 * @file desk_auto_lock.c
 * @brief 单一可信设备自动童锁状态机实现。
 */
#include "desk_auto_lock.h"

#include <string.h>

static bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static bool selected_device_matches(const desk_auto_lock_state_t *state,
                                    const char *device_id)
{
    return state && state->enabled && device_id &&
           strcmp(state->selected_device_id, device_id) == 0;
}

static desk_auto_lock_action_t arrival_action(
    const desk_auto_lock_state_t *state)
{
    return state->lock_reason == DESK_CHILD_LOCK_REASON_AUTO_AWAY
               ? DESK_AUTO_LOCK_ACTION_UNLOCK
               : DESK_AUTO_LOCK_ACTION_NONE;
}

bool desk_auto_lock_device_id_valid(const char *device_id)
{
    if (!device_id || strlen(device_id) != 17 ||
        strncmp(device_id, "bond_", 5) != 0) {
        return false;
    }
    for (size_t i = 5; i < 17; ++i) {
        char c = device_id[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

void desk_auto_lock_init(desk_auto_lock_state_t *state, bool enabled,
                         const char *selected_device_id, bool child_locked,
                         desk_child_lock_reason_t persisted_reason,
                         uint32_t now_ms)
{
    if (!state) {
        return;
    }
    memset(state, 0, sizeof(*state));
    bool valid_device = desk_auto_lock_device_id_valid(selected_device_id);
    state->enabled = enabled && valid_device;
    if (valid_device) {
        memcpy(state->selected_device_id, selected_device_id, 18);
    }
    if (!child_locked) {
        state->lock_reason = DESK_CHILD_LOCK_REASON_NONE;
    } else if (persisted_reason == DESK_CHILD_LOCK_REASON_AUTO_AWAY) {
        state->lock_reason = DESK_CHILD_LOCK_REASON_AUTO_AWAY;
    } else {
        /* 升级迁移必须 fail closed：未知来源的旧童锁不能被到家事件解除。 */
        state->lock_reason = DESK_CHILD_LOCK_REASON_MANUAL;
    }
    if (state->enabled) {
        state->away_deadline_ms = now_ms + DESK_AUTO_LOCK_AWAY_TIMEOUT_MS;
    }
}

bool desk_auto_lock_configure(desk_auto_lock_state_t *state, bool enabled,
                              const char *selected_device_id,
                              uint32_t now_ms)
{
    if (!state || (enabled &&
                   !desk_auto_lock_device_id_valid(selected_device_id))) {
        return false;
    }
    if (selected_device_id && selected_device_id[0] != '\0' &&
        !desk_auto_lock_device_id_valid(selected_device_id)) {
        return false;
    }

    state->enabled = enabled;
    if (selected_device_id) {
        memset(state->selected_device_id, 0,
               sizeof(state->selected_device_id));
        if (selected_device_id[0] != '\0') {
            memcpy(state->selected_device_id, selected_device_id, 18);
        }
    }
    state->ble_present = false;
    state->recent_presence = false;
    state->away_deadline_ms = enabled
        ? now_ms + DESK_AUTO_LOCK_AWAY_TIMEOUT_MS : 0;
    return true;
}

desk_auto_lock_action_t desk_auto_lock_heartbeat(
    desk_auto_lock_state_t *state, const char *device_id, uint32_t now_ms)
{
    if (!selected_device_matches(state, device_id)) {
        return DESK_AUTO_LOCK_ACTION_NONE;
    }
    state->recent_presence = true;
    state->away_deadline_ms = now_ms + DESK_AUTO_LOCK_AWAY_TIMEOUT_MS;
    return arrival_action(state);
}

desk_auto_lock_action_t desk_auto_lock_set_ble_presence(
    desk_auto_lock_state_t *state, const char *device_id, bool present,
    uint32_t now_ms)
{
    if (!selected_device_matches(state, device_id)) {
        return DESK_AUTO_LOCK_ACTION_NONE;
    }
    state->ble_present = present;
    if (present) {
        state->recent_presence = true;
        state->away_deadline_ms = now_ms + DESK_AUTO_LOCK_AWAY_TIMEOUT_MS;
        return arrival_action(state);
    }

    /* BLE 刚断开时重新给足离家缓冲，避免射频瞬断立刻锁桌。 */
    state->away_deadline_ms = now_ms + DESK_AUTO_LOCK_AWAY_TIMEOUT_MS;
    return DESK_AUTO_LOCK_ACTION_NONE;
}

desk_auto_lock_action_t desk_auto_lock_tick(desk_auto_lock_state_t *state,
                                            uint32_t now_ms)
{
    if (!state || !state->enabled || state->ble_present ||
        !deadline_reached(now_ms, state->away_deadline_ms)) {
        return DESK_AUTO_LOCK_ACTION_NONE;
    }
    state->recent_presence = false;
    return state->lock_reason == DESK_CHILD_LOCK_REASON_NONE
               ? DESK_AUTO_LOCK_ACTION_LOCK
               : DESK_AUTO_LOCK_ACTION_NONE;
}

void desk_auto_lock_record_lock_state(desk_auto_lock_state_t *state,
                                      bool locked,
                                      desk_child_lock_reason_t reason)
{
    if (!state) {
        return;
    }
    state->lock_reason = locked ? reason : DESK_CHILD_LOCK_REASON_NONE;
}

bool desk_auto_lock_detector_online(const desk_auto_lock_state_t *state,
                                    uint32_t now_ms)
{
    return state && state->enabled &&
           (state->ble_present ||
            (state->recent_presence &&
             !deadline_reached(now_ms, state->away_deadline_ms)));
}

const char *desk_child_lock_reason_name(desk_child_lock_reason_t reason)
{
    switch (reason) {
    case DESK_CHILD_LOCK_REASON_MANUAL:
        return "manual";
    case DESK_CHILD_LOCK_REASON_AUTO_AWAY:
        return "auto_away";
    case DESK_CHILD_LOCK_REASON_NONE:
    default:
        return "none";
    }
}
