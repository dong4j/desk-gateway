/**
 * @file desk_wifi.h
 * @brief WiFi STA + SoftAP 配网 + NVS 凭证
 */
#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** SoftAP 已就绪或 STA 已拿到 IP 时回调（用于再启动 HTTP，避免监听过早） */
typedef void (*desk_wifi_ready_cb_t)(void);

void desk_wifi_set_ready_cb(desk_wifi_ready_cb_t cb);

esp_err_t desk_wifi_init(void);
esp_err_t desk_wifi_set_sta(const char *ssid, const char *pass);
bool desk_wifi_is_connected(void);
/** 当前是否处于 SoftAP 配网模式（无 STA 凭证时） */
bool desk_wifi_is_ap_active(void);
esp_err_t desk_wifi_get_ip(char *buf, size_t len);

#ifdef __cplusplus
}
#endif
