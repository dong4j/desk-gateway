/**
 * @file desk_ble_bond_storage.c
 * @brief BLE Bond 匿名元数据的版本化 NVS blob 实现。
 */
#include "desk_ble_bond_storage.h"

#include "nvs.h"

#include <string.h>

#define DESK_BLE_BOND_STORAGE_MAGIC UINT32_C(0x424d4554)
#define DESK_BLE_BOND_STORAGE_VERSION 1

static const char *NVS_NAMESPACE = "ble_meta";
static const char *NVS_KEY = "bonds";

typedef struct {
    uint8_t in_use;
    uint8_t identity_type;
    uint8_t identity_value[DESK_BLE_PEER_ADDRESS_LENGTH];
    uint8_t opaque_id[DESK_BLE_BOND_OPAQUE_ID_LENGTH];
    uint8_t client_kind;
    uint8_t reserved;
} persisted_record_t;

typedef struct {
    uint32_t magic;
    uint8_t version;
    uint8_t reserved[3];
    persisted_record_t records[DESK_BLE_BOND_CAPACITY];
} persisted_registry_t;

static bool registry_is_valid(const desk_ble_bond_registry_t *registry)
{
    for (size_t i = 0; i < DESK_BLE_BOND_CAPACITY; ++i) {
        const desk_ble_bond_record_t *left = &registry->records[i];
        if (!left->in_use) {
            continue;
        }
        if (left->client_kind > DESK_BLE_CLIENT_ANDROID) {
            return false;
        }
        for (size_t j = i + 1; j < DESK_BLE_BOND_CAPACITY; ++j) {
            const desk_ble_bond_record_t *right = &registry->records[j];
            if (!right->in_use) {
                continue;
            }
            if (desk_ble_peer_identity_equal(&left->identity,
                                             &right->identity) ||
                memcmp(left->opaque_id, right->opaque_id,
                       DESK_BLE_BOND_OPAQUE_ID_LENGTH) == 0) {
                return false;
            }
        }
    }
    return true;
}

esp_err_t desk_ble_bond_storage_load(desk_ble_bond_registry_t *registry)
{
    if (!registry) {
        return ESP_ERR_INVALID_ARG;
    }
    desk_ble_bond_registry_init(registry);
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    persisted_registry_t persisted = {0};
    size_t length = sizeof(persisted);
    err = nvs_get_blob(handle, NVS_KEY, &persisted, &length);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    if (length != sizeof(persisted) ||
        persisted.magic != DESK_BLE_BOND_STORAGE_MAGIC ||
        persisted.version != DESK_BLE_BOND_STORAGE_VERSION) {
        return ESP_ERR_INVALID_VERSION;
    }

    for (size_t i = 0; i < DESK_BLE_BOND_CAPACITY; ++i) {
        const persisted_record_t *source = &persisted.records[i];
        if (source->in_use > 1) {
            desk_ble_bond_registry_init(registry);
            return ESP_ERR_INVALID_STATE;
        }
        if (!source->in_use) {
            continue;
        }
        desk_ble_bond_record_t *target = &registry->records[i];
        target->in_use = true;
        target->identity.type = source->identity_type;
        memcpy(target->identity.value, source->identity_value,
               sizeof(target->identity.value));
        memcpy(target->opaque_id, source->opaque_id,
               sizeof(target->opaque_id));
        target->client_kind = (desk_ble_client_kind_t)source->client_kind;
    }
    if (!registry_is_valid(registry)) {
        desk_ble_bond_registry_init(registry);
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t desk_ble_bond_storage_save(
    const desk_ble_bond_registry_t *registry)
{
    if (!registry || !registry_is_valid(registry)) {
        return ESP_ERR_INVALID_ARG;
    }
    persisted_registry_t persisted = {
        .magic = DESK_BLE_BOND_STORAGE_MAGIC,
        .version = DESK_BLE_BOND_STORAGE_VERSION,
    };
    for (size_t i = 0; i < DESK_BLE_BOND_CAPACITY; ++i) {
        const desk_ble_bond_record_t *source = &registry->records[i];
        persisted_record_t *target = &persisted.records[i];
        target->in_use = source->in_use ? 1 : 0;
        if (!source->in_use) {
            continue;
        }
        target->identity_type = source->identity.type;
        memcpy(target->identity_value, source->identity.value,
               sizeof(target->identity_value));
        memcpy(target->opaque_id, source->opaque_id,
               sizeof(target->opaque_id));
        target->client_kind = (uint8_t)source->client_kind;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(handle, NVS_KEY, &persisted, sizeof(persisted));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
