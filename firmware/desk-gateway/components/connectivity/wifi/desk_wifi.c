/**
 * @file desk_wifi.c
 * @brief STA / SoftAP 配网；凭证存 NVS
 *
 * 无凭证时开 SoftAP（DeskGateway），手机连上后浏览器配网，不依赖串口输入。
 * HTTP 应在 SoftAP 就绪或 STA_GOT_IP 之后再启动（见 ready_cb）。
 */
#include "desk_wifi.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "desk_wifi";
static const char *NVS_NS = "desk_wifi";

/** SoftAP 固定参数：仅首次配网用 */
static const char *AP_SSID = "DeskGateway";
static const char *AP_PASS = "desk-gateway";

static bool s_connected;
static bool s_ap_active;
static esp_netif_t *s_sta_netif;
static esp_netif_t *s_ap_netif;
static desk_wifi_ready_cb_t s_ready_cb;

void desk_wifi_set_ready_cb(desk_wifi_ready_cb_t cb)
{
    s_ready_cb = cb;
}

static void notify_ready(void)
{
    if (s_ready_cb) {
        s_ready_cb();
    }
}

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (!s_ap_active) {
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (!s_ap_active) {
            ESP_LOGW(TAG, "disconnected, retry");
            esp_wifi_connect();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_connected = true;
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "got ip: " IPSTR, IP2STR(&e->ip_info.ip));
        /* STA 场景：拿到 IP 后再起 HTTP，避免 listen 过早导致局域网访问失败 */
        notify_ready();
    }
}

static esp_err_t load_creds(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_get_str(h, "ssid", ssid, &ssid_len);
    if (err == ESP_OK) {
        err = nvs_get_str(h, "pass", pass, &pass_len);
    }
    nvs_close(h);
    return err;
}

static esp_err_t start_softap(void)
{
    s_ap_netif = esp_netif_create_default_wifi_ap();
    wifi_config_t ap = {0};
    strncpy((char *)ap.ap.ssid, AP_SSID, sizeof(ap.ap.ssid) - 1);
    strncpy((char *)ap.ap.password, AP_PASS, sizeof(ap.ap.password) - 1);
    ap.ap.ssid_len = strlen(AP_SSID);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* 配网期关闭省电，避免手机连热点后 HTTP 偶发不通 */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    s_ap_active = true;
    ESP_LOGW(TAG, "SoftAP \"%s\" pass=\"%s\" → http://192.168.4.1/", AP_SSID, AP_PASS);
    notify_ready();
    return ESP_OK;
}

esp_err_t desk_wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi, NULL));

    char ssid[33] = {0};
    char pass[65] = {0};
    if (load_creds(ssid, sizeof(ssid), pass, sizeof(pass)) == ESP_OK && ssid[0]) {
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        wifi_config_t wcfg = {0};
        strncpy((char *)wcfg.sta.ssid, ssid, sizeof(wcfg.sta.ssid) - 1);
        strncpy((char *)wcfg.sta.password, pass, sizeof(wcfg.sta.password) - 1);
        wcfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wcfg));
        ESP_ERROR_CHECK(esp_wifi_start());
        /* 关闭 Modem sleep，否则部分路由下能拿到 IP 但 80 端口不通 */
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
        s_ap_active = false;
        ESP_LOGI(TAG, "STA start ssid=%s (HTTP waits for got ip)", ssid);
    } else {
        ESP_ERROR_CHECK(start_softap());
    }
    return ESP_OK;
}

esp_err_t desk_wifi_set_sta(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, "ssid", ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(h, "pass", pass ? pass : "");
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err != ESP_OK) {
        return err;
    }

    s_ap_active = false;
    s_connected = false;
    esp_wifi_stop();
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    wifi_config_t wcfg = {0};
    strncpy((char *)wcfg.sta.ssid, ssid, sizeof(wcfg.sta.ssid) - 1);
    strncpy((char *)wcfg.sta.password, pass ? pass : "", sizeof(wcfg.sta.password) - 1);
    wcfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wcfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    return esp_wifi_connect();
}

bool desk_wifi_is_connected(void)
{
    return s_connected;
}

bool desk_wifi_is_ap_active(void)
{
    return s_ap_active;
}

esp_err_t desk_wifi_get_ip(char *buf, size_t len)
{
    if (!buf || len < 8) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ap_active) {
        snprintf(buf, len, "192.168.4.1");
        return ESP_OK;
    }
    if (!s_sta_netif) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(s_sta_netif, &info) != ESP_OK) {
        return ESP_FAIL;
    }
    snprintf(buf, len, IPSTR, IP2STR(&info.ip));
    return ESP_OK;
}
