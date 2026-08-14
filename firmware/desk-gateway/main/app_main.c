/**
 * @file app_main.c
 * @brief Desk Gateway 入口：core + 双 ToF + BLE + Wi-Fi + Web + console
 *
 * HTTP 等 WiFi ready（SoftAP 就绪或 STA 拿到 IP）后再启动，
 * 避免「已 got ip 但 80 端口打不开」。
 */
#include "console_cmd.h"

#include "desk_ble.h"
#include "desk_core.h"
#include "desk_tof.h"
#include "desk_web.h"
#include "desk_wifi.h"
#include "yourdesk_v1.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "app";

static void on_wifi_ready(void)
{
    esp_err_t err = desk_web_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "desk_web_start: %s", esp_err_to_name(err));
    }
}

void app_main(void)
{
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(desk_core_init(&yourdesk_v1_driver));
#if CONFIG_DESK_TOF_ENABLE
    esp_err_t tof_err = desk_tof_start();
    if (tof_err != ESP_OK) {
        /* 测距是只读辅助数据，启动失败不能阻断 STOP、Web 或原厂控制链。 */
        ESP_LOGW(TAG, "desk_tof_start: %s", esp_err_to_name(tof_err));
    }
#endif
    ESP_ERROR_CHECK(desk_ble_start());

    desk_wifi_set_ready_cb(on_wifi_ready);
    ESP_ERROR_CHECK(desk_wifi_init());

    xTaskCreate(console_cmd_task, "console", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "desk-gateway ready (web starts after wifi ready)");
}
