/**
 * @file app_main.c
 * @brief Desk Gateway 入口：core + 本地语音提醒 + 外设 + 联网入口
 *
 * HTTP 等 WiFi ready（SoftAP 就绪或 STA 拿到 IP）后再启动，
 * 避免「已 got ip 但 80 端口打不开」。
 */
#include "console_cmd.h"

#include "desk_ble.h"
#include "desk_audio.h"
#include "desk_core.h"
#include "desk_oled.h"
#include "desk_peripheral_i2c.h"
#include "desk_reminder.h"
#include "desk_status_led.h"
#include "desk_tof.h"
#include "desk_web.h"
#include "desk_wifi.h"
#include "mxtark.h"

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

    ESP_ERROR_CHECK(desk_core_init(&mxtark_driver));
#if CONFIG_DESK_STATUS_LED_ENABLE
    esp_err_t led_err = desk_status_led_start();
    if (led_err != ESP_OK) {
        /* 状态灯是辅助指示，缺灯或 GPIO 失败不能挡住 STOP、Web 或原厂控制链。 */
        ESP_LOGW(TAG, "desk_status_led_start: %s", esp_err_to_name(led_err));
    }
#endif
#if CONFIG_DESK_AUDIO_ENABLED
    esp_err_t audio_err = desk_audio_init();
    if (audio_err != ESP_OK) {
        /* 扬声器是辅助能力，缺失时桌控和所有安全入口仍必须启动。 */
        ESP_LOGW(TAG, "desk_audio_init: %s", esp_err_to_name(audio_err));
    }
#endif
#if CONFIG_DESK_REMINDER_ENABLED
    esp_err_t reminder_err = desk_reminder_init();
    if (reminder_err != ESP_OK) {
        /* 提醒失败不能改变 desk_core 的运动行为或启动顺序。 */
        ESP_LOGW(TAG, "desk_reminder_init: %s",
                 esp_err_to_name(reminder_err));
    }
#endif
#if CONFIG_DESK_TOF_ENABLE
    i2c_master_bus_handle_t peripheral_bus = NULL;
    esp_err_t bus_err = desk_peripheral_i2c_start(&peripheral_bus);
    if (bus_err != ESP_OK) {
        ESP_LOGW(TAG, "desk_peripheral_i2c_start: %s",
                 esp_err_to_name(bus_err));
    }
    esp_err_t tof_err = bus_err == ESP_OK
                            ? desk_tof_start(peripheral_bus)
                            : bus_err;
    if (tof_err != ESP_OK) {
        /* 测距是只读辅助数据，启动失败不能阻断 STOP、Web 或原厂控制链。 */
        ESP_LOGW(TAG, "desk_tof_start: %s", esp_err_to_name(tof_err));
    }
#if CONFIG_DESK_OLED_ENABLE
    esp_err_t oled_err = bus_err == ESP_OK
                             ? desk_oled_start(peripheral_bus)
                             : bus_err;
    if (oled_err != ESP_OK) {
        /* OLED 是只读附加显示，失败不能影响控制和其他联网入口。 */
        ESP_LOGW(TAG, "desk_oled_start: %s", esp_err_to_name(oled_err));
    }
#endif
#endif
    ESP_ERROR_CHECK(desk_ble_start());

    desk_wifi_set_ready_cb(on_wifi_ready);
    ESP_ERROR_CHECK(desk_wifi_init());

    xTaskCreate(console_cmd_task, "console", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "desk-gateway ready (web starts after wifi ready)");
}
