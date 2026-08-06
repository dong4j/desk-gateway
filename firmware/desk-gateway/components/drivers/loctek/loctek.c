/**
 * @file loctek.c
 * @brief Loctek 驱动占位：动作一律 NOT_SUPPORTED
 */
#include "desk_driver.h"

#include "esp_log.h"

static const char *TAG = "loctek";

static esp_err_t lk_init(void)
{
    ESP_LOGW(TAG, "stub driver — protocol not implemented");
    return ESP_OK;
}
static esp_err_t lk_deinit(void) { return ESP_OK; }
static esp_err_t lk_stop(void) { return ESP_ERR_NOT_SUPPORTED; }
static esp_err_t lk_hold_up(void) { return ESP_ERR_NOT_SUPPORTED; }
static esp_err_t lk_hold_down(void) { return ESP_ERR_NOT_SUPPORTED; }
static esp_err_t lk_goto(uint8_t n) { (void)n; return ESP_ERR_NOT_SUPPORTED; }
static esp_err_t lk_save(uint8_t n) { (void)n; return ESP_ERR_NOT_SUPPORTED; }
static esp_err_t lk_height(int *mm) { (void)mm; return ESP_ERR_NOT_SUPPORTED; }
static desk_status_t lk_status(void) { return DESK_STATUS_IDLE; }
static desk_caps_t lk_caps(void)
{
    return (desk_caps_t){0};
}

const desk_driver_t loctek_driver = {
    .name = "loctek",
    .init = lk_init,
    .deinit = lk_deinit,
    .stop = lk_stop,
    .hold_up = lk_hold_up,
    .hold_down = lk_hold_down,
    .goto_preset = lk_goto,
    .save_preset = lk_save,
    .get_height_mm = lk_height,
    .get_status = lk_status,
    .get_caps = lk_caps,
};
