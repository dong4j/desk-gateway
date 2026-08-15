#include "desk_auto_lock.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *DEVICE_A = "bond_001122aabbcc";
static const char *DEVICE_B = "bond_ffeeddccbbaa";

static void test_only_selected_device_controls_automation(void)
{
    desk_auto_lock_state_t state;
    desk_auto_lock_init(&state, true, DEVICE_A, false,
                        DESK_CHILD_LOCK_REASON_NONE, 1000);
    assert(desk_auto_lock_heartbeat(&state, DEVICE_B, 2000) ==
           DESK_AUTO_LOCK_ACTION_NONE);
    assert(!desk_auto_lock_detector_online(&state, 2000));
    assert(desk_auto_lock_heartbeat(&state, DEVICE_A, 2000) ==
           DESK_AUTO_LOCK_ACTION_NONE);
    assert(desk_auto_lock_detector_online(&state, 2000));
}

static void test_manual_lock_is_never_auto_unlocked(void)
{
    desk_auto_lock_state_t state;
    desk_auto_lock_init(&state, true, DEVICE_A, true,
                        DESK_CHILD_LOCK_REASON_MANUAL, 0);
    assert(desk_auto_lock_heartbeat(&state, DEVICE_A, 1) ==
           DESK_AUTO_LOCK_ACTION_NONE);
    assert(state.lock_reason == DESK_CHILD_LOCK_REASON_MANUAL);
}

static void test_auto_lock_round_trip(void)
{
    desk_auto_lock_state_t state;
    desk_auto_lock_init(&state, true, DEVICE_A, false,
                        DESK_CHILD_LOCK_REASON_NONE, 100);
    assert(desk_auto_lock_tick(
               &state, 100 + DESK_AUTO_LOCK_AWAY_TIMEOUT_MS - 1) ==
           DESK_AUTO_LOCK_ACTION_NONE);
    assert(desk_auto_lock_tick(
               &state, 100 + DESK_AUTO_LOCK_AWAY_TIMEOUT_MS) ==
           DESK_AUTO_LOCK_ACTION_LOCK);
    desk_auto_lock_record_lock_state(
        &state, true, DESK_CHILD_LOCK_REASON_AUTO_AWAY);
    assert(desk_auto_lock_set_ble_presence(&state, DEVICE_A, true, 200000) ==
           DESK_AUTO_LOCK_ACTION_UNLOCK);
}

static void test_old_locked_state_migrates_to_manual(void)
{
    desk_auto_lock_state_t state;
    desk_auto_lock_init(&state, true, DEVICE_A, true,
                        DESK_CHILD_LOCK_REASON_NONE, 0);
    assert(state.lock_reason == DESK_CHILD_LOCK_REASON_MANUAL);
}

static void test_device_id_validation(void)
{
    assert(desk_auto_lock_device_id_valid(DEVICE_A));
    assert(!desk_auto_lock_device_id_valid("bond_001122AABBCC"));
    assert(!desk_auto_lock_device_id_valid("001122aabbcc"));
    assert(!desk_auto_lock_device_id_valid(NULL));
}

int main(void)
{
    test_only_selected_device_controls_automation();
    test_manual_lock_is_never_auto_unlocked();
    test_auto_lock_round_trip();
    test_old_locked_state_migrates_to_manual();
    test_device_id_validation();
    puts("desk_auto_lock_test: ok");
    return 0;
}
