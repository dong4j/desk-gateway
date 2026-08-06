/**
 * @file jiecang.c
 * @brief Jiecang 驱动占位
 */
#include "desk_driver.h"
#include "esp_log.h"

static const char *TAG = "jiecang";

static esp_err_t jc_init(void)
{
    ESP_LOGW(TAG, "stub driver — protocol not implemented");
    return ESP_OK;
}
static esp_err_t jc_deinit(void) { return ESP_OK; }
static esp_err_t jc_ns(void) { return ESP_ERR_NOT_SUPPORTED; }
static esp_err_t jc_ns_n(uint8_t n) { (void)n; return ESP_ERR_NOT_SUPPORTED; }
static esp_err_t jc_ns_h(int *mm) { (void)mm; return ESP_ERR_NOT_SUPPORTED; }
static desk_status_t jc_status(void) { return DESK_STATUS_IDLE; }
static desk_caps_t jc_caps(void) { return (desk_caps_t){0}; }

const desk_driver_t jiecang_driver = {
    .name = "jiecang",
    .init = jc_init,
    .deinit = jc_deinit,
    .stop = jc_ns,
    .hold_up = jc_ns,
    .hold_down = jc_ns,
    .goto_preset = jc_ns_n,
    .save_preset = jc_ns_n,
    .get_height_mm = jc_ns_h,
    .get_status = jc_status,
    .get_caps = jc_caps,
};
