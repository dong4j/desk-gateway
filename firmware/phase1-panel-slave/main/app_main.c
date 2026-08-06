/**
 * @file app_main.c
 * @brief Phase 1 入口：默认 idle → 启动 I²C Slave → 串口控 DR
 *
 * 验收顺序（人在旁盯着桌子）：
 *   up → stop → down → stop →（可选）preset1/4
 * 原厂面板可断开；只接主机 CLK/DAT + 共地；USB-C 供电。
 */
#include "console_cmd.h"
#include "desk_dr.h"
#include "i2c_panel_slave.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "app";

void app_main(void)
{
    /* NVS 预留：后续 WiFi 配网会用；Phase 1 也初始化以免以后改入口 */
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    desk_dr_init();

    ESP_ERROR_CHECK(i2c_panel_slave_start());

    xTaskCreate(console_cmd_task, "console", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "phase1 ready — type 'help' on UART");
}
