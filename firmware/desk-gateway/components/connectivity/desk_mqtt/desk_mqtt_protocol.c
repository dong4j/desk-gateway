/**
 * @file desk_mqtt_protocol.c
 * @brief MQTT v1 契约实现：精确 Topic、固定命令、状态 JSON 与 HA Discovery
 */
#include "desk_mqtt_protocol.h"

#include <stdio.h>
#include <string.h>

static bool copy_bounded(char *out, size_t out_len, const char *value)
{
    if (!out || out_len == 0 || !value) {
        return false;
    }
    size_t n = strlen(value);
    if (n + 1 > out_len) {
        return false;
    }
    memcpy(out, value, n + 1);
    return true;
}

static bool is_lower_hex12(const char *device_id)
{
    if (!device_id || strlen(device_id) != DESK_MQTT_DEVICE_ID_LENGTH) {
        return false;
    }
    for (size_t i = 0; i < DESK_MQTT_DEVICE_ID_LENGTH; i++) {
        char c = device_id[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

static bool format_topic(char *out, size_t out_len, const char *fmt,
                         const char *device_id)
{
    if (!is_lower_hex12(device_id) || !out) {
        return false;
    }
    int n = snprintf(out, out_len, fmt, device_id);
    return n > 0 && (size_t)n < out_len;
}

static bool topic_equals(const char *topic, size_t topic_len, const char *expected)
{
    size_t expected_len = strlen(expected);
    return topic && expected_len == topic_len &&
           memcmp(topic, expected, topic_len) == 0;
}

static bool host_char_ok(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
}

static bool prefix_char_ok(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-';
}

static bool json_escape(char *out, size_t out_len, const char *in)
{
    if (!out || out_len < 3 || !in) {
        return false;
    }
    size_t w = 0;
    out[w++] = '"';
    for (size_t i = 0; in[i] != '\0'; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c < 0x20 || c == '"' || c == '\\') {
            return false;
        }
        if (w + 2 >= out_len) {
            return false;
        }
        out[w++] = (char)c;
    }
    if (w + 1 >= out_len) {
        return false;
    }
    out[w++] = '"';
    out[w] = '\0';
    return true;
}

bool desk_mqtt_device_id_from_mac(const uint8_t mac[6], char *out,
                                  size_t out_len)
{
    if (!mac || !out || out_len < DESK_MQTT_DEVICE_ID_BUFFER) {
        return false;
    }
    int n = snprintf(out, out_len, "%02x%02x%02x%02x%02x%02x", mac[0], mac[1],
                     mac[2], mac[3], mac[4], mac[5]);
    return n == DESK_MQTT_DEVICE_ID_LENGTH;
}

bool desk_mqtt_format_client_id(const char *device_id, char *out,
                                size_t out_len)
{
    return format_topic(out, out_len, "desk-gateway-%s", device_id);
}

bool desk_mqtt_topic_availability(const char *device_id, char *out,
                                  size_t out_len)
{
    return format_topic(out, out_len, "desk-gateway/%s/availability",
                        device_id);
}

bool desk_mqtt_topic_state(const char *device_id, char *out, size_t out_len)
{
    return format_topic(out, out_len, "desk-gateway/%s/state", device_id);
}

bool desk_mqtt_topic_command(const char *device_id, char *out, size_t out_len)
{
    return format_topic(out, out_len, "desk-gateway/%s/command", device_id);
}

bool desk_mqtt_topic_result(const char *device_id, char *out, size_t out_len)
{
    return format_topic(out, out_len, "desk-gateway/%s/result", device_id);
}

bool desk_mqtt_topic_discovery(const char *prefix, const char *device_id,
                               char *out, size_t out_len)
{
    if (!prefix || !prefix[0] || !is_lower_hex12(device_id) || !out) {
        return false;
    }
    int n = snprintf(out, out_len, "%s/device/%s/config", prefix, device_id);
    return n > 0 && (size_t)n < out_len;
}

bool desk_mqtt_is_command_topic(const char *device_id, const char *topic,
                                size_t topic_len)
{
    char expected[DESK_MQTT_TOPIC_BUFFER];
    if (!desk_mqtt_topic_command(device_id, expected, sizeof(expected))) {
        return false;
    }
    return topic_equals(topic, topic_len, expected);
}

bool desk_mqtt_is_ha_status_topic(const char *topic, size_t topic_len)
{
    return topic_equals(topic, topic_len, "homeassistant/status");
}

bool desk_mqtt_parse_command(const char *payload, size_t len, bool retained,
                             desk_mqtt_action_t *out_action)
{
    if (!payload || !out_action || retained || len == 0 ||
        len > DESK_MQTT_COMMAND_MAX) {
        return false;
    }
    if (len == 3 && memcmp(payload, "SIT", 3) == 0) {
        *out_action = DESK_MQTT_ACTION_SIT;
        return true;
    }
    if (len == 5 && memcmp(payload, "STAND", 5) == 0) {
        *out_action = DESK_MQTT_ACTION_STAND;
        return true;
    }
    if (len == 4 && memcmp(payload, "STOP", 4) == 0) {
        *out_action = DESK_MQTT_ACTION_STOP;
        return true;
    }
    return false;
}

const char *desk_mqtt_action_name(desk_mqtt_action_t action)
{
    switch (action) {
    case DESK_MQTT_ACTION_SIT:
        return "SIT";
    case DESK_MQTT_ACTION_STAND:
        return "STAND";
    case DESK_MQTT_ACTION_STOP:
        return "STOP";
    case DESK_MQTT_ACTION_NONE:
    default:
        return "NONE";
    }
}

int desk_mqtt_cover_position(bool height_known, int height_mm,
                             int preset1_height_mm, int preset4_height_mm)
{
    if (!height_known || preset4_height_mm <= preset1_height_mm) {
        return -1;
    }
    int span = preset4_height_mm - preset1_height_mm;
    int pos = (int)(((long)(height_mm - preset1_height_mm) * 100L +
                     span / 2) /
                    span);
    if (pos < 0) {
        return 0;
    }
    if (pos > 100) {
        return 100;
    }
    return pos;
}

const char *desk_mqtt_status_name(desk_mqtt_status_t status)
{
    switch (status) {
    case DESK_MQTT_STATUS_MOVING_UP:
        return "moving_up";
    case DESK_MQTT_STATUS_MOVING_DOWN:
        return "moving_down";
    case DESK_MQTT_STATUS_GOTO_PRESET:
        return "goto_preset";
    case DESK_MQTT_STATUS_ERROR:
        return "error";
    case DESK_MQTT_STATUS_IDLE:
    default:
        return "idle";
    }
}

const char *desk_mqtt_cover_state(desk_mqtt_status_t status, bool height_known,
                                  int height_mm, bool have_previous_height,
                                  int previous_height_mm)
{
    if (status == DESK_MQTT_STATUS_MOVING_UP) {
        return "opening";
    }
    if (status == DESK_MQTT_STATUS_MOVING_DOWN) {
        return "closing";
    }
    if (status == DESK_MQTT_STATUS_GOTO_PRESET) {
        if (!height_known || !have_previous_height) {
            return "stopped";
        }
        if (height_mm > previous_height_mm) {
            return "opening";
        }
        if (height_mm < previous_height_mm) {
            return "closing";
        }
    }
    return "stopped";
}

bool desk_mqtt_tls_mode_from_name(const char *name,
                                  desk_mqtt_tls_mode_t *out_mode)
{
    if (!name || !out_mode) {
        return false;
    }
    if (strcmp(name, "none") == 0) {
        *out_mode = DESK_MQTT_TLS_NONE;
        return true;
    }
    if (strcmp(name, "certificate_bundle") == 0) {
        *out_mode = DESK_MQTT_TLS_CERTIFICATE_BUNDLE;
        return true;
    }
    return false;
}

const char *desk_mqtt_tls_mode_name(desk_mqtt_tls_mode_t mode)
{
    return mode == DESK_MQTT_TLS_CERTIFICATE_BUNDLE ? "certificate_bundle"
                                                    : "none";
}

void desk_mqtt_config_init_defaults(desk_mqtt_config_t *config)
{
    if (!config) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->port = DESK_MQTT_DEFAULT_PORT;
    (void)copy_bounded(config->discovery_prefix,
                       sizeof(config->discovery_prefix),
                       DESK_MQTT_DEFAULT_PREFIX);
}

bool desk_mqtt_config_valid(const desk_mqtt_config_t *config,
                            const char **out_reason)
{
    static const char *bad_host = "invalid_host";
    static const char *bad_port = "invalid_port";
    static const char *bad_prefix = "invalid_discovery_prefix";
    if (out_reason) {
        *out_reason = "invalid_config";
    }
    if (!config) {
        return false;
    }
    size_t host_len = strlen(config->host);
    if (host_len == 0 || host_len >= DESK_MQTT_HOST_MAX) {
        if (out_reason) {
            *out_reason = bad_host;
        }
        return false;
    }
    if (strstr(config->host, "://") != NULL || strchr(config->host, '@') ||
        strchr(config->host, '/') || strchr(config->host, ' ') ||
        strchr(config->host, ':')) {
        if (out_reason) {
            *out_reason = bad_host;
        }
        return false;
    }
    for (size_t i = 0; i < host_len; i++) {
        if (!host_char_ok(config->host[i])) {
            if (out_reason) {
                *out_reason = bad_host;
            }
            return false;
        }
    }
    if (config->port == 0) {
        if (out_reason) {
            *out_reason = bad_port;
        }
        return false;
    }
    size_t prefix_len = strlen(config->discovery_prefix);
    if (prefix_len == 0 || prefix_len >= DESK_MQTT_PREFIX_MAX) {
        if (out_reason) {
            *out_reason = bad_prefix;
        }
        return false;
    }
    for (size_t i = 0; i < prefix_len; i++) {
        if (!prefix_char_ok(config->discovery_prefix[i])) {
            if (out_reason) {
                *out_reason = bad_prefix;
            }
            return false;
        }
    }
    if (strlen(config->username) >= DESK_MQTT_USER_MAX ||
        strlen(config->password) >= DESK_MQTT_PASSWORD_MAX) {
        if (out_reason) {
            *out_reason = "invalid_credentials";
        }
        return false;
    }
    if (out_reason) {
        *out_reason = NULL;
    }
    return true;
}

size_t desk_mqtt_format_state(const desk_mqtt_state_input_t *input, char *out,
                              size_t out_len)
{
    if (!input || !out || out_len < 32) {
        return 0;
    }
    int position = desk_mqtt_cover_position(input->height_known, input->height_mm,
                                            input->preset1_height_mm,
                                            input->preset4_height_mm);
    const char *cover = desk_mqtt_cover_state(
        input->status, input->height_known, input->height_mm,
        input->have_previous_height, input->previous_height_mm);
    char height_buf[24];
    char position_buf[16];
    if (input->height_known) {
        snprintf(height_buf, sizeof(height_buf), "%d", input->height_mm);
    } else {
        strncpy(height_buf, "null", sizeof(height_buf) - 1);
        height_buf[sizeof(height_buf) - 1] = '\0';
    }
    if (position < 0) {
        strncpy(position_buf, "null", sizeof(position_buf) - 1);
        position_buf[sizeof(position_buf) - 1] = '\0';
    } else {
        snprintf(position_buf, sizeof(position_buf), "%d", position);
    }
    char driver[48];
    char firmware[48];
    if (!json_escape(driver, sizeof(driver),
                     input->driver ? input->driver : "none") ||
        !json_escape(firmware, sizeof(firmware),
                     input->firmware_version ? input->firmware_version : "")) {
        return 0;
    }
    int n = snprintf(
        out, out_len,
        "{\"version\":%d,\"status\":\"%s\",\"cover_state\":\"%s\","
        "\"height_mm\":%s,\"height_known\":%s,\"position\":%s,"
        "\"child_lock\":%s,\"upward_blocked\":%s,\"max_height_mm\":%d,"
        "\"preset1_height_mm\":%d,\"preset4_height_mm\":%d,"
        "\"mqtt_control_enabled\":%s,\"driver\":%s,\"firmware_version\":%s}",
        DESK_MQTT_PROTOCOL_VERSION, desk_mqtt_status_name(input->status), cover,
        height_buf, input->height_known ? "true" : "false", position_buf,
        input->child_lock ? "true" : "false",
        input->upward_blocked ? "true" : "false", input->max_height_mm,
        input->preset1_height_mm, input->preset4_height_mm,
        input->mqtt_control_enabled ? "true" : "false", driver, firmware);
    if (n <= 0 || (size_t)n >= out_len) {
        return 0;
    }
    return (size_t)n;
}

size_t desk_mqtt_format_result(uint32_t sequence, desk_mqtt_action_t action,
                               bool ok, const char *error, const char *reason,
                               char *out, size_t out_len)
{
    if (!out || out_len < 32) {
        return 0;
    }
    char error_json[48];
    if (!json_escape(error_json, sizeof(error_json),
                     error ? error : "ESP_FAIL")) {
        return 0;
    }
    int n;
    if (reason && reason[0]) {
        char reason_json[48];
        if (!json_escape(reason_json, sizeof(reason_json), reason)) {
            return 0;
        }
        n = snprintf(out, out_len,
                     "{\"version\":%d,\"sequence\":%lu,\"action\":\"%s\","
                     "\"ok\":%s,\"error\":%s,\"reason\":%s}",
                     DESK_MQTT_PROTOCOL_VERSION, (unsigned long)sequence,
                     desk_mqtt_action_name(action), ok ? "true" : "false",
                     error_json, reason_json);
    } else {
        n = snprintf(out, out_len,
                     "{\"version\":%d,\"sequence\":%lu,\"action\":\"%s\","
                     "\"ok\":%s,\"error\":%s}",
                     DESK_MQTT_PROTOCOL_VERSION, (unsigned long)sequence,
                     desk_mqtt_action_name(action), ok ? "true" : "false",
                     error_json);
    }
    if (n <= 0 || (size_t)n >= out_len) {
        return 0;
    }
    return (size_t)n;
}

size_t desk_mqtt_format_discovery(const char *device_id, const char *prefix,
                                  const char *firmware_version, char *out,
                                  size_t out_len)
{
    char command[DESK_MQTT_TOPIC_BUFFER];
    char state[DESK_MQTT_TOPIC_BUFFER];
    char availability[DESK_MQTT_TOPIC_BUFFER];
    char sw[48];
    if (!desk_mqtt_topic_command(device_id, command, sizeof(command)) ||
        !desk_mqtt_topic_state(device_id, state, sizeof(state)) ||
        !desk_mqtt_topic_availability(device_id, availability,
                                      sizeof(availability)) ||
        !json_escape(sw, sizeof(sw),
                     firmware_version ? firmware_version : "")) {
        return 0;
    }
    const char *pfx =
        (prefix && prefix[0]) ? prefix : DESK_MQTT_DEFAULT_PREFIX;
    int n = snprintf(
        out, out_len,
        "{\"dev\":{\"ids\":[\"desk_gateway_%s\"],\"name\":\"Desk Gateway\","
        "\"mf\":\"Desk Gateway\",\"mdl\":\"ESP32-S3\",\"sw\":%s},"
        "\"o\":{\"name\":\"desk-gateway\",\"sw\":%s},"
        "\"cmps\":{"
        "\"cover\":{\"p\":\"cover\","
        "\"unique_id\":\"desk_gateway_%s_cover\",\"name\":\"Desk\","
        "\"command_topic\":\"%s\",\"payload_open\":\"STAND\","
        "\"payload_close\":\"SIT\",\"payload_stop\":\"STOP\","
        "\"state_topic\":\"%s\","
        "\"value_template\":\"{{ value_json.cover_state }}\","
        "\"position_topic\":\"%s\","
        "\"position_template\":\"{{ value_json.position }}\","
        "\"availability_topic\":\"%s\",\"payload_available\":\"online\","
        "\"payload_not_available\":\"offline\",\"optimistic\":false,\"qos\":1},"
        "\"height\":{\"p\":\"sensor\","
        "\"unique_id\":\"desk_gateway_%s_height\",\"name\":\"Height\","
        "\"state_topic\":\"%s\","
        "\"value_template\":\"{{ value_json.height_mm }}\","
        "\"unit_of_measurement\":\"mm\",\"device_class\":\"distance\"},"
        "\"child_lock\":{\"p\":\"binary_sensor\","
        "\"unique_id\":\"desk_gateway_%s_child_lock\",\"name\":\"Child lock\","
        "\"state_topic\":\"%s\","
        "\"value_template\":\"{{ value_json.child_lock }}\","
        "\"payload_on\":\"true\",\"payload_off\":\"false\"}"
        "}}",
        device_id, sw, sw, device_id, command, state, state, availability,
        device_id, state, device_id, state);
    (void)pfx;
    if (n <= 0 || (size_t)n >= out_len) {
        return 0;
    }
    return (size_t)n;
}

void desk_mqtt_cmd_queue_init(desk_mqtt_cmd_queue_t *queue)
{
    if (!queue) {
        return;
    }
    memset(queue, 0, sizeof(*queue));
}

desk_mqtt_queue_result_t desk_mqtt_cmd_queue_push(desk_mqtt_cmd_queue_t *queue,
                                                  desk_mqtt_action_t action)
{
    if (!queue || action == DESK_MQTT_ACTION_NONE) {
        return DESK_MQTT_QUEUE_INVALID;
    }
    if (action == DESK_MQTT_ACTION_STOP) {
        /* STOP 必须压过尚未执行的目标动作，否则 Cover 点停后仍会被旧 SIT 拉走。 */
        queue->count = 0;
        queue->stop_pending = true;
        return DESK_MQTT_QUEUE_OK;
    }
    if (queue->count >= DESK_MQTT_QUEUE_CAPACITY) {
        return DESK_MQTT_QUEUE_FULL;
    }
    queue->items[queue->count++] = action;
    return DESK_MQTT_QUEUE_OK;
}

bool desk_mqtt_cmd_queue_pop(desk_mqtt_cmd_queue_t *queue,
                             desk_mqtt_action_t *out_action)
{
    if (!queue || !out_action) {
        return false;
    }
    if (queue->stop_pending) {
        queue->stop_pending = false;
        *out_action = DESK_MQTT_ACTION_STOP;
        return true;
    }
    if (queue->count == 0) {
        return false;
    }
    *out_action = queue->items[0];
    memmove(&queue->items[0], &queue->items[1],
            (queue->count - 1) * sizeof(queue->items[0]));
    queue->count--;
    return true;
}

void desk_mqtt_payload_accum_reset(desk_mqtt_payload_accum_t *accum)
{
    if (!accum) {
        return;
    }
    memset(accum, 0, sizeof(*accum));
}

bool desk_mqtt_payload_accum_feed(desk_mqtt_payload_accum_t *accum,
                                  size_t offset, size_t total,
                                  const void *data, size_t len)
{
    if (!accum || !data) {
        return false;
    }
    if (offset == 0) {
        desk_mqtt_payload_accum_reset(accum);
        accum->expected = total;
        accum->active = true;
        if (total == 0 || total > DESK_MQTT_PAYLOAD_ACCUM_MAX) {
            accum->overflow = true;
            return false;
        }
    }
    if (!accum->active || accum->overflow) {
        return false;
    }
    if (offset != accum->filled || offset + len > accum->expected) {
        accum->overflow = true;
        return false;
    }
    memcpy(accum->buf + accum->filled, data, len);
    accum->filled += len;
    return true;
}

bool desk_mqtt_payload_accum_ready(const desk_mqtt_payload_accum_t *accum)
{
    return accum && accum->active && !accum->overflow && accum->expected > 0 &&
           accum->filled == accum->expected;
}
