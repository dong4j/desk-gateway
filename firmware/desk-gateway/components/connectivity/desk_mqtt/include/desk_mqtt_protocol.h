/**
 * @file desk_mqtt_protocol.h
 * @brief MQTT v1 Topic / Payload 契约，可在宿主机测试。
 *
 * 本模块不依赖 Broker、ESP-MQTT 或 desk_core，避免把网络回调和运动裁决
 * 缠在一起。固件客户端只负责连接，协议解析与 JSON 构造必须走这里。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DESK_MQTT_PROTOCOL_VERSION 1
#define DESK_MQTT_DEVICE_ID_LENGTH 12
#define DESK_MQTT_DEVICE_ID_BUFFER (DESK_MQTT_DEVICE_ID_LENGTH + 1)
#define DESK_MQTT_CLIENT_ID_BUFFER 32
#define DESK_MQTT_TOPIC_BUFFER 96
#define DESK_MQTT_HOST_MAX 128
#define DESK_MQTT_USER_MAX 64
#define DESK_MQTT_PASSWORD_MAX 64
#define DESK_MQTT_PREFIX_MAX 32
#define DESK_MQTT_COMMAND_MAX 8
#define DESK_MQTT_PAYLOAD_ACCUM_MAX 32
#define DESK_MQTT_QUEUE_CAPACITY 4
#define DESK_MQTT_STATE_JSON_MAX 768
#define DESK_MQTT_RESULT_JSON_MAX 192
#define DESK_MQTT_DISCOVERY_JSON_MAX 2048
#define DESK_MQTT_DEFAULT_PORT 1883
#define DESK_MQTT_DEFAULT_TLS_PORT 8883
#define DESK_MQTT_DEFAULT_PREFIX "homeassistant"

typedef enum {
    DESK_MQTT_ACTION_NONE = 0,
    DESK_MQTT_ACTION_SIT,
    DESK_MQTT_ACTION_STAND,
    DESK_MQTT_ACTION_STOP,
} desk_mqtt_action_t;

typedef enum {
    DESK_MQTT_TLS_NONE = 0,
    DESK_MQTT_TLS_CERTIFICATE_BUNDLE,
} desk_mqtt_tls_mode_t;

typedef enum {
    DESK_MQTT_STATUS_IDLE = 0,
    DESK_MQTT_STATUS_MOVING_UP,
    DESK_MQTT_STATUS_MOVING_DOWN,
    DESK_MQTT_STATUS_GOTO_PRESET,
    DESK_MQTT_STATUS_ERROR,
} desk_mqtt_status_t;

typedef struct {
    bool client_enabled;
    char host[DESK_MQTT_HOST_MAX];
    uint16_t port;
    desk_mqtt_tls_mode_t tls_mode;
    char username[DESK_MQTT_USER_MAX];
    char password[DESK_MQTT_PASSWORD_MAX];
    char discovery_prefix[DESK_MQTT_PREFIX_MAX];
} desk_mqtt_config_t;

typedef struct {
    desk_mqtt_status_t status;
    bool height_known;
    int height_mm;
    bool have_previous_height;
    int previous_height_mm;
    int preset1_height_mm;
    int preset4_height_mm;
    int max_height_mm;
    bool child_lock;
    bool upward_blocked;
    bool mqtt_control_enabled;
    const char *driver;
    const char *firmware_version;
} desk_mqtt_state_input_t;

/** 用 STA MAC 生成 12 位小写十六进制设备 ID。 */
bool desk_mqtt_device_id_from_mac(const uint8_t mac[6], char *out,
                                  size_t out_len);

/** client_id = desk-gateway-<id> */
bool desk_mqtt_format_client_id(const char *device_id, char *out,
                                size_t out_len);

bool desk_mqtt_topic_availability(const char *device_id, char *out,
                                  size_t out_len);
bool desk_mqtt_topic_state(const char *device_id, char *out, size_t out_len);
bool desk_mqtt_topic_command(const char *device_id, char *out, size_t out_len);
bool desk_mqtt_topic_result(const char *device_id, char *out, size_t out_len);
bool desk_mqtt_topic_discovery(const char *prefix, const char *device_id,
                               char *out, size_t out_len);

bool desk_mqtt_is_command_topic(const char *device_id, const char *topic,
                                size_t topic_len);
bool desk_mqtt_is_ha_status_topic(const char *topic, size_t topic_len);

/**
 * 解析 command Payload。retain 为真、分片未完成、空白或大小写变体一律拒绝。
 * 完整分片由调用方拼好后再传入；本函数不处理 offset。
 */
bool desk_mqtt_parse_command(const char *payload, size_t len, bool retained,
                             desk_mqtt_action_t *out_action);

const char *desk_mqtt_action_name(desk_mqtt_action_t action);

/**
 * 请坐=0、起立=100。高度未知或档位非法时返回 -1，调用方必须写成 JSON null。
 */
int desk_mqtt_cover_position(bool height_known, int height_mm,
                             int preset1_height_mm, int preset4_height_mm);

const char *desk_mqtt_status_name(desk_mqtt_status_t status);

/**
 * opening / closing / stopped。goto_preset 没有连续可信高度时不得猜测方向。
 */
const char *desk_mqtt_cover_state(desk_mqtt_status_t status, bool height_known,
                                  int height_mm, bool have_previous_height,
                                  int previous_height_mm);

bool desk_mqtt_tls_mode_from_name(const char *name,
                                  desk_mqtt_tls_mode_t *out_mode);
const char *desk_mqtt_tls_mode_name(desk_mqtt_tls_mode_t mode);

/** 校验 Broker 配置；失败时 out_reason 指向静态字符串。 */
bool desk_mqtt_config_valid(const desk_mqtt_config_t *config,
                            const char **out_reason);

void desk_mqtt_config_init_defaults(desk_mqtt_config_t *config);

size_t desk_mqtt_format_state(const desk_mqtt_state_input_t *input,
                              char *out, size_t out_len);

size_t desk_mqtt_format_result(uint32_t sequence, desk_mqtt_action_t action,
                               bool ok, const char *error, const char *reason,
                               char *out, size_t out_len);

size_t desk_mqtt_format_discovery(const char *device_id, const char *prefix,
                                  const char *firmware_version, char *out,
                                  size_t out_len);

/**
 * 有界命令队列：STOP 清掉待执行的 SIT/STAND，避免停车后又被旧目标动作拉走。
 * 队列满时不得覆盖旧命令；STOP 仍可进入独立高优先级槽。
 */
typedef struct {
    desk_mqtt_action_t items[DESK_MQTT_QUEUE_CAPACITY];
    size_t count;
    bool stop_pending;
} desk_mqtt_cmd_queue_t;

typedef enum {
    DESK_MQTT_QUEUE_OK = 0,
    DESK_MQTT_QUEUE_FULL,
    DESK_MQTT_QUEUE_INVALID,
} desk_mqtt_queue_result_t;

void desk_mqtt_cmd_queue_init(desk_mqtt_cmd_queue_t *queue);
desk_mqtt_queue_result_t desk_mqtt_cmd_queue_push(desk_mqtt_cmd_queue_t *queue,
                                                  desk_mqtt_action_t action);
bool desk_mqtt_cmd_queue_pop(desk_mqtt_cmd_queue_t *queue,
                             desk_mqtt_action_t *out_action);

/** 分片重组：total 超限或出现空洞时记 overflow，调用方必须拒绝执行。 */
typedef struct {
    char buf[DESK_MQTT_PAYLOAD_ACCUM_MAX];
    size_t filled;
    size_t expected;
    bool overflow;
    bool active;
} desk_mqtt_payload_accum_t;

void desk_mqtt_payload_accum_reset(desk_mqtt_payload_accum_t *accum);
bool desk_mqtt_payload_accum_feed(desk_mqtt_payload_accum_t *accum,
                                  size_t offset, size_t total,
                                  const void *data, size_t len);
bool desk_mqtt_payload_accum_ready(const desk_mqtt_payload_accum_t *accum);

#ifdef __cplusplus
}
#endif
