/**
 * @file desk_ble.c
 * @brief Desk Gateway 原生 NimBLE GATT Server。
 *
 * BLE 只负责入口协议和连接生命周期，所有运动仍进入 desk_core，从而统一
 * 执行童锁、Bluetooth 来源权限、最高安全高度和运动超时策略。
 */
#include "desk_ble.h"

#include "desk_ble_protocol.h"
#include "desk_ble_session.h"
#include "desk_core.h"

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
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
/* Bluetooth SIG Device Information Service / Firmware Revision String. */
static const ble_uuid16_t s_device_information_service_uuid =
    BLE_UUID16_INIT(0x180a);
static const ble_uuid16_t s_firmware_revision_uuid = BLE_UUID16_INIT(0x2a26);

static const char *TAG = "desk_ble";
static uint8_t s_own_addr_type;
static uint16_t s_state_value_handle;
static uint16_t s_config_value_handle;
static desk_ble_session_t s_session;
static struct ble_npl_callout s_hold_lease_callout;
static struct ble_npl_event s_core_event;
static portMUX_TYPE s_core_event_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_core_release_pending;
static bool s_core_event_posted;
static volatile bool s_stack_synced;
static volatile size_t s_connection_count;
static volatile bool s_any_state_subscribed;
static volatile bool s_any_config_subscribed;
static volatile bool s_motion_owner_active;
static volatile bool s_restart_pending;

static void start_advertising(void);

/** Host 上下文每次改槽位后刷新只读聚合值，通知任务不直接读会话表。 */
static void update_session_aggregates(void)
{
    bool any_state = false;
    bool any_config = false;
    for (size_t i = 0; i < DESK_BLE_MAX_CONNECTIONS; ++i) {
        const desk_ble_connection_slot_t *slot = &s_session.slots[i];
        if (!slot->in_use) {
            continue;
        }
        any_state |= slot->state_subscribed;
        any_config |= slot->config_subscribed;
    }
    s_connection_count = desk_ble_session_connection_count(&s_session);
    s_any_state_subscribed = any_state;
    s_any_config_subscribed = any_config;
    s_motion_owner_active = s_session.motion_owner.valid;
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
    ble_npl_error_t rc = ble_npl_callout_reset(
        &s_hold_lease_callout,
        pdMS_TO_TICKS(CONFIG_DESK_BLE_HOLD_LEASE_MS));
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
    if (!slot || slot->delete_state == DESK_BLE_DELETE_PENDING) {
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
        .max_height_mm = snapshot.max_height_mm,
        .preset1_height_mm = snapshot.preset1_height_mm,
        .preset4_height_mm = snapshot.preset4_height_mm,
    };
    return desk_ble_config_encode(&input, out, DESK_BLE_CONFIG_LENGTH);
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
    (void)conn_handle;
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
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
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
    if (s_restart_pending) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    esp_err_t err = desk_core_stop();
    if (err == ESP_OK) {
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
    ESP_LOGI(TAG, "system restart accepted");
    return 0;
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
            if (desk_ble_session_disconnect(&s_session, conn_handle,
                                            generation, &was_owner) &&
                was_owner) {
                cancel_hold_lease();
                ESP_LOGW(TAG, "BLE owner disconnected -> stop");
                (void)desk_core_stop();
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
        }
        update_session_aggregates();
        return 0;
    }

    case BLE_GAP_EVENT_ENC_CHANGE:
    {
        desk_ble_connection_slot_t *slot = desk_ble_session_find(
            &s_session, event->enc_change.conn_handle);
        if (slot) {
            slot->encrypted = event->enc_change.status == 0;
        }
        ESP_LOGI(TAG, "encryption changed handle=%u status=%d",
                 event->enc_change.conn_handle, event->enc_change.status);
        return 0;
    }

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        /*
         * iOS“忽略此设备”只会删除手机端密钥。ESP32 仍保留旧 bond 时，
         * 必须删除该 peer 的旧密钥并让 NimBLE 重试，否则首个加密写会一直
         * 卡在配对阶段。这里只删除当前 peer，不影响 Wi-Fi 或桌子设置。
         */
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
        ESP_LOGW(TAG, "stale BLE bond removed; retry pairing");
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
    s_stack_synced = true;
    start_advertising();
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
    bool previous_valid = false;
    bool previous_config_valid = false;
    uint32_t last_notify_ms = 0;

    for (;;) {
        uint8_t state[DESK_BLE_STATE_LENGTH];
        current_state(state);
        uint8_t config[DESK_BLE_CONFIG_LENGTH];
        current_config(config);
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
        memcpy(previous, state, sizeof(previous));
        memcpy(previous_config, config, sizeof(previous_config));
        previous_valid = true;
        previous_config_valid = true;
        vTaskDelay(pdMS_TO_TICKS(DESK_BLE_STATE_POLL_MS));
    }
}

esp_err_t desk_ble_start(void)
{
    desk_ble_session_init(&s_session);
    update_session_aggregates();
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        return err;
    }
    ble_npl_callout_init(&s_hold_lease_callout,
                         nimble_port_get_dflt_eventq(),
                         hold_lease_event_cb, NULL);
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
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0; /* 无屏幕/键盘设备只能使用 Just Works。 */
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist |=
        BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist |=
        BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();

    /* 该入口不仅创建 host task，还会启用 ESP32 蓝牙控制器。 */
    nimble_port_freertos_init(nimble_host_task);
    if (xTaskCreate(state_notify_task, "ble_state", 3072, NULL, 4, NULL) !=
        pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "GATT ready lease=%d ms", CONFIG_DESK_BLE_HOLD_LEASE_MS);
    return ESP_OK;
}
