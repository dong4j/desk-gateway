/**
 * @file desk_ble_bond_registry_test.c
 * @brief BLE Bond 注册表主机回归测试。
 */
#include "desk_ble_bond_registry.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const uint8_t (*values)[DESK_BLE_BOND_OPAQUE_ID_LENGTH];
    size_t count;
    size_t cursor;
} random_fixture_t;

static bool fixture_random(
    uint8_t out[DESK_BLE_BOND_OPAQUE_ID_LENGTH], void *context)
{
    random_fixture_t *fixture = context;
    if (!fixture || fixture->cursor >= fixture->count) {
        return false;
    }
    memcpy(out, fixture->values[fixture->cursor++],
           DESK_BLE_BOND_OPAQUE_ID_LENGTH);
    return true;
}

static desk_ble_peer_identity_t identity(uint8_t seed)
{
    desk_ble_peer_identity_t result = {.type = seed & 1U};
    for (size_t i = 0; i < DESK_BLE_PEER_ADDRESS_LENGTH; ++i) {
        result.value[i] = (uint8_t)(seed + i);
    }
    return result;
}

static void test_reconcile_collision_and_orphan_cleanup(void)
{
    const uint8_t random_values[][DESK_BLE_BOND_OPAQUE_ID_LENGTH] = {
        {0x73, 0xc9, 0x8f, 0x21, 0xa1, 0xb2},
        {0x73, 0xc9, 0x8f, 0x21, 0xa1, 0xb2},
        {0x11, 0x22, 0x33, 0x44, 0xc3, 0xd4},
        {0xaa, 0xbb, 0xcc, 0xdd, 0xe5, 0xf6},
    };
    random_fixture_t fixture = {
        .values = random_values,
        .count = sizeof(random_values) / sizeof(random_values[0]),
    };
    desk_ble_bond_registry_t registry;
    desk_ble_bond_registry_init(&registry);
    desk_ble_peer_identity_t first_set[] = {identity(10), identity(20)};
    bool changed = false;
    assert(desk_ble_bond_registry_reconcile(
        &registry, first_set, 2, fixture_random, &fixture, &changed));
    assert(changed);
    assert(desk_ble_bond_registry_count(&registry) == 2);
    assert(fixture.cursor == 3); /* 第二条先碰撞，再取得新 ID。 */

    desk_ble_bond_record_t *first =
        desk_ble_bond_registry_find_identity(&registry, &first_set[0]);
    assert(first);
    first->client_kind = DESK_BLE_CLIENT_WATCHOS;
    char bond_id[DESK_BLE_BOND_ID_TEXT_LENGTH];
    char label[DESK_BLE_BOND_LABEL_MAX_LENGTH];
    assert(desk_ble_bond_format_id(first, bond_id, sizeof(bond_id)));
    assert(strcmp(bond_id, "bond_73c98f21a1b2") == 0);
    assert(desk_ble_bond_registry_find_id(&registry, bond_id) == first);
    assert(desk_ble_bond_format_label(first, label, sizeof(label)));
    assert(strcmp(label, "Apple Watch · A1B2") == 0);

    desk_ble_peer_identity_t second_set[] = {identity(20), identity(30)};
    assert(desk_ble_bond_registry_reconcile(
        &registry, second_set, 2, fixture_random, &fixture, &changed));
    assert(changed);
    assert(!desk_ble_bond_registry_find_identity(&registry, &first_set[0]));
    assert(desk_ble_bond_registry_find_identity(&registry, &second_set[0]));
    assert(desk_ble_bond_registry_find_identity(&registry, &second_set[1]));
}

static void test_failed_reconcile_is_atomic(void)
{
    desk_ble_bond_registry_t registry;
    desk_ble_bond_registry_init(&registry);
    desk_ble_peer_identity_t identities[] = {identity(40)};
    random_fixture_t empty = {0};
    bool changed = true;
    assert(!desk_ble_bond_registry_reconcile(
        &registry, identities, 1, fixture_random, &empty, &changed));
    assert(!changed);
    assert(desk_ble_bond_registry_count(&registry) == 0);

    desk_ble_peer_identity_t duplicates[] = {identity(50), identity(50)};
    assert(!desk_ble_bond_registry_reconcile(
        &registry, duplicates, 2, fixture_random, &empty, NULL));
    assert(desk_ble_bond_registry_count(&registry) == 0);
}

int main(void)
{
    test_reconcile_collision_and_orphan_cleanup();
    test_failed_reconcile_is_atomic();
    puts("desk_ble_bond_registry_test: OK");
    return 0;
}
