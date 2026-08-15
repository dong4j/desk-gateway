/**
 * @file desk_ble.c
 * @brief Desk Gateway 原生 NimBLE GATT Server。
 *
 * BLE 只负责入口协议和连接生命周期，所有运动仍进入 desk_core，从而统一
 * 执行童锁、Bluetooth 来源权限、最高安全高度和运动超时策略。
 */
#include "desk_ble.h"

#include "desk_ble_bond_registry.h"
#include "desk_ble_bond_storage.h"
#include "desk_ble_protocol.h"
#include "desk_ble_session.h"
#include "desk_audio.h"
#include "desk_core.h"
#include "desk_reminder.h"

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nimble/nimble_npl.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"

#include <stdio.h>
#include <string.h>

/* NimBLE store/config currently exports this initializer without a public declaration. */
void ble_store_config_init(void);

#define DESK_BLE_DEVICE_NAME "DeskGateway"
#define DESK_BLE_STATE_POLL_MS 200
#define DESK_BLE_STATE_HEARTBEAT_MS 1000
#define DESK_BLE_FIRMWARE_REVISION_MAX_LEN 64
#define DESK_BLE_RESTART_DELAY_MS 500
#define DESK_BLE_ATT_ERR_BUSY 0x80
#define DESK_BLE_MANAGEMENT_QUEUE_CAPACITY 4
#define DESK_BLE_MANAGEMENT_WAIT_MS 1000
#define DESK_BLE_DELETE_TIMEOUT_MS 10000
#define DESK_BLE_DELETE_TIMEOUT_POLL_MS 500

typedef enum {
    MANAGEMENT_COMMAND_OPEN_PAIRING = 0,
    MANAGEMENT_COMMAND_CLOSE_PAIRING,
    MANAGEMENT_COMMAND_DELETE_ONE,
    MANAGEMENT_COMMAND_DELETE_ALL,
    MANAGEMENT_COMMAND_SET_ALIAS,
} management_command_kind_t;

typedef enum {
    MANAGEMENT_SLOT_FREE = 0,
    MANAGEMENT_SLOT_QUEUED,
    MANAGEMENT_SLOT_PROCESSING,
    MANAGEMENT_SLOT_DONE,
} management_slot_state_t;

typedef struct {
    management_slot_state_t state;
    bool waiter_abandoned;
    management_command_kind_t command;
    char bond_id[DESK_BLE_BOND_ID_TEXT_LENGTH];
    char alias[DESK_BLE_BOND_ALIAS_BUFFER_LENGTH];
    desk_ble_management_result_t result;
    StaticSemaphore_t completion_storage;
    SemaphoreHandle_t completion;
} management_command_slot_t;

/* Canonical UUID: 7f4e0001-6d4c-4f4b-9f7a-3c1d2e5a9b10. */
static const ble_uuid128_t s_service_uuid = BLE_UUID128_INIT(
    0x10, 0x9b, 0x5a, 0x2e, 0x1d, 0x3c, 0x7a, 0x9f,
    0x4b, 0x4f, 0x4c, 0x6d, 0x01, 0x00, 0x4e, 0x7f);
/* Canonical UUID: 7f4e0002-6d4c-4f4b-9f7a-3c1d2e5a9b10. */
static const ble_uuid128_t s_command_uuid = BLE_UUID128_INIT(
    0x10, 0x9b, 0x5a, 0x2e, 0x1d, 0x3c, 0x7a, 0x9f,
    0x4b, 0x4f, 0x4c, 0x6d, 0x02, 0x00, 0x4e, 0x7f);
/* Canonical UUID: 7f4e0003-6d4c-4f4b-9f7a-3c1d2e5a9b10. */
static const ble_uuid128_t s_state_uuid = BLE_UUID128_INIT(
    0x10, 0x9b, 0x5a, 0x2e, 0x1d, 0x3c, 0x7a, 0x9f,
    0x4b, 0x4f, 0x4c, 0x6d, 0x03, 0x00, 0x4e, 0x7f);
/* Canonical UUID: 7f4e0004-6d4c-4f4b-9f7a-3c1d2e5a9b10. */
static const ble_uuid128_t s_config_uuid = BLE_UUID128_INIT(
    0x10, 0x9b, 0x5a, 0x2e, 0x1d, 0x3c, 0x7a, 0x9f,
    0x4b, 0x4f, 0x4c, 0x6d, 0x04, 0x00, 0x4e, 0x7f);
/* Canonical UUID: 7f4e0005-6d4c-4f4b-9f7a-3c1d2e5a9b10. */
static const ble_uuid128_t s_system_uuid = BLE_UUID128_INIT(
    0x10, 0x9b, 0x5a, 0x2e, 0x1d, 0x3c, 0x7a, 0x9f,
    0x4b, 0x4f, 0x4c, 0x6d, 0x05, 0x00, 0x4e, 0x7f);
/* Canonical UUID: 7f4e0006-6d4c-4f4b-9f7a-3c1d2e5a9b10. */
static const ble_uuid128_t s_client_info_uuid = BLE_UUID128_INIT(
    0x10, 0x9b, 0x5a, 0x2e, 0x1d, 0x3c, 0x7a, 0x9f,
    0x4b, 0x4f, 0x4c, 0x6d, 0x06, 0x00, 0x4e, 0x7f);
/* Canonical UUID: 7f4e0007-6d4c-4f4b-9f7a-3c1d2e5a9b10. */
static const ble_uuid128_t s_presence_uuid = BLE_UUID128_INIT(
    0x10, 0x9b, 0x5a, 0x2e, 0x1d, 0x3c, 0x7a, 0x9f,
    0x4b, 0x4f, 0x4c, 0x6d, 0x07, 0x00, 0x4e, 0x7f);
/* Canonical UUID: 7f4e0008-6d4c-4f4b-9f7a-3c1d2e5a9b10. */
static const ble_uuid128_t s_reminder_uuid = BLE_UUID128_INIT(
    0x10, 0x9b, 0x5a, 0x2e, 0x1d, 0x3c, 0x7a, 0x9f,
    0x4b, 0x4f, 0x4c, 0x6d, 0x08, 0x00, 0x4e, 0x7f);
/* Bluetooth SIG Device Information Service / Firmware Revision String. */
static const ble_uuid16_t s_device_information_service_uuid =
    BLE_UUID16_INIT(0x180a);
static const ble_uuid16_t s_firmware_revision_uuid = BLE_UUID16_INIT(0x2a26);

static const char *TAG = "desk_ble";
static uint8_t s_own_addr_type;
static uint16_t s_state_value_handle;
static uint16_t s_config_value_handle;
static uint16_t s_reminder_value_handle;
static desk_ble_session_t s_session;
static desk_ble_bond_registry_t s_bond_registry;
static struct ble_npl_callout s_hold_lease_callout;
static struct ble_npl_callout s_delete_timeout_callout;
static struct ble_npl_event s_core_event;
static struct ble_npl_event s_management_event;
static portMUX_TYPE s_core_event_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_management_queue_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_management_snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
static management_command_slot_t
    s_management_queue[DESK_BLE_MANAGEMENT_QUEUE_CAPACITY];
static desk_ble_management_snapshot_t s_management_snapshot;
static uint32_t s_management_pairing_deadline_ms;
static bool s_core_release_pending;
static bool s_core_event_posted;
static volatile bool s_management_ready;
static volatile bool s_stack_synced;
static volatile size_t s_connection_count;
static volatile bool s_any_state_subscribed;
static volatile bool s_any_config_subscribed;
static volatile bool s_any_reminder_subscribed;
static volatile bool s_motion_owner_active;
static volatile bool s_restart_pending;

static void start_advertising(void);
static void publish_management_snapshot(void);

static desk_ble_peer_identity_t identity_from_ble_addr(
    const ble_addr_t *address)
{
    desk_ble_peer_identity_t identity = {0};
    if (address) {
        identity.type = address->type;
        memcpy(identity.value, address->val, sizeof(identity.value));
    }
    return identity;
}

static bool fill_random_id(
    uint8_t out[DESK_BLE_BOND_OPAQUE_ID_LENGTH], void *context)
{
    (void)context;
    esp_fill_random(out, DESK_BLE_BOND_OPAQUE_ID_LENGTH);
    return true;
}

/** 启动和配对完成都以 NimBLE Store 为事实源刷新匿名元数据。 */
static esp_err_t reconcile_bond_metadata(void)
{
    ble_addr_t peer_addresses[DESK_BLE_BOND_CAPACITY];
    int peer_count = 0;
    int rc = ble_store_util_bonded_peers(
        peer_addresses, &peer_count, DESK_BLE_BOND_CAPACITY);
    if (rc != 0) {
        ESP_LOGE(TAG, "enumerate BLE bonds failed: %d", rc);
        return ESP_FAIL;
    }
    desk_ble_peer_identity_t identities[DESK_BLE_BOND_CAPACITY];
    for (int i = 0; i < peer_count; ++i) {
        identities[i] = identity_from_ble_addr(&peer_addresses[i]);
    }
    desk_ble_bond_registry_t candidate = s_bond_registry;
    bool changed = false;
    if (!desk_ble_bond_registry_reconcile(
            &candidate, identities, (size_t)peer_count,
            fill_random_id, NULL, &changed)) {
        ESP_LOGE(TAG, "reconcile BLE bond metadata failed");
        return ESP_FAIL;
    }
    if (changed) {
        esp_err_t err = desk_ble_bond_storage_save(&candidate);
        if (err != ESP_OK) {
            return err;
        }
        s_bond_registry = candidate;
    }
    return ESP_OK;
}

/**
 * ENC_CHANGE 可能早于持久 Store 枚举观察到新记录，因此用当前注册表加上
 * 已确认的 Identity 原子补齐元数据；后续启动对账仍会清掉真实 Store 孤儿。
 */
static esp_err_t ensure_bond_metadata(
    const desk_ble_peer_identity_t *identity)
{
    if (desk_ble_bond_registry_find_identity(&s_bond_registry, identity)) {
        return ESP_OK;
    }
    size_t count = desk_ble_bond_registry_count(&s_bond_registry);
    if (count >= DESK_BLE_BOND_CAPACITY) {
        return ESP_ERR_NO_MEM;
    }
    desk_ble_peer_identity_t identities[DESK_BLE_BOND_CAPACITY];
    size_t cursor = 0;
    for (size_t i = 0; i < DESK_BLE_BOND_CAPACITY; ++i) {
        if (s_bond_registry.records[i].in_use) {
            identities[cursor++] = s_bond_registry.records[i].identity;
        }
    }
    identities[cursor++] = *identity;
    desk_ble_bond_registry_t candidate = s_bond_registry;
    bool changed = false;
    if (!desk_ble_bond_registry_reconcile(
            &candidate, identities, cursor, fill_random_id, NULL,
            &changed)) {
        return ESP_FAIL;
    }
    if (!changed) {
        return ESP_OK;
    }
    esp_err_t err = desk_ble_bond_storage_save(&candidate);
    if (err == ESP_OK) {
        s_bond_registry = candidate;
    }
    return err;
}

static void populate_slot_identity(desk_ble_connection_slot_t *slot,
                                   const ble_addr_t *peer_id_addr)
{
    if (!slot || !peer_id_addr) {
        return;
    }
    desk_ble_peer_identity_t identity =
        identity_from_ble_addr(peer_id_addr);
    const desk_ble_bond_record_t *record =
        desk_ble_bond_registry_find_identity_const(&s_bond_registry,
                                                   &identity);
    if (!record) {
        return;
    }
    slot->peer_identity_valid = true;
    slot->peer_identity = identity;
    slot->client_kind = record->client_kind;
}

static void report_bond_presence(const desk_ble_peer_identity_t *identity,
                                 bool present)
{
    const desk_ble_bond_record_t *record =
        desk_ble_bond_registry_find_identity_const(&s_bond_registry,
                                                   identity);
    char bond_id[DESK_BLE_BOND_ID_TEXT_LENGTH];
    if (record && desk_ble_bond_format_id(record, bond_id, sizeof(bond_id))) {
        (void)desk_core_auto_child_lock_ble_presence(bond_id, present);
    }
}

static desk_ble_connection_slot_t *find_slot_by_identity(
    const desk_ble_peer_identity_t *identity)
{
    if (!identity) {
        return NULL;
    }
    for (size_t i = 0; i < DESK_BLE_MAX_CONNECTIONS; ++i) {
        desk_ble_connection_slot_t *slot = &s_session.slots[i];
        if (slot->in_use && slot->peer_identity_valid &&
            desk_ble_peer_identity_equal(&slot->peer_identity, identity)) {
            return slot;
        }
    }
    return NULL;
}

static desk_ble_bond_record_t *find_pending_delete_by_handle(
    uint16_t conn_handle)
{
    for (size_t i = 0; i < DESK_BLE_BOND_CAPACITY; ++i) {
        desk_ble_bond_record_t *record = &s_bond_registry.records[i];
        if (record->in_use &&
            record->delete_state == DESK_BLE_DELETE_PENDING &&
            record->delete_waiting_disconnect &&
            record->delete_conn_handle == conn_handle) {
            return record;
        }
    }
    return NULL;
}

/**
 * Host 先组装不含 Identity 的本地副本，再在极短临界区发布给 REST 查询。
 * 配对截止时间单独保存，读取方可实时计算倒计时而不访问 Host 会话。
 */
static void publish_management_snapshot(void)
{
    desk_ble_management_snapshot_t snapshot = {
        .capacity = DESK_BLE_BOND_CAPACITY,
        .pairing_window_open = s_session.pairing_window_open,
    };
    for (size_t i = 0; i < DESK_BLE_BOND_CAPACITY; ++i) {
        const desk_ble_bond_record_t *record = &s_bond_registry.records[i];
        if (!record->in_use ||
            snapshot.device_count >= DESK_BLE_MANAGEMENT_MAX_DEVICES) {
            continue;
        }
        desk_ble_bond_view_t *view =
            &snapshot.devices[snapshot.device_count++];
        (void)desk_ble_bond_format_id(record, view->id, sizeof(view->id));
        (void)snprintf(view->kind, sizeof(view->kind), "%s",
                       desk_ble_client_kind_name(record->client_kind));
        (void)desk_ble_bond_format_label(record, view->label,
                                         sizeof(view->label));
        (void)snprintf(view->alias, sizeof(view->alias), "%s",
                       record->alias);
        desk_ble_connection_slot_t *slot =
            find_slot_by_identity(&record->identity);
        view->connected = slot != NULL;
        view->controlling = slot && desk_ble_session_is_motion_owner(
                                      &s_session, slot->conn_handle,
                                      slot->generation);
        view->delete_state = (uint8_t)record->delete_state;
        (void)snprintf(view->delete_error, sizeof(view->delete_error), "%s",
                       record->delete_error);
    }
    uint32_t deadline = s_session.pairing_window_deadline_ms;
    portENTER_CRITICAL(&s_management_snapshot_lock);
    s_management_snapshot = snapshot;
    s_management_pairing_deadline_ms = deadline;
    portEXIT_CRITICAL(&s_management_snapshot_lock);
}

/** Host 上下文每次改槽位后刷新只读聚合值，通知任务不直接读会话表。 */
static void update_session_aggregates(void)
{
    bool any_state = false;
    bool any_config = false;
    bool any_reminder = false;
    for (size_t i = 0; i < DESK_BLE_MAX_CONNECTIONS; ++i) {
        const desk_ble_connection_slot_t *slot = &s_session.slots[i];
        if (!slot->in_use) {
            continue;
        }
        any_state |= slot->state_subscribed;
        any_config |= slot->config_subscribed;
        any_reminder |= slot->reminder_subscribed;
    }
    s_connection_count = desk_ble_session_connection_count(&s_session);
    s_any_state_subscribed = any_state;
    s_any_config_subscribed = any_config;
    s_any_reminder_subscribed = any_reminder;
    s_motion_owner_active = s_session.motion_owner.valid;
    publish_management_snapshot();
}

static void cancel_hold_lease(void)
{
    ble_npl_callout_stop(&s_hold_lease_callout);
}

static void stop_owned_motion(const char *reason)
{
    cancel_hold_lease();
    if (desk_ble_session_release_motion(&s_session)) {
        update_session_aggregates();
        ESP_LOGW(TAG, "%s -> stop", reason);
        (void)desk_core_stop();
    }
}

static void hold_lease_event_cb(struct ble_npl_event *event)
{
    (void)event;
    /* callout 运行在 NimBLE 默认 event queue，和所有会话写入串行。 */
    stop_owned_motion("BLE hold lease expired");
}

static esp_err_t arm_hold_lease(void)
{
    cancel_hold_lease();
    /* NimBLE callout 使用 NPL tick；FreeRTOS tick 会把 750 ms 缩短到约 75 ms。 */
    ble_npl_error_t rc = ble_npl_callout_reset(
        &s_hold_lease_callout,
        ble_npl_time_ms_to_ticks32(CONFIG_DESK_BLE_HOLD_LEASE_MS));
    if (rc != BLE_NPL_OK) {
        desk_ble_session_release_motion(&s_session);
        update_session_aggregates();
        (void)desk_core_stop();
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void queue_owner_release(void)
{
    portENTER_CRITICAL(&s_core_event_lock);
    s_core_release_pending = true;
    bool should_post = !s_core_event_posted;
    s_core_event_posted = true;
    portEXIT_CRITICAL(&s_core_event_lock);
    if (should_post) {
        ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_core_event);
    }
}

static void core_event_listener(const desk_core_event_t *event, void *context)
{
    (void)context;
    if (!event) {
        return;
    }
    if (event->kind == DESK_CORE_EVENT_STOP_ACCEPTED ||
        (event->kind == DESK_CORE_EVENT_MOTION_ACCEPTED &&
         event->source != DESK_CONTROL_SOURCE_BLUETOOTH)) {
        /* 跨任务回调只投递标记，会话仍由 NimBLE Host 独占写入。 */
        queue_owner_release();
    }
}

static void core_event_cb(struct ble_npl_event *event)
{
    (void)event;
    portENTER_CRITICAL(&s_core_event_lock);
    bool release = s_core_release_pending;
    s_core_release_pending = false;
    s_core_event_posted = false;
    portEXIT_CRITICAL(&s_core_event_lock);
    if (release) {
        cancel_hold_lease();
        desk_ble_session_release_motion(&s_session);
        update_session_aggregates();
    }
}

static bool deadline_reached(uint32_t now_ms, uint32_t deadline_ms)
{
    return (int32_t)(now_ms - deadline_ms) >= 0;
}

static ble_addr_t ble_addr_from_identity(
    const desk_ble_peer_identity_t *identity)
{
    ble_addr_t address = {0};
    if (identity) {
        address.type = identity->type;
        memcpy(address.val, identity->value, sizeof(address.val));
    }
    return address;
}

static void mark_delete_failed(desk_ble_bond_record_t *record,
                               const char *reason)
{
    if (!record) {
        return;
    }
    desk_ble_connection_slot_t *slot =
        find_slot_by_identity(&record->identity);
    if (slot) {
        desk_ble_session_mark_delete_failed(slot, reason);
    }
    desk_ble_bond_mark_delete_failed(record, reason);
    publish_management_snapshot();
}

/**
 * 删除顺序以 NimBLE Store 为事实源：Store 成功后再原子保存元数据副本。
 * 若第二步失败，保留 failed 元数据供显式重试；下一次启动对账也会清理孤儿。
 */
static bool delete_bond_from_store(
    const desk_ble_peer_identity_t *identity)
{
    desk_ble_bond_record_t *record =
        desk_ble_bond_registry_find_identity(&s_bond_registry, identity);
    if (!record) {
        return true;
    }
    char bond_id[DESK_BLE_BOND_ID_TEXT_LENGTH] = {0};
    (void)desk_ble_bond_format_id(record, bond_id, sizeof(bond_id));
    ble_addr_t address = ble_addr_from_identity(identity);
    int rc = ble_store_util_delete_peer(&address);
    if (rc != 0 && rc != BLE_HS_ENOENT) {
        ESP_LOGE(TAG, "delete BLE bond store entry failed: %d", rc);
        mark_delete_failed(record, "删除蓝牙密钥失败，请重试");
        return false;
    }

    desk_ble_bond_registry_t candidate = s_bond_registry;
    if (!desk_ble_bond_registry_remove(&candidate, identity)) {
        return true;
    }
    esp_err_t err = desk_ble_bond_storage_save(&candidate);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save BLE bond deletion failed: %s",
                 esp_err_to_name(err));
        record = desk_ble_bond_registry_find_identity(&s_bond_registry,
                                                      identity);
        mark_delete_failed(record, "保存删除结果失败，请重试");
        return false;
    }
    s_bond_registry = candidate;
    if (bond_id[0]) {
        (void)desk_core_forget_auto_child_lock_device(bond_id);
    }
    publish_management_snapshot();
    return true;
}

static void arm_delete_timeout_scan(void)
{
    ble_npl_error_t rc = ble_npl_callout_reset(
        &s_delete_timeout_callout,
        pdMS_TO_TICKS(DESK_BLE_DELETE_TIMEOUT_POLL_MS));
    if (rc != BLE_NPL_OK) {
        ESP_LOGE(TAG, "arm BLE delete timeout scan failed: %d", rc);
    }
}

static desk_ble_management_result_t execute_delete_one(
    const char *bond_id)
{
    desk_ble_bond_record_t *record =
        desk_ble_bond_registry_find_id(&s_bond_registry, bond_id);
    if (!record) {
        return DESK_BLE_MANAGEMENT_NOT_FOUND;
    }
    if (record->delete_state == DESK_BLE_DELETE_PENDING) {
        return DESK_BLE_MANAGEMENT_ACCEPTED;
    }

    desk_ble_connection_slot_t *slot =
        find_slot_by_identity(&record->identity);
    if (!slot) {
        desk_ble_peer_identity_t identity = record->identity;
        desk_ble_bond_mark_delete_pending(record, false, 0, 0, 0);
        publish_management_snapshot();
        return delete_bond_from_store(&identity)
                   ? DESK_BLE_MANAGEMENT_OK
                   : DESK_BLE_MANAGEMENT_INTERNAL_ERROR;
    }

    if (desk_ble_session_is_motion_owner(
            &s_session, slot->conn_handle, slot->generation)) {
        stop_owned_motion("deleting BLE motion owner");
    }
    uint32_t now_ms = esp_log_timestamp();
    desk_ble_bond_mark_delete_pending(
        record, true, slot->conn_handle, slot->generation,
        now_ms + DESK_BLE_DELETE_TIMEOUT_MS);
    desk_ble_session_mark_delete_pending(slot);
    publish_management_snapshot();
    int rc = ble_gap_terminate(slot->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    if (rc != 0) {
        ESP_LOGE(TAG, "terminate BLE bond target failed: %d", rc);
        mark_delete_failed(record, "断开设备失败，请重试");
        return DESK_BLE_MANAGEMENT_INTERNAL_ERROR;
    }
    arm_delete_timeout_scan();
    return DESK_BLE_MANAGEMENT_ACCEPTED;
}

static desk_ble_management_result_t execute_delete_all(void)
{
    if (desk_ble_bond_registry_has_delete_conflict(&s_bond_registry)) {
        return DESK_BLE_MANAGEMENT_CONFLICT;
    }

    /* 全删是安全动作，即使当前无 BLE 所有者也要向 desk_core 发送 STOP。 */
    cancel_hold_lease();
    (void)desk_ble_session_release_motion(&s_session);
    update_session_aggregates();
    (void)desk_core_stop();
    desk_ble_session_close_pairing_window(&s_session);

    desk_ble_peer_identity_t identities[DESK_BLE_BOND_CAPACITY];
    size_t count = 0;
    uint32_t now_ms = esp_log_timestamp();
    for (size_t i = 0; i < DESK_BLE_BOND_CAPACITY; ++i) {
        desk_ble_bond_record_t *record = &s_bond_registry.records[i];
        if (!record->in_use) {
            continue;
        }
        identities[count++] = record->identity;
        desk_ble_connection_slot_t *slot =
            find_slot_by_identity(&record->identity);
        if (slot) {
            desk_ble_bond_mark_delete_pending(
                record, true, slot->conn_handle, slot->generation,
                now_ms + DESK_BLE_DELETE_TIMEOUT_MS);
            desk_ble_session_mark_delete_pending(slot);
        } else {
            desk_ble_bond_mark_delete_pending(record, false, 0, 0, 0);
        }
    }
    publish_management_snapshot();

    bool online_accepted = false;
    bool failed = false;
    for (size_t i = 0; i < count; ++i) {
        desk_ble_bond_record_t *record =
            desk_ble_bond_registry_find_identity(&s_bond_registry,
                                                 &identities[i]);
        if (!record) {
            continue;
        }
        if (!record->delete_waiting_disconnect) {
            failed |= !delete_bond_from_store(&identities[i]);
            continue;
        }
        int rc = ble_gap_terminate(record->delete_conn_handle,
                                   BLE_ERR_REM_USER_CONN_TERM);
        if (rc != 0) {
            ESP_LOGE(TAG, "terminate BLE bond target failed: %d", rc);
            mark_delete_failed(record, "断开设备失败，请重试");
            failed = true;
        } else {
            online_accepted = true;
        }
    }
    if (online_accepted) {
        arm_delete_timeout_scan();
    }
    if (failed) {
        return DESK_BLE_MANAGEMENT_INTERNAL_ERROR;
    }
    return online_accepted ? DESK_BLE_MANAGEMENT_ACCEPTED
                           : DESK_BLE_MANAGEMENT_OK;
}

static desk_ble_management_result_t execute_management_command(
    management_command_kind_t command, const char *bond_id,
    const char *alias)
{
    if (!s_stack_synced) {
        return DESK_BLE_MANAGEMENT_INTERNAL_ERROR;
    }
    switch (command) {
    case MANAGEMENT_COMMAND_OPEN_PAIRING:
        if (desk_ble_bond_registry_count(&s_bond_registry) >=
            DESK_BLE_BOND_CAPACITY) {
            return DESK_BLE_MANAGEMENT_CONFLICT;
        }
        desk_ble_session_open_pairing_window(&s_session,
                                             esp_log_timestamp());
        publish_management_snapshot();
        return DESK_BLE_MANAGEMENT_OK;
    case MANAGEMENT_COMMAND_CLOSE_PAIRING:
        desk_ble_session_close_pairing_window(&s_session);
        publish_management_snapshot();
        return DESK_BLE_MANAGEMENT_OK;
    case MANAGEMENT_COMMAND_DELETE_ONE:
        return execute_delete_one(bond_id);
    case MANAGEMENT_COMMAND_DELETE_ALL:
        return execute_delete_all();
    case MANAGEMENT_COMMAND_SET_ALIAS: {
        desk_ble_bond_record_t *record =
            desk_ble_bond_registry_find_id(&s_bond_registry, bond_id);
        if (!record) {
            return DESK_BLE_MANAGEMENT_NOT_FOUND;
        }
        if (record->delete_state != DESK_BLE_DELETE_IDLE) {
            return DESK_BLE_MANAGEMENT_CONFLICT;
        }
        desk_ble_bond_registry_t candidate = s_bond_registry;
        desk_ble_bond_record_t *candidate_record =
            desk_ble_bond_registry_find_id(&candidate, bond_id);
        if (!desk_ble_bond_set_alias(candidate_record, alias)) {
            return DESK_BLE_MANAGEMENT_INVALID_ARGUMENT;
        }
        esp_err_t err = desk_ble_bond_storage_save(&candidate);
        if (err != ESP_OK) {
            return DESK_BLE_MANAGEMENT_INTERNAL_ERROR;
        }
        s_bond_registry = candidate;
        publish_management_snapshot();
        return DESK_BLE_MANAGEMENT_OK;
    }
    default:
        return DESK_BLE_MANAGEMENT_INTERNAL_ERROR;
    }
}

static void management_event_cb(struct ble_npl_event *event)
{
    (void)event;
    for (;;) {
        size_t index = DESK_BLE_MANAGEMENT_QUEUE_CAPACITY;
        management_command_kind_t command = MANAGEMENT_COMMAND_OPEN_PAIRING;
        char bond_id[DESK_BLE_BOND_ID_TEXT_LENGTH] = {0};
        char alias[DESK_BLE_BOND_ALIAS_BUFFER_LENGTH] = {0};
        portENTER_CRITICAL(&s_management_queue_lock);
        for (size_t i = 0; i < DESK_BLE_MANAGEMENT_QUEUE_CAPACITY; ++i) {
            if (s_management_queue[i].state == MANAGEMENT_SLOT_QUEUED) {
                index = i;
                s_management_queue[i].state = MANAGEMENT_SLOT_PROCESSING;
                command = s_management_queue[i].command;
                memcpy(bond_id, s_management_queue[i].bond_id,
                       sizeof(bond_id));
                memcpy(alias, s_management_queue[i].alias, sizeof(alias));
                break;
            }
        }
        portEXIT_CRITICAL(&s_management_queue_lock);
        if (index == DESK_BLE_MANAGEMENT_QUEUE_CAPACITY) {
            return;
        }

        desk_ble_management_result_t result =
            execute_management_command(command, bond_id, alias);
        bool notify_waiter = false;
        portENTER_CRITICAL(&s_management_queue_lock);
        management_command_slot_t *slot = &s_management_queue[index];
        if (slot->waiter_abandoned) {
            slot->state = MANAGEMENT_SLOT_FREE;
            slot->waiter_abandoned = false;
        } else {
            slot->result = result;
            slot->state = MANAGEMENT_SLOT_DONE;
            notify_waiter = true;
        }
        portEXIT_CRITICAL(&s_management_queue_lock);
        if (notify_waiter) {
            xSemaphoreGive(slot->completion);
        }
    }
}

static void delete_timeout_event_cb(struct ble_npl_event *event)
{
    (void)event;
    uint32_t now_ms = esp_log_timestamp();
    bool still_pending = false;
    for (size_t i = 0; i < DESK_BLE_BOND_CAPACITY; ++i) {
        desk_ble_bond_record_t *record = &s_bond_registry.records[i];
        if (!record->in_use ||
            record->delete_state != DESK_BLE_DELETE_PENDING ||
            !record->delete_waiting_disconnect) {
            continue;
        }
        if (!deadline_reached(now_ms, record->delete_deadline_ms)) {
            still_pending = true;
            continue;
        }
        ESP_LOGE(TAG, "BLE bond disconnect timed out handle=%u generation=%lu",
                 record->delete_conn_handle,
                 (unsigned long)record->delete_generation);
        mark_delete_failed(record, "等待设备断开超时，请重试");
    }
    if (still_pending) {
        arm_delete_timeout_scan();
    }
}

static esp_err_t execute_command(uint16_t conn_handle,
                                 desk_ble_command_t command)
{
    esp_err_t err;
    switch (command) {
    case DESK_BLE_COMMAND_STOP:
#if CONFIG_DESK_MOTION_DIAGNOSTICS
        ESP_LOGI(TAG, "motion ingress source=bluetooth command=stop");
#endif
        cancel_hold_lease();
        desk_ble_session_release_motion(&s_session);
        update_session_aggregates();
        return desk_core_stop();

    case DESK_BLE_COMMAND_HOLD_UP:
        err = desk_core_hold_up(DESK_CONTROL_SOURCE_BLUETOOTH);
        if (err == ESP_OK) {
            if (!desk_ble_session_claim_motion(&s_session, conn_handle)) {
                (void)desk_core_stop();
                return ESP_ERR_INVALID_STATE;
            }
            update_session_aggregates();
            return arm_hold_lease();
        }
        return err;

    case DESK_BLE_COMMAND_HOLD_DOWN:
        err = desk_core_hold_down(DESK_CONTROL_SOURCE_BLUETOOTH);
        if (err == ESP_OK) {
            if (!desk_ble_session_claim_motion(&s_session, conn_handle)) {
                (void)desk_core_stop();
                return ESP_ERR_INVALID_STATE;
            }
            update_session_aggregates();
            return arm_hold_lease();
        }
        return err;

    case DESK_BLE_COMMAND_PRESET_1:
    case DESK_BLE_COMMAND_PRESET_4:
        err = desk_core_goto_preset(
            DESK_CONTROL_SOURCE_BLUETOOTH,
            command == DESK_BLE_COMMAND_PRESET_1 ? 1 : 4);
        if (err == ESP_OK) {
            cancel_hold_lease();
            /* 档位由驱动闭环停止；连接断开前仍由 BLE 会话拥有本次运动。 */
            if (!desk_ble_session_claim_motion(&s_session, conn_handle)) {
                (void)desk_core_stop();
                return ESP_ERR_INVALID_STATE;
            }
            update_session_aggregates();
        }
        return err;
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

static int command_error_to_att(esp_err_t err)
{
    if (err == ESP_ERR_NOT_ALLOWED) {
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }
    if (err == ESP_ERR_INVALID_ARG || err == ESP_ERR_NOT_SUPPORTED) {
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int command_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr_handle;
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    size_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len != 1) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    uint8_t raw = 0;
    if (os_mbuf_copydata(ctxt->om, 0, sizeof(raw), &raw) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    desk_ble_command_t command;
    if (!desk_ble_command_decode(&raw, sizeof(raw), &command)) {
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }
    desk_ble_connection_slot_t *slot =
        desk_ble_session_find(&s_session, conn_handle);
    if (!slot || !slot->encrypted ||
        slot->delete_state == DESK_BLE_DELETE_PENDING) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (command != DESK_BLE_COMMAND_STOP &&
        s_session.motion_owner.valid &&
        !desk_ble_session_is_motion_owner(&s_session, conn_handle,
                                          slot->generation)) {
        ESP_LOGW(TAG, "command 0x%02x rejected: desk busy owner=%u/%lu",
                 raw, s_session.motion_owner.conn_handle,
                 (unsigned long)s_session.motion_owner.generation);
        return DESK_BLE_ATT_ERR_BUSY;
    }
    esp_err_t err = execute_command(conn_handle, command);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "command 0x%02x rejected: %s", raw,
                 esp_err_to_name(err));
        return command_error_to_att(err);
    }
    ESP_LOGI(TAG, "command 0x%02x accepted", raw);
    return 0;
}

static size_t current_state(uint8_t out[DESK_BLE_STATE_LENGTH])
{
    desk_core_snapshot_t snapshot = desk_core_snapshot();
    desk_ble_state_input_t input = {
        .status = (uint8_t)snapshot.status,
        .height_known = snapshot.height_known,
        .height_sim = snapshot.height_sim,
        .child_lock = snapshot.child_lock,
        .bluetooth_enabled =
            (snapshot.enabled_sources & DESK_CONTROL_SOURCE_BIT(
                 DESK_CONTROL_SOURCE_BLUETOOTH)) != 0,
        .upward_blocked = snapshot.upward_blocked,
        .controller_reset_supported = snapshot.controller_reset_supported,
        .controller_reset_active = snapshot.controller_reset_active,
        .controller_reset_recommended =
            snapshot.controller_reset_recommended,
        .height_mm = snapshot.height_mm,
        .max_height_mm = snapshot.max_height_mm,
    };
    return desk_ble_state_encode(&input, out, DESK_BLE_STATE_LENGTH);
}

/** Config 是 desk_core 当前真实设置的只读快照，不在 BLE 层维护第二份状态。 */
static size_t current_config(uint8_t out[DESK_BLE_CONFIG_LENGTH])
{
    desk_core_snapshot_t snapshot = desk_core_snapshot();
    desk_ble_config_input_t input = {
        .child_lock = snapshot.child_lock,
        .rest_enabled =
            (snapshot.enabled_sources & DESK_CONTROL_SOURCE_BIT(
                 DESK_CONTROL_SOURCE_REST)) != 0,
        .bluetooth_enabled =
            (snapshot.enabled_sources & DESK_CONTROL_SOURCE_BIT(
                 DESK_CONTROL_SOURCE_BLUETOOTH)) != 0,
        .panel_enabled =
            (snapshot.enabled_sources & DESK_CONTROL_SOURCE_BIT(
                 DESK_CONTROL_SOURCE_PANEL)) != 0,
        .min_height_mm = snapshot.min_height_mm,
        .max_height_mm = snapshot.max_height_mm,
        .preset1_height_mm = snapshot.preset1_height_mm,
        .preset4_height_mm = snapshot.preset4_height_mm,
    };
    return desk_ble_config_encode(&input, out, DESK_BLE_CONFIG_LENGTH);
}

/** Reminder characteristic 只投影提醒与语音组件快照，不维护 BLE 私有计时状态。 */
static size_t current_reminder(uint8_t out[DESK_BLE_REMINDER_LENGTH])
{
    desk_reminder_snapshot_t reminder = desk_reminder_snapshot();
    desk_audio_snapshot_t audio = desk_audio_snapshot();
    desk_ble_reminder_input_t input = {
        .state = (uint8_t)reminder.state,
        .phase = (uint8_t)reminder.phase,
        .alarm_reason = (uint8_t)reminder.alarm_reason,
        .available = reminder.available,
        .audio_available = audio.available,
        .audio_enabled = audio.enabled,
        .audio_playing = audio.playing,
        .volume_percent = audio.volume_percent,
        .focus_minutes = reminder.config.focus_minutes,
        .short_break_minutes = reminder.config.short_break_minutes,
        .long_break_minutes = reminder.config.long_break_minutes,
        .focuses_per_long_break = reminder.config.focuses_per_long_break,
        .remaining_sec = reminder.remaining_sec,
        .completed_focus_count = reminder.completed_focus_count,
    };
    return desk_ble_reminder_encode(&input, out, DESK_BLE_REMINDER_LENGTH);
}

/** 同一特征 READ/NOTIFY 状态，WRITE 执行动作；写入仍要求加密连接。 */
static int reminder_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr_handle;
    (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t reminder[DESK_BLE_REMINDER_LENGTH];
        size_t len = current_reminder(reminder);
        return os_mbuf_append(ctxt->om, reminder, len) == 0
                   ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    desk_ble_connection_slot_t *slot =
        desk_ble_session_find(&s_session, conn_handle);
    if (!slot || !slot->encrypted ||
        slot->delete_state == DESK_BLE_DELETE_PENDING) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }
    uint8_t raw = 0;
    if (OS_MBUF_PKTLEN(ctxt->om) != sizeof(raw)) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (os_mbuf_copydata(ctxt->om, 0, sizeof(raw), &raw) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    desk_ble_reminder_action_t action;
    if (!desk_ble_reminder_action_decode(&raw, sizeof(raw), &action)) {
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }
    desk_reminder_action_t core_action;
    switch (action) {
    case DESK_BLE_REMINDER_ACTION_START_FOCUS:
        core_action = DESK_REMINDER_ACTION_START_FOCUS;
        break;
    case DESK_BLE_REMINDER_ACTION_START_BREAK:
        core_action = DESK_REMINDER_ACTION_START_BREAK;
        break;
    case DESK_BLE_REMINDER_ACTION_PAUSE:
        core_action = DESK_REMINDER_ACTION_PAUSE;
        break;
    case DESK_BLE_REMINDER_ACTION_RESUME:
        core_action = DESK_REMINDER_ACTION_RESUME;
        break;
    case DESK_BLE_REMINDER_ACTION_SKIP:
        core_action = DESK_REMINDER_ACTION_SKIP;
        break;
    case DESK_BLE_REMINDER_ACTION_STOP:
        core_action = DESK_REMINDER_ACTION_STOP;
        break;
    case DESK_BLE_REMINDER_ACTION_SNOOZE:
        core_action = DESK_REMINDER_ACTION_SNOOZE;
        break;
    default:
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }
    esp_err_t err = desk_reminder_perform(core_action);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "reminder action=0x%02x rejected: %s", raw,
                 esp_err_to_name(err));
        return command_error_to_att(err);
    }
    ESP_LOGI(TAG, "reminder action=0x%02x accepted", raw);
    return 0;
}

static int state_access(uint16_t conn_handle, uint16_t attr_handle,
                        struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint8_t state[DESK_BLE_STATE_LENGTH];
    size_t len = current_state(state);
    return os_mbuf_append(ctxt->om, state, len) == 0
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static esp_err_t execute_config_write(const desk_ble_config_write_t *write)
{
    switch (write->field) {
    case DESK_BLE_CONFIG_FIELD_CHILD_LOCK:
        return desk_core_set_child_lock(write->value != 0);
    case DESK_BLE_CONFIG_FIELD_REST_ENABLED:
        return desk_core_set_source_enabled(DESK_CONTROL_SOURCE_REST,
                                            write->value != 0);
    case DESK_BLE_CONFIG_FIELD_BLUETOOTH_ENABLED:
        return desk_core_set_source_enabled(DESK_CONTROL_SOURCE_BLUETOOTH,
                                            write->value != 0);
    case DESK_BLE_CONFIG_FIELD_PANEL_ENABLED:
        return desk_core_set_source_enabled(DESK_CONTROL_SOURCE_PANEL,
                                            write->value != 0);
    case DESK_BLE_CONFIG_FIELD_MAX_HEIGHT_MM:
        return desk_core_set_max_height_mm((int)write->value);
    case DESK_BLE_CONFIG_FIELD_MIN_HEIGHT_MM:
        return desk_core_set_min_height_mm((int)write->value);
    case DESK_BLE_CONFIG_FIELD_PRESET1_HEIGHT_MM: {
        desk_core_snapshot_t snapshot = desk_core_snapshot();
        return desk_core_set_preset_heights_mm((int)write->value,
                                                snapshot.preset4_height_mm);
    }
    case DESK_BLE_CONFIG_FIELD_PRESET4_HEIGHT_MM: {
        desk_core_snapshot_t snapshot = desk_core_snapshot();
        return desk_core_set_preset_heights_mm(snapshot.preset1_height_mm,
                                                (int)write->value);
    }
    default:
        return ESP_ERR_INVALID_ARG;
    }
}

/**
 * Config 的 READ 返回完整快照，WRITE 只更新一个字段。
 *
 * 管理写入不经过来源开关：否则关闭 Bluetooth 后将无法用同一加密连接重新开启；
 * 但所有会运动的命令仍统一受童锁与 Bluetooth 来源权限约束。
 */
static int config_access(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr_handle;
    (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t config[DESK_BLE_CONFIG_LENGTH];
        size_t len = current_config(config);
        return os_mbuf_append(ctxt->om, config, len) == 0
                   ? 0
                   : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    desk_ble_connection_slot_t *slot =
        desk_ble_session_find(&s_session, conn_handle);
    if (!slot || !slot->encrypted ||
        slot->delete_state == DESK_BLE_DELETE_PENDING) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }

    uint8_t raw[DESK_BLE_CONFIG_WRITE_LENGTH];
    size_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len != sizeof(raw)) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (os_mbuf_copydata(ctxt->om, 0, sizeof(raw), raw) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    desk_ble_config_write_t write;
    if (!desk_ble_config_write_decode(raw, sizeof(raw), &write)) {
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }
    esp_err_t err = execute_config_write(&write);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "config field=0x%02x value=%u rejected: %s",
                 (unsigned)write.field, (unsigned)write.value,
                 esp_err_to_name(err));
        return command_error_to_att(err);
    }
    ESP_LOGI(TAG, "config field=0x%02x value=%u accepted",
             (unsigned)write.field, (unsigned)write.value);
    return 0;
}

/** 给 ATT Write Response 留出发送时间，再执行芯片软重启。 */
static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(DESK_BLE_RESTART_DELAY_MS));
    ESP_LOGW(TAG, "restarting by encrypted BLE request");
    esp_restart();
}

static int system_access(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr_handle;
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    desk_ble_connection_slot_t *slot =
        desk_ble_session_find(&s_session, conn_handle);
    if (!slot || !slot->encrypted ||
        slot->delete_state == DESK_BLE_DELETE_PENDING) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }

    uint8_t raw = 0;
    if (OS_MBUF_PKTLEN(ctxt->om) != sizeof(raw)) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (os_mbuf_copydata(ctxt->om, 0, sizeof(raw), &raw) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    desk_ble_system_command_t command;
    if (!desk_ble_system_command_decode(&raw, sizeof(raw), &command)) {
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }
    if (command == DESK_BLE_SYSTEM_COMMAND_RESTART && s_restart_pending) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    esp_err_t err;
    if (command == DESK_BLE_SYSTEM_COMMAND_RESET_CONTROLLER) {
        err = desk_core_reset_controller(DESK_CONTROL_SOURCE_BLUETOOTH);
    } else {
        err = desk_core_stop();
    }
    if (err == ESP_OK && command == DESK_BLE_SYSTEM_COMMAND_RESTART) {
        s_restart_pending = true;
        if (xTaskCreate(restart_task, "ble_restart", 2048, NULL,
                        tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
            s_restart_pending = false;
            err = ESP_ERR_NO_MEM;
        }
    }
    if (err != ESP_OK) {
        return command_error_to_att(err);
    }
    ESP_LOGI(TAG, "system command=0x%02x accepted", (unsigned)command);
    return 0;
}

/**
 * 客户端只登记协议版本和平台类型；Identity 始终由加密连接描述解析，
 * 不接收客户端上传的地址或名称。
 */
static int client_info_access(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr_handle;
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    desk_ble_connection_slot_t *slot =
        desk_ble_session_find(&s_session, conn_handle);
    if (!slot || !slot->encrypted || !slot->peer_identity_valid ||
        slot->delete_state == DESK_BLE_DELETE_PENDING) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }
    uint8_t raw[DESK_BLE_CLIENT_INFO_LENGTH];
    if (OS_MBUF_PKTLEN(ctxt->om) != sizeof(raw)) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (os_mbuf_copydata(ctxt->om, 0, sizeof(raw), raw) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    desk_ble_client_info_t info;
    if (!desk_ble_client_info_decode(raw, sizeof(raw), &info)) {
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }
    desk_ble_client_kind_t kind =
        (desk_ble_client_kind_t)info.client_kind;
    desk_ble_bond_record_t *record = desk_ble_bond_registry_find_identity(
        &s_bond_registry, &slot->peer_identity);
    if (!record) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    desk_ble_client_kind_t previous = record->client_kind;
    record->client_kind = kind;
    esp_err_t err = desk_ble_bond_storage_save(&s_bond_registry);
    if (err != ESP_OK) {
        record->client_kind = previous;
        ESP_LOGE(TAG, "save client info failed: %s", esp_err_to_name(err));
        return BLE_ATT_ERR_UNLIKELY;
    }
    slot->client_kind = kind;
    publish_management_snapshot();
    ESP_LOGI(TAG, "client info handle=%u kind=%s", conn_handle,
             desk_ble_client_kind_name(kind));
    return 0;
}

static int presence_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr_handle;
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    desk_ble_connection_slot_t *slot =
        desk_ble_session_find(&s_session, conn_handle);
    if (!slot || !slot->encrypted || !slot->peer_identity_valid ||
        slot->delete_state == DESK_BLE_DELETE_PENDING) {
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }
    uint8_t raw[DESK_BLE_PRESENCE_LENGTH];
    if (OS_MBUF_PKTLEN(ctxt->om) != sizeof(raw)) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (os_mbuf_copydata(ctxt->om, 0, sizeof(raw), raw) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    desk_ble_presence_t presence;
    if (!desk_ble_presence_decode(raw, sizeof(raw), &presence)) {
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }
    const desk_ble_bond_record_t *record =
        desk_ble_bond_registry_find_identity_const(&s_bond_registry,
                                                   &slot->peer_identity);
    char connected_bond_id[DESK_BLE_BOND_ID_TEXT_LENGTH];
    if (!record ||
        !desk_ble_bond_format_id(record, connected_bond_id,
                                 sizeof(connected_bond_id)) ||
        strcmp(connected_bond_id, presence.device_id) != 0) {
        /* 加密只证明连接已授权；这里还要阻止其他已授权手机代报选中设备。 */
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }
    esp_err_t err = desk_core_auto_child_lock_heartbeat(presence.device_id);
    return err == ESP_OK ? 0 : command_error_to_att(err);
}

/** Expose build time and Git-derived app version without coupling clients to HTTP. */
static int firmware_revision_access(uint16_t conn_handle, uint16_t attr_handle,
                                    struct ble_gatt_access_ctxt *ctxt,
                                    void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    const esp_app_desc_t *app = esp_app_get_description();
    if (!app) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    char revision[DESK_BLE_FIRMWARE_REVISION_MAX_LEN];
    int length = snprintf(
        revision, sizeof(revision), "%s %s @ %s", app->date, app->time,
        app->version);
    if (length < 0 || (size_t)length >= sizeof(revision)) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return os_mbuf_append(ctxt->om, revision, (uint16_t)length) == 0
               ? 0
               : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static const struct ble_gatt_chr_def s_characteristics[] = {
    {
        .uuid = &s_command_uuid.u,
        .access_cb = command_access,
        /* 写入必须先完成 Just Works 配对并建立加密连接。 */
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
    },
    {
        .uuid = &s_state_uuid.u,
        .access_cb = state_access,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_state_value_handle,
    },
    {
        .uuid = &s_config_uuid.u,
        .access_cb = config_access,
        /* 读取用于展示真实状态；写入必须在配对加密后才能执行。 */
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY |
                 BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
        .val_handle = &s_config_value_handle,
    },
    {
        .uuid = &s_system_uuid.u,
        .access_cb = system_access,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
    },
    {
        .uuid = &s_client_info_uuid.u,
        .access_cb = client_info_access,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
    },
    {
        .uuid = &s_presence_uuid.u,
        .access_cb = presence_access,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
    },
    {
        .uuid = &s_reminder_uuid.u,
        .access_cb = reminder_access,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY |
                 BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
        .val_handle = &s_reminder_value_handle,
    },
    {0},
};

static const struct ble_gatt_chr_def s_device_information_characteristics[] = {
    {
        .uuid = &s_firmware_revision_uuid.u,
        .access_cb = firmware_revision_access,
        .flags = BLE_GATT_CHR_F_READ,
    },
    {0},
};

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics = s_characteristics,
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_device_information_service_uuid.u,
        .characteristics = s_device_information_characteristics,
    },
    {0},
};

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            desk_ble_connection_slot_t *slot = desk_ble_session_connect(
                &s_session, event->connect.conn_handle);
            if (!slot) {
                ESP_LOGE(TAG, "no session slot for handle=%u",
                         event->connect.conn_handle);
                (void)ble_gap_terminate(event->connect.conn_handle,
                                       BLE_ERR_CONN_LIMIT);
                return 0;
            }
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(slot->conn_handle, &desc) == 0) {
                populate_slot_identity(slot, &desc.peer_id_addr);
            }
            update_session_aggregates();
            ESP_LOGI(TAG, "client connected handle=%u generation=%lu count=%u",
                     slot->conn_handle, (unsigned long)slot->generation,
                     (unsigned)s_connection_count);
            start_advertising();
        } else {
            ESP_LOGW(TAG, "connect failed status=%d", event->connect.status);
            start_advertising();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
    {
        uint16_t conn_handle = event->disconnect.conn.conn_handle;
        desk_ble_connection_slot_t *slot =
            desk_ble_session_find(&s_session, conn_handle);
        bool was_owner = false;
        if (slot) {
            uint32_t generation = slot->generation;
            desk_ble_peer_identity_t identity = slot->peer_identity;
            bool identity_valid = slot->peer_identity_valid;
            if (identity_valid) {
                report_bond_presence(&identity, false);
            }
            desk_ble_bond_record_t *record =
                identity_valid
                    ? desk_ble_bond_registry_find_identity(&s_bond_registry,
                                                           &identity)
                    : NULL;
            desk_ble_bond_record_t *pending_by_handle =
                find_pending_delete_by_handle(conn_handle);
            if (pending_by_handle &&
                pending_by_handle->delete_generation != generation) {
                ESP_LOGW(TAG,
                         "ignore stale disconnect handle=%u expected_generation=%lu current=%lu",
                         conn_handle,
                         (unsigned long)pending_by_handle->delete_generation,
                         (unsigned long)generation);
                return 0;
            }
            bool complete_delete = record &&
                desk_ble_bond_delete_matches_disconnect(
                    record, conn_handle, generation);
            if (desk_ble_session_disconnect(&s_session, conn_handle,
                                            generation, &was_owner) &&
                was_owner) {
                cancel_hold_lease();
                ESP_LOGW(TAG, "BLE owner disconnected -> stop");
                (void)desk_core_stop();
            }
            if (complete_delete) {
                (void)delete_bond_from_store(&identity);
            }
        }
        update_session_aggregates();
        ESP_LOGI(TAG, "client disconnected handle=%u reason=%d count=%u",
                 conn_handle, event->disconnect.reason,
                 (unsigned)s_connection_count);
        start_advertising();
        return 0;
    }

    case BLE_GAP_EVENT_SUBSCRIBE:
    {
        desk_ble_connection_slot_t *slot = desk_ble_session_find(
            &s_session, event->subscribe.conn_handle);
        if (!slot) {
            ESP_LOGW(TAG, "subscribe from unknown handle=%u",
                     event->subscribe.conn_handle);
            return 0;
        }
        if (event->subscribe.attr_handle == s_state_value_handle) {
            slot->state_subscribed = event->subscribe.cur_notify != 0;
            ESP_LOGI(TAG, "state notify handle=%u -> %d",
                     slot->conn_handle, (int)slot->state_subscribed);
            if (slot->state_subscribed) {
                ble_gatts_chr_updated(s_state_value_handle);
            }
        } else if (event->subscribe.attr_handle == s_config_value_handle) {
            slot->config_subscribed = event->subscribe.cur_notify != 0;
            ESP_LOGI(TAG, "config notify handle=%u -> %d",
                     slot->conn_handle, (int)slot->config_subscribed);
            if (slot->config_subscribed) {
                ble_gatts_chr_updated(s_config_value_handle);
            }
        } else if (event->subscribe.attr_handle == s_reminder_value_handle) {
            slot->reminder_subscribed = event->subscribe.cur_notify != 0;
            ESP_LOGI(TAG, "reminder notify handle=%u -> %d",
                     slot->conn_handle, (int)slot->reminder_subscribed);
            if (slot->reminder_subscribed) {
                ble_gatts_chr_updated(s_reminder_value_handle);
            }
        }
        update_session_aggregates();
        return 0;
    }

    case BLE_GAP_EVENT_ENC_CHANGE:
    {
        desk_ble_connection_slot_t *slot = desk_ble_session_find(
            &s_session, event->enc_change.conn_handle);
        if (!slot) {
            return 0;
        }
        slot->encrypted = false;
        ESP_LOGI(TAG, "encryption changed handle=%u status=%d",
                 event->enc_change.conn_handle, event->enc_change.status);
        if (event->enc_change.status != 0) {
            return 0;
        }
        struct ble_gap_conn_desc desc;
        int rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        if (rc != 0 || !desc.sec_state.bonded) {
            ESP_LOGW(TAG, "encrypted connection is not bonded handle=%u rc=%d",
                     event->enc_change.conn_handle, rc);
            (void)ble_gap_terminate(event->enc_change.conn_handle,
                                   BLE_ERR_AUTH_FAIL);
            return 0;
        }
        desk_ble_peer_identity_t identity =
            identity_from_ble_addr(&desc.peer_id_addr);
        bool existing = desk_ble_bond_registry_find_identity_const(
                            &s_bond_registry, &identity) != NULL;
        if (!existing && !desk_ble_session_allows_new_pairing(
                             &s_session, esp_log_timestamp(),
                             desk_ble_bond_registry_count(&s_bond_registry),
                             DESK_BLE_BOND_CAPACITY)) {
            ESP_LOGW(TAG, "reject new bond outside pairing window handle=%u",
                     event->enc_change.conn_handle);
            (void)ble_store_util_delete_peer(&desc.peer_id_addr);
            (void)ble_gap_terminate(event->enc_change.conn_handle,
                                   BLE_ERR_AUTH_FAIL);
            return 0;
        }
        esp_err_t metadata_err = ensure_bond_metadata(&identity);
        if (metadata_err != ESP_OK) {
            ESP_LOGE(TAG, "persist new bond metadata failed: %s",
                     esp_err_to_name(metadata_err));
            if (!existing) {
                (void)ble_store_util_delete_peer(&desc.peer_id_addr);
            }
            (void)ble_gap_terminate(event->enc_change.conn_handle,
                                   BLE_ERR_AUTH_FAIL);
            return 0;
        }
        slot->peer_identity_valid = true;
        slot->peer_identity = identity;
        const desk_ble_bond_record_t *record =
            desk_ble_bond_registry_find_identity_const(&s_bond_registry,
                                                       &identity);
        slot->client_kind = record ? record->client_kind
                                   : DESK_BLE_CLIENT_UNKNOWN;
        slot->encrypted = true;
        report_bond_presence(&identity, true);
        if (!existing &&
            desk_ble_bond_registry_count(&s_bond_registry) >=
                DESK_BLE_BOND_CAPACITY) {
            desk_ble_session_close_pairing_window(&s_session);
            ESP_LOGI(TAG, "pairing window closed: bond capacity reached");
        }
        publish_management_snapshot();
        return 0;
    }

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        /*
         * iOS“忽略此设备”只会删除手机端密钥。ESP32 仍保留旧 bond 时，
         * 必须删除该 peer 的旧密钥并让 NimBLE 重试，否则首个加密写会一直
         * 卡在配对阶段。这里只删除当前 peer，不影响 Wi-Fi 或桌子设置。
         */
        if (!desk_ble_session_pairing_window_is_open(
                &s_session, esp_log_timestamp())) {
            ESP_LOGW(TAG, "repeat pairing rejected: pairing window closed");
            return BLE_GAP_REPEAT_PAIRING_IGNORE;
        }
        struct ble_gap_conn_desc desc;
        int rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        if (rc != 0) {
            ESP_LOGE(TAG, "repeat pairing peer lookup failed: %d", rc);
            return BLE_GAP_REPEAT_PAIRING_IGNORE;
        }
        rc = ble_store_util_delete_peer(&desc.peer_id_addr);
        if (rc != 0) {
            ESP_LOGE(TAG, "delete stale BLE bond failed: %d", rc);
            return BLE_GAP_REPEAT_PAIRING_IGNORE;
        }
        desk_ble_peer_identity_t identity =
            identity_from_ble_addr(&desc.peer_id_addr);
        desk_ble_bond_registry_t candidate = s_bond_registry;
        if (desk_ble_bond_registry_remove(&candidate, &identity)) {
            esp_err_t err = desk_ble_bond_storage_save(&candidate);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "remove repeated bond metadata failed: %s",
                         esp_err_to_name(err));
                /*
                 * Store 密钥已经删除，不能继续把旧记录伪装成正常 Bond。
                 * 保留记录并暴露 failed 状态，让管理端可重试；重启时的
                 * Store/NVS 对账也会清理这条已经失去密钥的元数据。
                 */
                desk_ble_bond_record_t *record =
                    desk_ble_bond_registry_find_identity(&s_bond_registry,
                                                         &identity);
                mark_delete_failed(
                    record, "重复配对元数据清理失败，请在设备管理中重试");
                return BLE_GAP_REPEAT_PAIRING_IGNORE;
            }
            s_bond_registry = candidate;
            publish_management_snapshot();
        }
        desk_ble_connection_slot_t *slot = desk_ble_session_find(
            &s_session, event->repeat_pairing.conn_handle);
        if (slot) {
            slot->encrypted = false;
            slot->peer_identity_valid = false;
            slot->client_kind = DESK_BLE_CLIENT_UNKNOWN;
        }
        ESP_LOGW(TAG, "stale BLE bond and metadata removed; retry pairing");
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        return 0;

    default:
        return 0;
    }
}

static void start_advertising(void)
{
    if (!s_stack_synced ||
        s_connection_count >= DESK_BLE_MAX_CONNECTIONS ||
        ble_gap_adv_active()) {
        return;
    }
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&s_service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "set advertising fields failed: %d", rc);
        return;
    }

    const char *name = ble_svc_gap_device_name();
    struct ble_hs_adv_fields response = {0};
    response.name = (uint8_t *)name;
    response.name_len = strlen(name);
    response.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&response);
    if (rc != 0) {
        ESP_LOGE(TAG, "set scan response failed: %d", rc);
        return;
    }

    struct ble_gap_adv_params params = {0};
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params,
                           gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "start advertising failed: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "advertising as %s", DESK_BLE_DEVICE_NAME);
}

static void on_stack_reset(int reason)
{
    s_stack_synced = false;
    stop_owned_motion("NimBLE stack reset");
    desk_ble_session_init(&s_session);
    update_session_aggregates();
    ESP_LOGE(TAG, "stack reset reason=%d", reason);
}

static void on_stack_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) {
        rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "resolve BLE address failed: %d", rc);
        return;
    }
    esp_err_t metadata_err = reconcile_bond_metadata();
    if (metadata_err != ESP_OK) {
        ESP_LOGE(TAG, "BLE bond startup reconciliation failed: %s",
                 esp_err_to_name(metadata_err));
        return;
    }
    publish_management_snapshot();
    s_stack_synced = true;
    start_advertising();
}

/** Store 容量不足只让本次写入失败，禁止示例 callback 淘汰最旧 Bond。 */
static int bond_store_status(struct ble_store_status_event *event, void *arg)
{
    (void)arg;
    if (!event) {
        return BLE_HS_EINVAL;
    }
    switch (event->event_code) {
    case BLE_STORE_EVENT_FULL:
        /*
         * FULL 是配对前的悲观预警：第三个合法 Bond 正好达到容量时也会触发。
         * 仅在窗口有效且注册表仍有空位时允许继续；真正写溢出仍在下方拒绝。
         */
        if (desk_ble_session_allows_store_event(
                &s_session, DESK_BLE_STORE_FULL, esp_log_timestamp(),
                desk_ble_bond_registry_count(&s_bond_registry),
                DESK_BLE_BOND_CAPACITY)) {
            return 0;
        }
        ESP_LOGW(TAG, "BLE bond store full outside admission window");
        return BLE_HS_ESTORE_CAP;
    case BLE_STORE_EVENT_OVERFLOW:
        ESP_LOGW(TAG, "BLE bond store overflow rejected without eviction");
        return BLE_HS_ESTORE_CAP;
    default:
        return BLE_HS_EUNKNOWN;
    }
}

static void nimble_host_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "NimBLE host started");
    nimble_port_run();
    /* 同官方示例配对：退出 host loop 时同时关闭 controller 和 host task。 */
    nimble_port_freertos_deinit();
}

static void state_notify_task(void *arg)
{
    (void)arg;
    uint8_t previous[DESK_BLE_STATE_LENGTH] = {0};
    uint8_t previous_config[DESK_BLE_CONFIG_LENGTH] = {0};
    uint8_t previous_reminder[DESK_BLE_REMINDER_LENGTH] = {0};
    bool previous_valid = false;
    bool previous_config_valid = false;
    bool previous_reminder_valid = false;
    uint32_t last_notify_ms = 0;

    for (;;) {
        uint8_t state[DESK_BLE_STATE_LENGTH];
        current_state(state);
        uint8_t config[DESK_BLE_CONFIG_LENGTH];
        current_config(config);
        uint8_t reminder[DESK_BLE_REMINDER_LENGTH];
        current_reminder(reminder);
        if (state[1] == DESK_STATUS_IDLE && s_motion_owner_active) {
            /* 清掉已闭环结束的档位所有权，避免之后断连误停其他入口。 */
            queue_owner_release();
        }

        uint32_t now = esp_log_timestamp();
        bool changed = !previous_valid ||
                       memcmp(previous, state, sizeof(state)) != 0;
        bool heartbeat = (uint32_t)(now - last_notify_ms) >=
                         DESK_BLE_STATE_HEARTBEAT_MS;
        if (s_stack_synced && s_connection_count > 0 &&
            s_any_state_subscribed &&
            (changed || heartbeat)) {
            ble_gatts_chr_updated(s_state_value_handle);
            last_notify_ms = now;
        }
        bool config_changed = !previous_config_valid ||
                              memcmp(previous_config, config,
                                     sizeof(config)) != 0;
        if (s_stack_synced && s_connection_count > 0 &&
            s_any_config_subscribed &&
            config_changed) {
            ble_gatts_chr_updated(s_config_value_handle);
        }
        bool reminder_changed = !previous_reminder_valid ||
            memcmp(previous_reminder, reminder, sizeof(reminder)) != 0;
        if (s_stack_synced && s_connection_count > 0 &&
            s_any_reminder_subscribed && reminder_changed) {
            ble_gatts_chr_updated(s_reminder_value_handle);
        }
        memcpy(previous, state, sizeof(previous));
        memcpy(previous_config, config, sizeof(previous_config));
        memcpy(previous_reminder, reminder, sizeof(previous_reminder));
        previous_valid = true;
        previous_config_valid = true;
        previous_reminder_valid = true;
        vTaskDelay(pdMS_TO_TICKS(DESK_BLE_STATE_POLL_MS));
    }
}

static void init_management_queue(void)
{
    memset(s_management_queue, 0, sizeof(s_management_queue));
    for (size_t i = 0; i < DESK_BLE_MANAGEMENT_QUEUE_CAPACITY; ++i) {
        s_management_queue[i].completion = xSemaphoreCreateBinaryStatic(
            &s_management_queue[i].completion_storage);
    }
    ble_npl_event_init(&s_management_event, management_event_cb, NULL);
    s_management_ready = true;
}

static desk_ble_management_result_t submit_management_command(
    management_command_kind_t command, const char *bond_id,
    const char *alias)
{
    if (!s_management_ready) {
        return DESK_BLE_MANAGEMENT_INTERNAL_ERROR;
    }
    if (command == MANAGEMENT_COMMAND_SET_ALIAS &&
        !desk_ble_bond_alias_valid(alias)) {
        return DESK_BLE_MANAGEMENT_INVALID_ARGUMENT;
    }
    if ((command == MANAGEMENT_COMMAND_DELETE_ONE ||
         command == MANAGEMENT_COMMAND_SET_ALIAS) &&
        (!bond_id || strlen(bond_id) >= DESK_BLE_BOND_ID_TEXT_LENGTH)) {
        return DESK_BLE_MANAGEMENT_NOT_FOUND;
    }

    size_t index = DESK_BLE_MANAGEMENT_QUEUE_CAPACITY;
    portENTER_CRITICAL(&s_management_queue_lock);
    for (size_t i = 0; i < DESK_BLE_MANAGEMENT_QUEUE_CAPACITY; ++i) {
        if (s_management_queue[i].state == MANAGEMENT_SLOT_FREE) {
            index = i;
            management_command_slot_t *slot = &s_management_queue[i];
            slot->state = MANAGEMENT_SLOT_QUEUED;
            slot->waiter_abandoned = false;
            slot->command = command;
            slot->bond_id[0] = '\0';
            slot->alias[0] = '\0';
            if (bond_id) {
                memcpy(slot->bond_id, bond_id, strlen(bond_id) + 1);
            }
            if (alias) {
                memcpy(slot->alias, alias, strlen(alias) + 1);
            }
            break;
        }
    }
    portEXIT_CRITICAL(&s_management_queue_lock);
    if (index == DESK_BLE_MANAGEMENT_QUEUE_CAPACITY) {
        return DESK_BLE_MANAGEMENT_INTERNAL_ERROR;
    }

    management_command_slot_t *slot = &s_management_queue[index];
    /* 槽位只有在上一次 waiter 已消费后才回到 FREE，这里仅做防御性排空。 */
    (void)xSemaphoreTake(slot->completion, 0);
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_management_event);
    if (xSemaphoreTake(slot->completion,
                       pdMS_TO_TICKS(DESK_BLE_MANAGEMENT_WAIT_MS)) == pdTRUE) {
        desk_ble_management_result_t result =
            DESK_BLE_MANAGEMENT_INTERNAL_ERROR;
        portENTER_CRITICAL(&s_management_queue_lock);
        if (slot->state == MANAGEMENT_SLOT_DONE) {
            result = slot->result;
            slot->state = MANAGEMENT_SLOT_FREE;
        } else {
            slot->waiter_abandoned = true;
        }
        portEXIT_CRITICAL(&s_management_queue_lock);
        return result;
    }

    /* 超时后命令可能仍在 Host 执行；槽位由 Host 完成时回收，避免悬空 waiter。 */
    desk_ble_management_result_t result = DESK_BLE_MANAGEMENT_INTERNAL_ERROR;
    portENTER_CRITICAL(&s_management_queue_lock);
    if (slot->state == MANAGEMENT_SLOT_DONE) {
        result = slot->result;
        slot->state = MANAGEMENT_SLOT_FREE;
    } else {
        slot->waiter_abandoned = true;
    }
    portEXIT_CRITICAL(&s_management_queue_lock);
    return result;
}

bool desk_ble_get_management_snapshot(
    desk_ble_management_snapshot_t *out_snapshot)
{
    if (!out_snapshot || !s_management_ready) {
        return false;
    }
    uint32_t deadline_ms;
    portENTER_CRITICAL(&s_management_snapshot_lock);
    *out_snapshot = s_management_snapshot;
    deadline_ms = s_management_pairing_deadline_ms;
    portEXIT_CRITICAL(&s_management_snapshot_lock);

    if (out_snapshot->pairing_window_open) {
        uint32_t now_ms = esp_log_timestamp();
        if (deadline_reached(now_ms, deadline_ms)) {
            out_snapshot->pairing_window_open = false;
            out_snapshot->pairing_window_remaining_seconds = 0;
        } else {
            uint32_t remaining_ms = deadline_ms - now_ms;
            out_snapshot->pairing_window_remaining_seconds =
                (remaining_ms + 999U) / 1000U;
        }
    }
    return true;
}

desk_ble_management_result_t desk_ble_open_pairing_window(void)
{
    return submit_management_command(MANAGEMENT_COMMAND_OPEN_PAIRING, NULL,
                                     NULL);
}

desk_ble_management_result_t desk_ble_close_pairing_window(void)
{
    return submit_management_command(MANAGEMENT_COMMAND_CLOSE_PAIRING, NULL,
                                     NULL);
}

desk_ble_management_result_t desk_ble_delete_bond(const char *bond_id)
{
    return submit_management_command(MANAGEMENT_COMMAND_DELETE_ONE, bond_id,
                                     NULL);
}

desk_ble_management_result_t desk_ble_delete_all_bonds(void)
{
    return submit_management_command(MANAGEMENT_COMMAND_DELETE_ALL, NULL,
                                     NULL);
}

desk_ble_management_result_t desk_ble_set_bond_alias(const char *bond_id,
                                                     const char *alias)
{
    if (!desk_ble_bond_alias_valid(alias)) {
        return DESK_BLE_MANAGEMENT_INVALID_ARGUMENT;
    }
    return submit_management_command(MANAGEMENT_COMMAND_SET_ALIAS, bond_id,
                                     alias);
}

esp_err_t desk_ble_start(void)
{
    s_management_ready = false;
    desk_ble_session_init(&s_session);
    update_session_aggregates();
    esp_err_t storage_err = desk_ble_bond_storage_load(&s_bond_registry);
    if (storage_err == ESP_ERR_INVALID_VERSION ||
        storage_err == ESP_ERR_INVALID_STATE) {
        /* Bond Store 会在 sync 时重建安全的 unknown 元数据。 */
        ESP_LOGW(TAG, "discard invalid BLE bond metadata: %s",
                 esp_err_to_name(storage_err));
        desk_ble_bond_registry_init(&s_bond_registry);
    } else if (storage_err != ESP_OK) {
        return storage_err;
    }
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        return err;
    }
    ble_npl_callout_init(&s_hold_lease_callout,
                         nimble_port_get_dflt_eventq(),
                         hold_lease_event_cb, NULL);
    ble_npl_callout_init(&s_delete_timeout_callout,
                         nimble_port_get_dflt_eventq(),
                         delete_timeout_event_cb, NULL);
    ble_npl_event_init(&s_core_event, core_event_cb, NULL);
    desk_core_set_event_listener(core_event_listener, NULL);

    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_svc_gap_device_name_set(DESK_BLE_DEVICE_NAME);
    if (rc == 0) {
        rc = ble_gatts_count_cfg(s_services);
    }
    if (rc == 0) {
        rc = ble_gatts_add_svcs(s_services);
    }
    if (rc != 0) {
        ESP_LOGE(TAG, "register GATT service failed: %d", rc);
        return ESP_FAIL;
    }

    ble_hs_cfg.reset_cb = on_stack_reset;
    ble_hs_cfg.sync_cb = on_stack_sync;
    ble_hs_cfg.store_status_cb = bond_store_status;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0; /* 无屏幕/键盘设备只能使用 Just Works。 */
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist |=
        BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist |=
        BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();
    init_management_queue();
    publish_management_snapshot();

    /* 该入口不仅创建 host task，还会启用 ESP32 蓝牙控制器。 */
    nimble_port_freertos_init(nimble_host_task);
    if (xTaskCreate(state_notify_task, "ble_state", 3072, NULL, 4, NULL) !=
        pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "GATT ready lease=%d ms", CONFIG_DESK_BLE_HOLD_LEASE_MS);
    return ESP_OK;
}
