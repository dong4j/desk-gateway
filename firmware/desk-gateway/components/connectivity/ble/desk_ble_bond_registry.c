/**
 * @file desk_ble_bond_registry.c
 * @brief 固定容量 BLE Bond 注册表实现。
 */
#include "desk_ble_bond_registry.h"

#include <stdio.h>
#include <string.h>

static bool opaque_id_equal(
    const uint8_t left[DESK_BLE_BOND_OPAQUE_ID_LENGTH],
    const uint8_t right[DESK_BLE_BOND_OPAQUE_ID_LENGTH])
{
    return memcmp(left, right, DESK_BLE_BOND_OPAQUE_ID_LENGTH) == 0;
}

static bool opaque_id_exists(const desk_ble_bond_registry_t *registry,
                             const uint8_t opaque_id[
                                 DESK_BLE_BOND_OPAQUE_ID_LENGTH])
{
    for (size_t i = 0; i < DESK_BLE_BOND_CAPACITY; ++i) {
        const desk_ble_bond_record_t *record = &registry->records[i];
        if (record->in_use &&
            opaque_id_equal(record->opaque_id, opaque_id)) {
            return true;
        }
    }
    return false;
}

static bool identity_in_list(const desk_ble_peer_identity_t *identity,
                             const desk_ble_peer_identity_t *identities,
                             size_t identity_count)
{
    for (size_t i = 0; i < identity_count; ++i) {
        if (desk_ble_peer_identity_equal(identity, &identities[i])) {
            return true;
        }
    }
    return false;
}

void desk_ble_bond_registry_init(desk_ble_bond_registry_t *registry)
{
    if (registry) {
        memset(registry, 0, sizeof(*registry));
    }
}

size_t desk_ble_bond_registry_count(
    const desk_ble_bond_registry_t *registry)
{
    if (!registry) {
        return 0;
    }
    size_t count = 0;
    for (size_t i = 0; i < DESK_BLE_BOND_CAPACITY; ++i) {
        count += registry->records[i].in_use ? 1U : 0U;
    }
    return count;
}

bool desk_ble_peer_identity_equal(const desk_ble_peer_identity_t *left,
                                  const desk_ble_peer_identity_t *right)
{
    return left && right && left->type == right->type &&
           memcmp(left->value, right->value,
                  DESK_BLE_PEER_ADDRESS_LENGTH) == 0;
}

desk_ble_bond_record_t *desk_ble_bond_registry_find_identity(
    desk_ble_bond_registry_t *registry,
    const desk_ble_peer_identity_t *identity)
{
    if (!registry || !identity) {
        return NULL;
    }
    for (size_t i = 0; i < DESK_BLE_BOND_CAPACITY; ++i) {
        desk_ble_bond_record_t *record = &registry->records[i];
        if (record->in_use &&
            desk_ble_peer_identity_equal(&record->identity, identity)) {
            return record;
        }
    }
    return NULL;
}

const desk_ble_bond_record_t *desk_ble_bond_registry_find_identity_const(
    const desk_ble_bond_registry_t *registry,
    const desk_ble_peer_identity_t *identity)
{
    if (!registry || !identity) {
        return NULL;
    }
    for (size_t i = 0; i < DESK_BLE_BOND_CAPACITY; ++i) {
        const desk_ble_bond_record_t *record = &registry->records[i];
        if (record->in_use &&
            desk_ble_peer_identity_equal(&record->identity, identity)) {
            return record;
        }
    }
    return NULL;
}

bool desk_ble_bond_format_id(const desk_ble_bond_record_t *record,
                             char *out, size_t out_size)
{
    if (!record || !record->in_use || !out ||
        out_size < DESK_BLE_BOND_ID_TEXT_LENGTH) {
        return false;
    }
    int length = snprintf(
        out, out_size, "bond_%02x%02x%02x%02x%02x%02x",
        record->opaque_id[0], record->opaque_id[1], record->opaque_id[2],
        record->opaque_id[3], record->opaque_id[4], record->opaque_id[5]);
    return length == DESK_BLE_BOND_ID_TEXT_LENGTH - 1;
}

desk_ble_bond_record_t *desk_ble_bond_registry_find_id(
    desk_ble_bond_registry_t *registry, const char *bond_id)
{
    if (!registry || !bond_id) {
        return NULL;
    }
    char formatted[DESK_BLE_BOND_ID_TEXT_LENGTH];
    for (size_t i = 0; i < DESK_BLE_BOND_CAPACITY; ++i) {
        desk_ble_bond_record_t *record = &registry->records[i];
        if (desk_ble_bond_format_id(record, formatted, sizeof(formatted)) &&
            strcmp(formatted, bond_id) == 0) {
            return record;
        }
    }
    return NULL;
}

static desk_ble_bond_record_t *find_free_record(
    desk_ble_bond_registry_t *registry)
{
    for (size_t i = 0; i < DESK_BLE_BOND_CAPACITY; ++i) {
        if (!registry->records[i].in_use) {
            return &registry->records[i];
        }
    }
    return NULL;
}

static bool add_identity(desk_ble_bond_registry_t *registry,
                         const desk_ble_peer_identity_t *identity,
                         desk_ble_bond_random_fn random_fn,
                         void *random_context)
{
    desk_ble_bond_record_t *record = find_free_record(registry);
    if (!record || !random_fn) {
        return false;
    }
    uint8_t candidate[DESK_BLE_BOND_OPAQUE_ID_LENGTH];
    bool generated = false;
    for (size_t attempt = 0; attempt < DESK_BLE_BOND_RANDOM_ATTEMPTS;
         ++attempt) {
        if (!random_fn(candidate, random_context)) {
            return false;
        }
        if (!opaque_id_exists(registry, candidate)) {
            generated = true;
            break;
        }
    }
    if (!generated) {
        return false;
    }

    memset(record, 0, sizeof(*record));
    record->in_use = true;
    record->identity = *identity;
    memcpy(record->opaque_id, candidate, sizeof(record->opaque_id));
    record->client_kind = DESK_BLE_CLIENT_UNKNOWN;
    return true;
}

bool desk_ble_bond_registry_reconcile(
    desk_ble_bond_registry_t *registry,
    const desk_ble_peer_identity_t *identities, size_t identity_count,
    desk_ble_bond_random_fn random_fn, void *random_context,
    bool *out_changed)
{
    if (out_changed) {
        *out_changed = false;
    }
    if (!registry || identity_count > DESK_BLE_BOND_CAPACITY ||
        (identity_count > 0 && !identities)) {
        return false;
    }
    for (size_t i = 0; i < identity_count; ++i) {
        for (size_t j = i + 1; j < identity_count; ++j) {
            if (desk_ble_peer_identity_equal(&identities[i],
                                             &identities[j])) {
                return false;
            }
        }
    }

    /* 在副本上完成整个对账，失败时不会留下已删孤儿或半条新记录。 */
    desk_ble_bond_registry_t next = *registry;
    bool changed = false;
    for (size_t i = 0; i < DESK_BLE_BOND_CAPACITY; ++i) {
        desk_ble_bond_record_t *record = &next.records[i];
        if (record->in_use &&
            !identity_in_list(&record->identity, identities,
                              identity_count)) {
            memset(record, 0, sizeof(*record));
            changed = true;
        }
    }
    for (size_t i = 0; i < identity_count; ++i) {
        if (!desk_ble_bond_registry_find_identity(&next, &identities[i])) {
            if (!add_identity(&next, &identities[i], random_fn,
                              random_context)) {
                return false;
            }
            changed = true;
        }
    }
    *registry = next;
    if (out_changed) {
        *out_changed = changed;
    }
    return true;
}

bool desk_ble_bond_registry_remove(
    desk_ble_bond_registry_t *registry,
    const desk_ble_peer_identity_t *identity)
{
    desk_ble_bond_record_t *record =
        desk_ble_bond_registry_find_identity(registry, identity);
    if (!record) {
        return false;
    }
    memset(record, 0, sizeof(*record));
    return true;
}

void desk_ble_bond_mark_delete_pending(desk_ble_bond_record_t *record,
                                       bool waiting_disconnect,
                                       uint16_t conn_handle,
                                       uint32_t generation,
                                       uint32_t deadline_ms)
{
    if (!record || !record->in_use) {
        return;
    }
    record->delete_state = DESK_BLE_DELETE_PENDING;
    record->delete_error[0] = '\0';
    record->delete_waiting_disconnect = waiting_disconnect;
    record->delete_conn_handle = waiting_disconnect ? conn_handle : 0;
    record->delete_generation = waiting_disconnect ? generation : 0;
    record->delete_deadline_ms = waiting_disconnect ? deadline_ms : 0;
}

void desk_ble_bond_mark_delete_failed(desk_ble_bond_record_t *record,
                                      const char *reason)
{
    if (!record || !record->in_use) {
        return;
    }
    record->delete_state = DESK_BLE_DELETE_FAILED;
    record->delete_waiting_disconnect = false;
    record->delete_conn_handle = 0;
    record->delete_generation = 0;
    record->delete_deadline_ms = 0;
    if (!reason) {
        reason = "unknown";
    }
    size_t length = strlen(reason);
    if (length >= sizeof(record->delete_error)) {
        length = sizeof(record->delete_error) - 1;
    }
    memcpy(record->delete_error, reason, length);
    record->delete_error[length] = '\0';
}

bool desk_ble_bond_delete_matches_disconnect(
    const desk_ble_bond_record_t *record, uint16_t conn_handle,
    uint32_t generation)
{
    return record && record->in_use &&
           record->delete_state == DESK_BLE_DELETE_PENDING &&
           record->delete_waiting_disconnect &&
           record->delete_conn_handle == conn_handle &&
           record->delete_generation == generation;
}

bool desk_ble_bond_registry_has_delete_conflict(
    const desk_ble_bond_registry_t *registry)
{
    if (!registry) {
        return false;
    }
    for (size_t i = 0; i < DESK_BLE_BOND_CAPACITY; ++i) {
        const desk_ble_bond_record_t *record = &registry->records[i];
        if (record->in_use && record->delete_state != DESK_BLE_DELETE_IDLE) {
            return true;
        }
    }
    return false;
}

const char *desk_ble_client_kind_name(desk_ble_client_kind_t kind)
{
    switch (kind) {
    case DESK_BLE_CLIENT_WATCHOS:
        return "watchos";
    case DESK_BLE_CLIENT_IOS:
        return "ios";
    case DESK_BLE_CLIENT_ANDROID:
        return "android";
    case DESK_BLE_CLIENT_UNKNOWN:
    default:
        return "unknown";
    }
}

bool desk_ble_bond_format_label(const desk_ble_bond_record_t *record,
                                char *out, size_t out_size)
{
    if (!record || !record->in_use || !out || out_size == 0) {
        return false;
    }
    if (record->alias[0]) {
        int alias_length = snprintf(out, out_size, "%s", record->alias);
        return alias_length > 0 && (size_t)alias_length < out_size;
    }
    const char *prefix;
    switch (record->client_kind) {
    case DESK_BLE_CLIENT_WATCHOS:
        prefix = "Apple Watch";
        break;
    case DESK_BLE_CLIENT_IOS:
        prefix = "iPhone";
        break;
    case DESK_BLE_CLIENT_ANDROID:
        prefix = "Android";
        break;
    case DESK_BLE_CLIENT_UNKNOWN:
    default:
        prefix = "未知设备";
        break;
    }
    int length = snprintf(out, out_size, "%s · %02X%02X", prefix,
                          record->opaque_id[4], record->opaque_id[5]);
    return length > 0 && (size_t)length < out_size;
}

bool desk_ble_bond_alias_valid(const char *alias)
{
    if (!alias) {
        return false;
    }
    size_t length = strlen(alias);
    if (length > DESK_BLE_BOND_ALIAS_MAX_BYTES) {
        return false;
    }
    if (length == 0) {
        return true;
    }
    bool has_visible_byte = false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char byte = (unsigned char)alias[i];
        if (byte < 0x20 || byte == 0x7f) {
            return false;
        }
        if (byte > 0x20) {
            has_visible_byte = true;
        }
    }
    return has_visible_byte;
}

bool desk_ble_bond_set_alias(desk_ble_bond_record_t *record,
                             const char *alias)
{
    if (!record || !record->in_use || !desk_ble_bond_alias_valid(alias)) {
        return false;
    }
    memcpy(record->alias, alias, strlen(alias) + 1);
    return true;
}
