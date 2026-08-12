/**
 * @file desk_ble_session.h
 * @brief BLE 多连接会话、运动所有权和配对窗口的纯 C 状态机。
 *
 * 本模块不依赖 NimBLE 或 FreeRTOS。运行时必须只在 NimBLE Host 上下文写入，
 * 纯 C 形态用于把句柄复用、删除状态和运动所有权规则纳入主机构建门禁。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DESK_BLE_MAX_CONNECTIONS 3
#define DESK_BLE_PEER_ADDRESS_LENGTH 6
#define DESK_BLE_DELETE_ERROR_MAX_LENGTH 48
#define DESK_BLE_PAIRING_WINDOW_MS UINT32_C(120000)

typedef enum {
    DESK_BLE_CLIENT_UNKNOWN = 0x00,
    DESK_BLE_CLIENT_WATCHOS = 0x01,
    DESK_BLE_CLIENT_IOS = 0x02,
    DESK_BLE_CLIENT_ANDROID = 0x03,
} desk_ble_client_kind_t;

typedef enum {
    DESK_BLE_DELETE_IDLE = 0,
    DESK_BLE_DELETE_PENDING,
    DESK_BLE_DELETE_FAILED,
} desk_ble_delete_state_t;

typedef enum {
    DESK_BLE_STORE_FULL = 0,
    DESK_BLE_STORE_OVERFLOW,
} desk_ble_store_event_t;

typedef struct {
    uint8_t type;
    uint8_t value[DESK_BLE_PEER_ADDRESS_LENGTH];
} desk_ble_peer_identity_t;

typedef struct {
    bool in_use;
    uint16_t conn_handle;
    uint32_t generation;
    bool peer_identity_valid;
    desk_ble_peer_identity_t peer_identity;
    bool encrypted;
    desk_ble_client_kind_t client_kind;
    bool state_subscribed;
    bool config_subscribed;
    desk_ble_delete_state_t delete_state;
    char delete_error[DESK_BLE_DELETE_ERROR_MAX_LENGTH];
} desk_ble_connection_slot_t;

typedef struct {
    bool valid;
    uint16_t conn_handle;
    uint32_t generation;
} desk_ble_motion_owner_t;

typedef struct {
    desk_ble_connection_slot_t slots[DESK_BLE_MAX_CONNECTIONS];
    uint32_t next_generation;
    desk_ble_motion_owner_t motion_owner;
    bool pairing_window_open;
    uint32_t pairing_window_deadline_ms;
} desk_ble_session_t;

/** 初始化空会话；配对窗口保持关闭。 */
void desk_ble_session_init(desk_ble_session_t *session);

/**
 * 为新连接分配槽位。重复句柄和容量已满都会失败。
 * generation 每次分配都变化，禁止迟到事件命中复用后的槽位。
 */
desk_ble_connection_slot_t *desk_ble_session_connect(
    desk_ble_session_t *session, uint16_t conn_handle);

desk_ble_connection_slot_t *desk_ble_session_find(
    desk_ble_session_t *session, uint16_t conn_handle);
const desk_ble_connection_slot_t *desk_ble_session_find_const(
    const desk_ble_session_t *session, uint16_t conn_handle);

size_t desk_ble_session_connection_count(const desk_ble_session_t *session);

/**
 * 仅当句柄与代次同时匹配时释放槽位。
 * out_was_owner 用于调用方决定是否执行安全 STOP。
 */
bool desk_ble_session_disconnect(desk_ble_session_t *session,
                                 uint16_t conn_handle,
                                 uint32_t generation,
                                 bool *out_was_owner);

/** 无所有者或当前连接已拥有时成功；其他连接持有时返回 false。 */
bool desk_ble_session_claim_motion(desk_ble_session_t *session,
                                   uint16_t conn_handle);

/** 任意 STOP 或非 BLE 来源接管时释放所有权。 */
bool desk_ble_session_release_motion(desk_ble_session_t *session);

bool desk_ble_session_is_motion_owner(const desk_ble_session_t *session,
                                      uint16_t conn_handle,
                                      uint32_t generation);

void desk_ble_session_open_pairing_window(desk_ble_session_t *session,
                                          uint32_t now_ms);
void desk_ble_session_close_pairing_window(desk_ble_session_t *session);
bool desk_ble_session_pairing_window_is_open(desk_ble_session_t *session,
                                             uint32_t now_ms);
uint32_t desk_ble_session_pairing_window_remaining_ms(
    desk_ble_session_t *session, uint32_t now_ms);

/** 新身份只有窗口有效且 Bond 未满时才允许进入持久配对。 */
bool desk_ble_session_allows_new_pairing(desk_ble_session_t *session,
                                         uint32_t now_ms,
                                         size_t bond_count,
                                         size_t bond_capacity);

/** FULL 可在第三个合法 Bond 时放行；OVERFLOW 永远拒绝且不得淘汰旧 Bond。 */
bool desk_ble_session_allows_store_event(desk_ble_session_t *session,
                                         desk_ble_store_event_t event,
                                         uint32_t now_ms,
                                         size_t bond_count,
                                         size_t bond_capacity);

void desk_ble_session_mark_delete_pending(desk_ble_connection_slot_t *slot);
void desk_ble_session_mark_delete_failed(desk_ble_connection_slot_t *slot,
                                         const char *reason);

#ifdef __cplusplus
}
#endif
