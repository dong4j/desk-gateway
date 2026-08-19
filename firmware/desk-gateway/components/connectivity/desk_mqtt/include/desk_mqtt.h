/**
 * @file desk_mqtt.h
 * @brief MQTT Client 生命周期：NVS、Broker 连接、命令队列与诊断快照
 *
 * 本模块可以依赖 desk_core，不能依赖具体 Driver。Web 只读写配置和诊断，
 * 不转发 Topic。event handler 不得销毁 client，也不得调用 desk_core。
 */
#pragma once

#include "desk_mqtt_protocol.h"

#include "esp_err.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool client_enabled;
    bool connected;
    bool sta_ready;
    bool control_enabled;
    bool password_configured;
    char device_id[DESK_MQTT_DEVICE_ID_BUFFER];
    char last_error[32];
} desk_mqtt_runtime_t;

/**
 * 幂等启动 worker。SoftAP 下不连 Broker；STA 拿到 IP 且配置有效后才创建 client。
 */
esp_err_t desk_mqtt_start(void);

/** 拷贝当前配置；password 始终清空，避免 Web 回传明文。 */
esp_err_t desk_mqtt_get_config(desk_mqtt_config_t *out);

bool desk_mqtt_password_configured(void);

/**
 * 写入 NVS 并请求 worker 重建 client。
 * password_present 为假时保留已存密码；空字符串表示清除密码。
 */
esp_err_t desk_mqtt_set_config(const desk_mqtt_config_t *config,
                               bool password_present);

void desk_mqtt_get_runtime(desk_mqtt_runtime_t *out);

#ifdef __cplusplus
}
#endif
