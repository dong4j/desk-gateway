/**
 * @file desk_ble_bond_registry.h
 * @brief BLE Bond Identity 与匿名展示 ID 的固定容量注册表。
 *
 * 注册表不依赖 NimBLE、NVS 或 FreeRTOS，便于对启动对账、随机 ID 碰撞和
 * 孤儿清理进行主机测试。调用方负责只在 NimBLE Host 上下文修改实例。
 */
#pragma once

#include "desk_ble_session.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DESK_BLE_BOND_CAPACITY 3
#define DESK_BLE_BOND_OPAQUE_ID_LENGTH 6
#define DESK_BLE_BOND_ID_TEXT_LENGTH 18
#define DESK_BLE_BOND_LABEL_MAX_LENGTH 32
#define DESK_BLE_BOND_RANDOM_ATTEMPTS 8

typedef bool (*desk_ble_bond_random_fn)(
    uint8_t out[DESK_BLE_BOND_OPAQUE_ID_LENGTH], void *context);

typedef struct {
    bool in_use;
    desk_ble_peer_identity_t identity;
    uint8_t opaque_id[DESK_BLE_BOND_OPAQUE_ID_LENGTH];
    desk_ble_client_kind_t client_kind;
} desk_ble_bond_record_t;

typedef struct {
    desk_ble_bond_record_t records[DESK_BLE_BOND_CAPACITY];
} desk_ble_bond_registry_t;

void desk_ble_bond_registry_init(desk_ble_bond_registry_t *registry);
size_t desk_ble_bond_registry_count(
    const desk_ble_bond_registry_t *registry);

bool desk_ble_peer_identity_equal(const desk_ble_peer_identity_t *left,
                                  const desk_ble_peer_identity_t *right);

desk_ble_bond_record_t *desk_ble_bond_registry_find_identity(
    desk_ble_bond_registry_t *registry,
    const desk_ble_peer_identity_t *identity);
const desk_ble_bond_record_t *desk_ble_bond_registry_find_identity_const(
    const desk_ble_bond_registry_t *registry,
    const desk_ble_peer_identity_t *identity);
desk_ble_bond_record_t *desk_ble_bond_registry_find_id(
    desk_ble_bond_registry_t *registry, const char *bond_id);

/**
 * 以 Bond Store 提供的 identities 为事实源对账。
 *
 * 不存在的元数据会被清理；缺失的旧 Bond 会生成匿名 ID 和 unknown 类型。
 * 容量不足、重复 Identity 或随机源连续失败时返回 false，且不提交半成品。
 */
bool desk_ble_bond_registry_reconcile(
    desk_ble_bond_registry_t *registry,
    const desk_ble_peer_identity_t *identities, size_t identity_count,
    desk_ble_bond_random_fn random_fn, void *random_context,
    bool *out_changed);

bool desk_ble_bond_registry_remove(
    desk_ble_bond_registry_t *registry,
    const desk_ble_peer_identity_t *identity);

/** 固定输出 `bond_<12位小写十六进制>`，缓冲区至少 18 字节。 */
bool desk_ble_bond_format_id(const desk_ble_bond_record_t *record,
                             char *out, size_t out_size);
const char *desk_ble_client_kind_name(desk_ble_client_kind_t kind);
bool desk_ble_bond_format_label(const desk_ble_bond_record_t *record,
                                char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
