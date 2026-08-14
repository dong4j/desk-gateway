/**
 * @file desk_peripheral_i2c.c
 * @brief ToF 与 OLED 的共享 I2C Master 总线。
 */
#include "desk_peripheral_i2c.h"

#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "peripheral_i2c";
static i2c_master_bus_handle_t s_bus;

esp_err_t desk_peripheral_i2c_start(i2c_master_bus_handle_t *out_bus)
{
    if (!out_bus) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_bus) {
        *out_bus = s_bus;
        return ESP_OK;
    }
    const i2c_master_bus_config_t config = {
        .i2c_port = CONFIG_DESK_TOF_I2C_PORT,
        .sda_io_num = CONFIG_DESK_TOF_I2C_SDA_GPIO,
        .scl_io_num = CONFIG_DESK_TOF_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&config, &s_bus);
    if (err != ESP_OK) {
        return err;
    }
    *out_bus = s_bus;
    ESP_LOGI(TAG, "shared I2C%d ready: SCL=%d SDA=%d",
             CONFIG_DESK_TOF_I2C_PORT, CONFIG_DESK_TOF_I2C_SCL_GPIO,
             CONFIG_DESK_TOF_I2C_SDA_GPIO);
    return ESP_OK;
}
