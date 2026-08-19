/**
 * @file desk_mqtt.c
 * @brief 局域网 MQTT Client：LWT、有界命令队列、HA Discovery 与状态节流
 *
 * 约束：
 * - 只调 desk_core，不碰 Driver。
 * - MQTT event handler 不得 destroy/stop client，也不得调用 desk_core。
 * - retained command 必须拒绝；clean session，断线不补执行。
 * - 密码不得写入日志或状态 JSON。
 */
#include "desk_mqtt.h"

#include "desk_core.h"
#include "desk_wifi.h"

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "mqtt_client.h"
#include "nvs.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "desk_mqtt";
static const char *NVS_NS = "desk_mqtt";

static SemaphoreHandle_t s_lock;
static TaskHandle_t s_task;
static esp_mqtt_client_handle_t s_client;
static desk_mqtt_config_t s_cfg;
static desk_mqtt_cmd_queue_t s_queue;
static desk_mqtt_payload_accum_t s_accum;
static char s_accum_topic[DESK_MQTT_TOPIC_BUFFER];
static size_t s_accum_topic_len;
static bool s_accum_retain;

static char s_device_id[DESK_MQTT_DEVICE_ID_BUFFER];
static char s_client_id[DESK_MQTT_CLIENT_ID_BUFFER];
static char s_topic_availability[DESK_MQTT_TOPIC_BUFFER];
static char s_topic_state[DESK_MQTT_TOPIC_BUFFER];
static char s_topic_command[DESK_MQTT_TOPIC_BUFFER];
static char s_topic_result[DESK_MQTT_TOPIC_BUFFER];

static bool s_connected;
static bool s_restart_requested;
static bool s_need_announce;
static uint32_t s_birth_at_ms;
static uint32_t s_sequence;
static char s_last_error[32];

static char s_json_state[DESK_MQTT_STATE_JSON_MAX];
static char s_json_result[DESK_MQTT_RESULT_JSON_MAX];
static char s_json_reject[DESK_MQTT_RESULT_JSON_MAX];
static char s_json_discovery[DESK_MQTT_DISCOVERY_JSON_MAX];

static bool s_have_prev_height;
static int s_prev_height_mm;
static desk_core_snapshot_t s_last_snap;
static uint32_t s_last_state_ms;
static bool s_have_last_snap;

static void set_last_error_locked(const char *reason)
{
    strncpy(s_last_error, reason ? reason : "", sizeof(s_last_error) - 1);
    s_last_error[sizeof(s_last_error) - 1] = '\0';
}

static void copy_config(desk_mqtt_config_t *dst, const desk_mqtt_config_t *src)
{
    memcpy(dst, src, sizeof(*dst));
}

static void load_nvs(void)
{
    desk_mqtt_config_init_defaults(&s_cfg);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    uint8_t enabled = 0;
    if (nvs_get_u8(h, "en", &enabled) == ESP_OK) {
        s_cfg.client_enabled = enabled != 0;
    }
    size_t len = sizeof(s_cfg.host);
    (void)nvs_get_str(h, "host", s_cfg.host, &len);
    uint16_t port = 0;
    if (nvs_get_u16(h, "port", &port) == ESP_OK && port != 0) {
        s_cfg.port = port;
    }
    uint8_t tls = 0;
    if (nvs_get_u8(h, "tls", &tls) == ESP_OK) {
        s_cfg.tls_mode = (tls == (uint8_t)DESK_MQTT_TLS_CERTIFICATE_BUNDLE)
                             ? DESK_MQTT_TLS_CERTIFICATE_BUNDLE
                             : DESK_MQTT_TLS_NONE;
    }
    len = sizeof(s_cfg.username);
    (void)nvs_get_str(h, "user", s_cfg.username, &len);
    len = sizeof(s_cfg.password);
    (void)nvs_get_str(h, "pass", s_cfg.password, &len);
    len = sizeof(s_cfg.discovery_prefix);
    if (nvs_get_str(h, "prefix", s_cfg.discovery_prefix, &len) != ESP_OK ||
        s_cfg.discovery_prefix[0] == '\0') {
        strncpy(s_cfg.discovery_prefix, DESK_MQTT_DEFAULT_PREFIX,
                sizeof(s_cfg.discovery_prefix) - 1);
    }
    nvs_close(h);
}

static esp_err_t save_nvs(const desk_mqtt_config_t *cfg)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(h, "en", cfg->client_enabled ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_set_str(h, "host", cfg->host);
    }
    if (err == ESP_OK) {
        err = nvs_set_u16(h, "port", cfg->port);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(h, "tls", (uint8_t)cfg->tls_mode);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(h, "user", cfg->username);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(h, "pass", cfg->password);
    }
    if (err == ESP_OK) {
        err = nvs_set_str(h, "prefix", cfg->discovery_prefix);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

static bool refresh_identity(void)
{
    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        return false;
    }
    if (!desk_mqtt_device_id_from_mac(mac, s_device_id, sizeof(s_device_id)) ||
        !desk_mqtt_format_client_id(s_device_id, s_client_id,
                                    sizeof(s_client_id)) ||
        !desk_mqtt_topic_availability(s_device_id, s_topic_availability,
                                      sizeof(s_topic_availability)) ||
        !desk_mqtt_topic_state(s_device_id, s_topic_state,
                               sizeof(s_topic_state)) ||
        !desk_mqtt_topic_command(s_device_id, s_topic_command,
                                 sizeof(s_topic_command)) ||
        !desk_mqtt_topic_result(s_device_id, s_topic_result,
                                sizeof(s_topic_result))) {
        return false;
    }
    return true;
}

static desk_mqtt_status_t map_status(desk_status_t status)
{
    switch (status) {
    case DESK_STATUS_MOVING_UP:
        return DESK_MQTT_STATUS_MOVING_UP;
    case DESK_STATUS_MOVING_DOWN:
        return DESK_MQTT_STATUS_MOVING_DOWN;
    case DESK_STATUS_GOTO_PRESET:
        return DESK_MQTT_STATUS_GOTO_PRESET;
    case DESK_STATUS_ERROR:
        return DESK_MQTT_STATUS_ERROR;
    case DESK_STATUS_IDLE:
    default:
        return DESK_MQTT_STATUS_IDLE;
    }
}

static const char *firmware_version(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    return app && app->version[0] ? app->version : "";
}

static bool fill_state_input(desk_mqtt_state_input_t *input,
                             const desk_core_snapshot_t *snap,
                             bool mqtt_control_enabled)
{
    if (!input || !snap) {
        return false;
    }
    memset(input, 0, sizeof(*input));
    input->status = map_status(snap->status);
    input->height_known = snap->height_known;
    input->height_mm = snap->height_mm;
    input->have_previous_height = s_have_prev_height;
    input->previous_height_mm = s_prev_height_mm;
    input->preset1_height_mm = snap->preset1_height_mm;
    input->preset4_height_mm = snap->preset4_height_mm;
    input->max_height_mm = snap->max_height_mm;
    input->child_lock = snap->child_lock;
    input->upward_blocked = snap->upward_blocked;
    input->mqtt_control_enabled = mqtt_control_enabled;
    input->driver = snap->driver;
    input->firmware_version = firmware_version();
    return true;
}

static void remember_height(const desk_core_snapshot_t *snap)
{
    if (snap->height_known) {
        s_have_prev_height = true;
        s_prev_height_mm = snap->height_mm;
    }
}

static bool snapshot_changed(const desk_core_snapshot_t *snap)
{
    if (!s_have_last_snap) {
        return true;
    }
    return snap->status != s_last_snap.status ||
           snap->child_lock != s_last_snap.child_lock ||
           snap->upward_blocked != s_last_snap.upward_blocked ||
           snap->enabled_sources != s_last_snap.enabled_sources ||
           snap->preset1_height_mm != s_last_snap.preset1_height_mm ||
           snap->preset4_height_mm != s_last_snap.preset4_height_mm ||
           snap->max_height_mm != s_last_snap.max_height_mm ||
           snap->height_known != s_last_snap.height_known;
}

static bool should_publish_state(const desk_core_snapshot_t *snap, uint32_t now_ms)
{
    if (snapshot_changed(snap)) {
        return true;
    }
    bool moving = snap->status == DESK_STATUS_MOVING_UP ||
                  snap->status == DESK_STATUS_MOVING_DOWN ||
                  snap->status == DESK_STATUS_GOTO_PRESET;
    if (moving) {
        int delta = 0;
        if (snap->height_known && s_last_snap.height_known) {
            delta = snap->height_mm - s_last_snap.height_mm;
            if (delta < 0) {
                delta = -delta;
            }
        }
        if (delta >= 5 || (now_ms - s_last_state_ms) >= 500) {
            return true;
        }
        return false;
    }
    return (now_ms - s_last_state_ms) >= 30000;
}

static void publish_text(esp_mqtt_client_handle_t client, const char *topic,
                         const char *payload, int qos, int retain)
{
    if (!client || !topic || !payload) {
        return;
    }
    (void)esp_mqtt_client_publish(client, topic, payload, 0, qos, retain);
}

static void publish_state_locked(esp_mqtt_client_handle_t client,
                                 const desk_core_snapshot_t *snap,
                                 bool mqtt_control_enabled)
{
    desk_mqtt_state_input_t input;
    if (!client || !fill_state_input(&input, snap, mqtt_control_enabled)) {
        return;
    }
    size_t n = desk_mqtt_format_state(&input, s_json_state, sizeof(s_json_state));
    if (n == 0) {
        return;
    }
    publish_text(client, s_topic_state, s_json_state, 1, 1);
    s_last_snap = *snap;
    s_have_last_snap = true;
    s_last_state_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    remember_height(snap);
}

static void publish_discovery_locked(esp_mqtt_client_handle_t client)
{
    if (!client) {
        return;
    }
    /* 必须发单实体 Topic。device/.../config 在未订阅 Device Discovery 的 HA
     * 上只会进监听页，不会出现在 MQTT 设备列表。 */
    static const desk_mqtt_discovery_kind_t kinds[] = {
        DESK_MQTT_DISCOVERY_COVER,
        DESK_MQTT_DISCOVERY_HEIGHT,
        DESK_MQTT_DISCOVERY_CHILD_LOCK,
    };
    char topic[DESK_MQTT_TOPIC_BUFFER];
    for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        if (!desk_mqtt_topic_component_discovery(s_cfg.discovery_prefix, kinds[i],
                                                 s_device_id, topic,
                                                 sizeof(topic))) {
            ESP_LOGE(TAG, "discovery topic failed kind=%d", (int)kinds[i]);
            continue;
        }
        size_t n = desk_mqtt_format_component_discovery(
            kinds[i], s_device_id, firmware_version(), s_json_discovery,
            sizeof(s_json_discovery));
        if (n == 0) {
            ESP_LOGE(TAG, "discovery payload failed kind=%d", (int)kinds[i]);
            continue;
        }
        publish_text(client, topic, s_json_discovery, 1, 0);
        ESP_LOGI(TAG, "discovery published %s (%u bytes)", topic, (unsigned)n);
    }
}

static void publish_result_buf(esp_mqtt_client_handle_t client, char *buf,
                               size_t buf_len, desk_mqtt_action_t action,
                               bool ok, const char *error, const char *reason)
{
    uint32_t seq = ++s_sequence;
    size_t n = desk_mqtt_format_result(seq, action, ok, error, reason, buf,
                                       buf_len);
    if (n == 0) {
        return;
    }
    publish_text(client, s_topic_result, buf, 1, 0);
}

static const char *reason_for_core_err(esp_err_t err,
                                       const desk_core_snapshot_t *snap)
{
    if (err == ESP_ERR_NOT_ALLOWED) {
        return snap && snap->child_lock ? "child_lock" : "source_disabled";
    }
    if (err == ESP_ERR_INVALID_STATE && snap && snap->upward_blocked) {
        return "upward_blocked";
    }
    return NULL;
}

static void execute_action(desk_mqtt_action_t action)
{
    esp_err_t err = ESP_FAIL;
    if (action == DESK_MQTT_ACTION_STOP) {
        err = desk_core_stop();
    } else if (action == DESK_MQTT_ACTION_SIT) {
        err = desk_core_goto_preset(DESK_CONTROL_SOURCE_MQTT, 1);
    } else if (action == DESK_MQTT_ACTION_STAND) {
        err = desk_core_goto_preset(DESK_CONTROL_SOURCE_MQTT, 4);
    } else {
        return;
    }
    /* desk_core 必须在 MQTT 锁之外完成，避免与 Web/REST 反向加锁。 */
    desk_core_snapshot_t snap = desk_core_snapshot();
    bool mqtt_control =
        desk_core_get_source_enabled(DESK_CONTROL_SOURCE_MQTT);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    publish_result_buf(s_client, s_json_result, sizeof(s_json_result), action,
                       err == ESP_OK, esp_err_to_name(err),
                       reason_for_core_err(err, &snap));
    if (s_client && s_connected) {
        publish_state_locked(s_client, &snap, mqtt_control);
    }
    xSemaphoreGive(s_lock);
}

static bool should_connect(const desk_mqtt_config_t *cfg)
{
    const char *reason = NULL;
    return cfg->client_enabled && desk_wifi_is_connected() &&
           !desk_wifi_is_ap_active() && desk_mqtt_config_valid(cfg, &reason);
}

static void teardown_client(void)
{
    esp_mqtt_client_handle_t client;
    bool was_connected;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    client = s_client;
    was_connected = s_connected;
    s_client = NULL;
    s_connected = false;
    s_need_announce = false;
    s_birth_at_ms = 0;
    xSemaphoreGive(s_lock);
    if (!client) {
        return;
    }
    if (was_connected) {
        publish_text(client, s_topic_availability, "offline", 1, 1);
    }
    /* stop/destroy 必须在 event handler 之外；会等待 MQTT 任务退出。 */
    (void)esp_mqtt_client_stop(client);
    (void)esp_mqtt_client_destroy(client);
}

static void mqtt_event_handler(void *args, esp_event_base_t base, int32_t id,
                               void *data);

static esp_err_t setup_client(void)
{
    if (!refresh_identity()) {
        set_last_error_locked("identity");
        return ESP_ERR_INVALID_STATE;
    }
    const char *reason = NULL;
    if (!desk_mqtt_config_valid(&s_cfg, &reason)) {
        set_last_error_locked(reason ? reason : "invalid_config");
        return ESP_ERR_INVALID_ARG;
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.hostname = s_cfg.host,
        .broker.address.port = s_cfg.port,
        .broker.address.transport = s_cfg.tls_mode == DESK_MQTT_TLS_CERTIFICATE_BUNDLE
                                        ? MQTT_TRANSPORT_OVER_SSL
                                        : MQTT_TRANSPORT_OVER_TCP,
        .credentials.client_id = s_client_id,
        .session.protocol_ver = MQTT_PROTOCOL_V_3_1_1,
        .session.disable_clean_session = false,
        .session.last_will.topic = s_topic_availability,
        .session.last_will.msg = "offline",
        .session.last_will.qos = 1,
        .session.last_will.retain = true,
        .network.disable_auto_reconnect = false,
        .buffer.size = 2048,
        .buffer.out_size = 2048,
    };
    if (s_cfg.username[0]) {
        mqtt_cfg.credentials.username = s_cfg.username;
        mqtt_cfg.credentials.authentication.password = s_cfg.password;
    }
    if (s_cfg.tls_mode == DESK_MQTT_TLS_CERTIFICATE_BUNDLE) {
        /* 禁止 skip_cert_common_name_check；公开 CA 必须校验 Broker 身份。 */
        mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
        mqtt_cfg.broker.verification.skip_cert_common_name_check = false;
    }

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    if (!client) {
        set_last_error_locked("init_failed");
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID,
                                                   mqtt_event_handler, NULL);
    if (err != ESP_OK) {
        (void)esp_mqtt_client_destroy(client);
        set_last_error_locked("register_event");
        return err;
    }
    err = esp_mqtt_client_start(client);
    if (err != ESP_OK) {
        (void)esp_mqtt_client_destroy(client);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        set_last_error_locked(esp_err_to_name(err));
        xSemaphoreGive(s_lock);
        return err;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_client = client;
    desk_mqtt_payload_accum_reset(&s_accum);
    s_accum_topic[0] = '\0';
    s_accum_topic_len = 0;
    set_last_error_locked("");
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "client start id=%s host=%s port=%u tls=%s", s_device_id,
             s_cfg.host, (unsigned)s_cfg.port,
             desk_mqtt_tls_mode_name(s_cfg.tls_mode));
    return ESP_OK;
}

static void handle_command_payload(esp_mqtt_client_handle_t client,
                                   const char *payload, size_t len,
                                   bool retained)
{
    desk_mqtt_action_t action = DESK_MQTT_ACTION_NONE;
    if (!desk_mqtt_parse_command(payload, len, retained, &action)) {
        const char *reason = retained ? "retained" : "invalid_payload";
        publish_result_buf(client, s_json_reject, sizeof(s_json_reject),
                           DESK_MQTT_ACTION_NONE, false, "ESP_ERR_INVALID_ARG",
                           reason);
        return;
    }
    desk_mqtt_queue_result_t queued = desk_mqtt_cmd_queue_push(&s_queue, action);
    if (queued == DESK_MQTT_QUEUE_FULL) {
        publish_result_buf(client, s_json_reject, sizeof(s_json_reject), action,
                           false, "ESP_ERR_NO_MEM", "queue_full");
    }
}

static void mqtt_event_handler(void *args, esp_event_base_t base, int32_t id,
                               void *data)
{
    (void)args;
    (void)base;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)data;
    if (!event) {
        return;
    }
    switch (id) {
    case MQTT_EVENT_CONNECTED:
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_connected = true;
        s_need_announce = true;
        set_last_error_locked("");
        xSemaphoreGive(s_lock);
        (void)esp_mqtt_client_subscribe(event->client, s_topic_command, 1);
        (void)esp_mqtt_client_subscribe(event->client, "homeassistant/status",
                                        0);
        ESP_LOGI(TAG, "connected");
        break;
    case MQTT_EVENT_DISCONNECTED:
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_connected = false;
        s_need_announce = false;
        set_last_error_locked("disconnected");
        xSemaphoreGive(s_lock);
        ESP_LOGW(TAG, "disconnected");
        break;
    case MQTT_EVENT_DATA: {
        if (event->current_data_offset == 0) {
            s_accum_topic_len = 0;
            s_accum_topic[0] = '\0';
            if (event->topic && event->topic_len > 0 &&
                (size_t)event->topic_len < sizeof(s_accum_topic)) {
                memcpy(s_accum_topic, event->topic, (size_t)event->topic_len);
                s_accum_topic[(size_t)event->topic_len] = '\0';
                s_accum_topic_len = (size_t)event->topic_len;
            }
            s_accum_retain = event->retain;
        }
        bool fed = desk_mqtt_payload_accum_feed(
            &s_accum, (size_t)event->current_data_offset,
            (size_t)event->total_data_len, event->data,
            (size_t)event->data_len);
        if (!desk_mqtt_payload_accum_ready(&s_accum)) {
            if (!fed || s_accum.overflow) {
                desk_mqtt_payload_accum_reset(&s_accum);
            }
            break;
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (desk_mqtt_is_command_topic(s_device_id, s_accum_topic,
                                       s_accum_topic_len)) {
            handle_command_payload(event->client, s_accum.buf, s_accum.filled,
                                   s_accum_retain);
        } else if (desk_mqtt_is_ha_status_topic(s_accum_topic,
                                                s_accum_topic_len) &&
                   s_accum.filled == 6 &&
                   memcmp(s_accum.buf, "online", 6) == 0) {
            uint32_t delay_ms = 100 + (esp_random() % 400);
            s_birth_at_ms =
                (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) + delay_ms;
        }
        xSemaphoreGive(s_lock);
        desk_mqtt_payload_accum_reset(&s_accum);
        break;
    }
    case MQTT_EVENT_ERROR:
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (event->error_handle &&
            event->error_handle->error_type ==
                MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            set_last_error_locked("connection_refused");
        } else {
            set_last_error_locked("transport_error");
        }
        xSemaphoreGive(s_lock);
        break;
    default:
        break;
    }
}

static void mqtt_task(void *arg)
{
    (void)arg;
    for (;;) {
        bool restart = false;
        desk_mqtt_config_t cfg;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        restart = s_restart_requested;
        s_restart_requested = false;
        copy_config(&cfg, &s_cfg);
        xSemaphoreGive(s_lock);

        if (restart) {
            teardown_client();
        }

        bool want = should_connect(&cfg);
        if (want && !s_client) {
            /* start 可能立即回调 CONNECTED，绝不能在持有 s_lock 时调用。 */
            esp_err_t err = setup_client();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "setup skipped: %s", s_last_error);
            }
        } else if (!want && s_client) {
            teardown_client();
        }

        desk_mqtt_action_t action = DESK_MQTT_ACTION_NONE;
        while (xSemaphoreTake(s_lock, portMAX_DELAY) == pdTRUE) {
            bool have = desk_mqtt_cmd_queue_pop(&s_queue, &action);
            xSemaphoreGive(s_lock);
            if (!have) {
                break;
            }
            execute_action(action);
        }

        uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        desk_core_snapshot_t snap = desk_core_snapshot();
        bool mqtt_control =
            desk_core_get_source_enabled(DESK_CONTROL_SOURCE_MQTT);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        esp_mqtt_client_handle_t client = s_client;
        bool connected = s_connected;
        if (client && connected) {
            if (s_need_announce) {
                publish_text(client, s_topic_availability, "online", 1, 1);
                publish_discovery_locked(client);
                publish_state_locked(client, &snap, mqtt_control);
                s_need_announce = false;
            }
            if (s_birth_at_ms != 0 && now_ms >= s_birth_at_ms) {
                s_birth_at_ms = 0;
                publish_discovery_locked(client);
                publish_state_locked(client, &snap, mqtt_control);
            } else if (should_publish_state(&snap, now_ms)) {
                publish_state_locked(client, &snap, mqtt_control);
            }
        }
        xSemaphoreGive(s_lock);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t desk_mqtt_start(void)
{
    if (s_task) {
        return ESP_OK;
    }
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    load_nvs();
    desk_mqtt_cmd_queue_init(&s_queue);
    (void)refresh_identity();
    xSemaphoreGive(s_lock);
    BaseType_t ok = xTaskCreate(mqtt_task, "desk_mqtt", 6144, NULL, 5, &s_task);
    if (ok != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "worker started");
    return ESP_OK;
}

esp_err_t desk_mqtt_get_config(desk_mqtt_config_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_lock) {
        desk_mqtt_config_init_defaults(out);
        return ESP_OK;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    copy_config(out, &s_cfg);
    xSemaphoreGive(s_lock);
    memset(out->password, 0, sizeof(out->password));
    return ESP_OK;
}

bool desk_mqtt_password_configured(void)
{
    if (!s_lock) {
        return false;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    bool configured = s_cfg.password[0] != '\0';
    xSemaphoreGive(s_lock);
    return configured;
}

esp_err_t desk_mqtt_set_config(const desk_mqtt_config_t *config,
                               bool password_present)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    desk_mqtt_config_t next;
    copy_config(&next, config);
    if (!s_lock) {
        load_nvs();
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!password_present) {
        memcpy(next.password, s_cfg.password, sizeof(next.password));
    }
    const char *reason = NULL;
    if (next.client_enabled && !desk_mqtt_config_valid(&next, &reason)) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_ARG;
    }
    if (!next.client_enabled) {
        /* 关闭 Client 时仍校验 prefix/port，避免下次启用时读到损坏配置。 */
        if (next.port == 0 || next.discovery_prefix[0] == '\0') {
            xSemaphoreGive(s_lock);
            return ESP_ERR_INVALID_ARG;
        }
    }
    esp_err_t err = save_nvs(&next);
    if (err == ESP_OK) {
        copy_config(&s_cfg, &next);
        s_restart_requested = true;
        if (!refresh_identity()) {
            err = ESP_ERR_INVALID_STATE;
        }
    }
    xSemaphoreGive(s_lock);
    return err;
}

void desk_mqtt_get_runtime(desk_mqtt_runtime_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->control_enabled =
        desk_core_get_source_enabled(DESK_CONTROL_SOURCE_MQTT);
    out->sta_ready = desk_wifi_is_connected() && !desk_wifi_is_ap_active();
    if (!s_lock) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    out->client_enabled = s_cfg.client_enabled;
    out->connected = s_connected;
    out->password_configured = s_cfg.password[0] != '\0';
    memcpy(out->device_id, s_device_id, sizeof(out->device_id));
    memcpy(out->last_error, s_last_error, sizeof(out->last_error));
    xSemaphoreGive(s_lock);
}
