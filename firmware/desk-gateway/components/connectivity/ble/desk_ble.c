/**
 * @file desk_ble.c
 * @brief Desk Gateway 原生 NimBLE GATT Server。
 *
 * BLE 只负责入口协议和连接生命周期，所有运动仍进入 desk_core，从而统一
 * 执行童锁、Bluetooth 来源权限、最高安全高度和运动超时策略。
 */
#include "desk_ble.h"

#include "desk_ble_protocol.h"
#include "desk_core.h"

#include "esp_log.h"
#include "esp_timer.h"
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
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"

#include <string.h>

/* NimBLE store/config currently exports this initializer without a public declaration. */
void ble_store_config_init(void);

#define DESK_BLE_DEVICE_NAME "DeskGateway"
#define DESK_BLE_STATE_POLL_MS 200
#define DESK_BLE_STATE_HEARTBEAT_MS 1000

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

static const char *TAG = "desk_ble";
static uint8_t s_own_addr_type;
static uint16_t s_state_value_handle;
static esp_timer_handle_t s_hold_lease_timer;
static portMUX_TYPE s_motion_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_motion_owned;
static volatile bool s_stack_synced;
static volatile bool s_connected;
static volatile bool s_state_subscribed;

static void start_advertising(void);

static bool take_motion_ownership(bool owned)
{
    portENTER_CRITICAL(&s_motion_lock);
    bool previous = s_motion_owned;
    s_motion_owned = owned;
    portEXIT_CRITICAL(&s_motion_lock);
    return previous;
}

static void cancel_hold_lease(void)
{
    if (!s_hold_lease_timer) {
        return;
    }
    esp_err_t err = esp_timer_stop(s_hold_lease_timer);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "stop BLE hold lease: %s", esp_err_to_name(err));
    }
}

static void stop_owned_motion(const char *reason)
{
    cancel_hold_lease();
    if (take_motion_ownership(false)) {
        ESP_LOGW(TAG, "%s -> stop", reason);
        (void)desk_core_stop();
    }
}

static void hold_lease_timer_cb(void *arg)
{
    (void)arg;
    stop_owned_motion("BLE hold lease expired");
}

static esp_err_t arm_hold_lease(void)
{
    cancel_hold_lease();
    esp_err_t err = esp_timer_start_once(
        s_hold_lease_timer,
        (uint64_t)CONFIG_DESK_BLE_HOLD_LEASE_MS * UINT64_C(1000));
    if (err != ESP_OK) {
        take_motion_ownership(false);
        (void)desk_core_stop();
    }
    return err;
}

static esp_err_t execute_command(desk_ble_command_t command)
{
    esp_err_t err;
    switch (command) {
    case DESK_BLE_COMMAND_STOP:
        cancel_hold_lease();
        take_motion_ownership(false);
        return desk_core_stop();

    case DESK_BLE_COMMAND_HOLD_UP:
        err = desk_core_hold_up(DESK_CONTROL_SOURCE_BLUETOOTH);
        if (err == ESP_OK) {
            take_motion_ownership(true);
            err = arm_hold_lease();
        }
        return err;

    case DESK_BLE_COMMAND_HOLD_DOWN:
        err = desk_core_hold_down(DESK_CONTROL_SOURCE_BLUETOOTH);
        if (err == ESP_OK) {
            take_motion_ownership(true);
            err = arm_hold_lease();
        }
        return err;

    case DESK_BLE_COMMAND_PRESET_1:
    case DESK_BLE_COMMAND_PRESET_4:
        cancel_hold_lease();
        err = desk_core_goto_preset(
            DESK_CONTROL_SOURCE_BLUETOOTH,
            command == DESK_BLE_COMMAND_PRESET_1 ? 1 : 4);
        if (err == ESP_OK) {
            /* 档位由驱动闭环停止；连接断开前仍由 BLE 会话拥有本次运动。 */
            take_motion_ownership(true);
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
    (void)conn_handle;
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
    esp_err_t err = execute_command(command);
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
    {0},
};

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics = s_characteristics,
    },
    {0},
};

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_connected = true;
            ESP_LOGI(TAG, "client connected handle=%u",
                     event->connect.conn_handle);
        } else {
            ESP_LOGW(TAG, "connect failed status=%d", event->connect.status);
            start_advertising();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        s_connected = false;
        s_state_subscribed = false;
        stop_owned_motion("BLE disconnected");
        ESP_LOGI(TAG, "client disconnected reason=%d",
                 event->disconnect.reason);
        start_advertising();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_state_value_handle) {
            s_state_subscribed = event->subscribe.cur_notify != 0;
            ESP_LOGI(TAG, "state notify -> %d", (int)s_state_subscribed);
            if (s_state_subscribed) {
                ble_gatts_chr_updated(s_state_value_handle);
            }
        }
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGI(TAG, "encryption changed status=%d",
                 event->enc_change.status);
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        return 0;

    default:
        return 0;
    }
}

static void start_advertising(void)
{
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
    bool previous_valid = false;
    uint32_t last_notify_ms = 0;

    for (;;) {
        uint8_t state[DESK_BLE_STATE_LENGTH];
        current_state(state);
        if (state[1] == DESK_STATUS_IDLE) {
            /* 清掉已闭环结束的档位所有权，避免之后断连误停其他入口。 */
            take_motion_ownership(false);
        }

        uint32_t now = esp_log_timestamp();
        bool changed = !previous_valid ||
                       memcmp(previous, state, sizeof(state)) != 0;
        bool heartbeat = (uint32_t)(now - last_notify_ms) >=
                         DESK_BLE_STATE_HEARTBEAT_MS;
        if (s_stack_synced && s_connected && s_state_subscribed &&
            (changed || heartbeat)) {
            ble_gatts_chr_updated(s_state_value_handle);
            last_notify_ms = now;
        }
        memcpy(previous, state, sizeof(previous));
        previous_valid = true;
        vTaskDelay(pdMS_TO_TICKS(DESK_BLE_STATE_POLL_MS));
    }
}

esp_err_t desk_ble_start(void)
{
    const esp_timer_create_args_t timer_args = {
        .callback = hold_lease_timer_cb,
        .name = "desk_ble_hold",
    };
    esp_err_t err = esp_timer_create(&timer_args, &s_hold_lease_timer);
    if (err != ESP_OK) {
        return err;
    }

    err = nimble_port_init();
    if (err != ESP_OK) {
        return err;
    }

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
