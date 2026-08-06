/**
 * @file desk_driver_registry.c
 * @brief 单活跃驱动注册表
 */
#include "desk_driver.h"

#include "esp_log.h"

static const char *TAG = "desk_drv";
static const desk_driver_t *s_active;

esp_err_t desk_driver_register(const desk_driver_t *drv)
{
    if (!drv || !drv->name || !drv->init || !drv->stop) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_active && s_active->deinit) {
        (void)s_active->deinit();
    }
    esp_err_t err = drv->init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "init %s failed: %s", drv->name, esp_err_to_name(err));
        s_active = NULL;
        return err;
    }
    s_active = drv;
    ESP_LOGI(TAG, "active driver: %s", drv->name);
    return ESP_OK;
}

const desk_driver_t *desk_driver_get_active(void)
{
    return s_active;
}
