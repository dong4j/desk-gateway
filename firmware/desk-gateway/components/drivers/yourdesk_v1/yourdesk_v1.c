/**
 * @file yourdesk_v1.c
 * @brief yourdesk_v1：I²C Slave 回 DR + desk_driver ops
 *
 * 超时/童锁在 desk_core；本文件只维护当前 DR 并应答主机轮询。
 * 键码契约见 docs/3-protocol-reverse-notes.md §18。
 */
#include "yourdesk_v1.h"

#include "driver/i2c_slave.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <stdatomic.h>

static const char *TAG = "yourdesk_v1";

#define ADDR_KEY_7BIT  0x24u
#define DR_IDLE        0x2Eu
#define DR_UP          0x47u
#define DR_DOWN        0x4Fu
#define DR_P1_GOTO     0x17u
#define DR_P1_SAVE     0x57u
#define DR_P4_GOTO     0x2Fu
#define DR_P4_SAVE     0x6Fu

typedef struct {
    i2c_slave_dev_handle_t handle;
    QueueHandle_t tx_q;
} slave_ctx_t;

static slave_ctx_t s_ctx;
static atomic_uint_fast8_t s_dr;

static bool IRAM_ATTR on_receive_cb(i2c_slave_dev_handle_t i2c_slave,
                                    const i2c_slave_rx_done_event_data_t *evt_data,
                                    void *arg)
{
    (void)i2c_slave;
    (void)evt_data;
    (void)arg;
    return false;
}

static bool IRAM_ATTR on_request_cb(i2c_slave_dev_handle_t i2c_slave,
                                    const i2c_slave_request_event_data_t *evt_data,
                                    void *arg)
{
    (void)i2c_slave;
    (void)evt_data;
    slave_ctx_t *ctx = (slave_ctx_t *)arg;
    uint8_t token = 1;
    BaseType_t hp = pdFALSE;
    xQueueOverwriteFromISR(ctx->tx_q, &token, &hp);
    return hp == pdTRUE;
}

static void slave_tx_task(void *arg)
{
    slave_ctx_t *ctx = (slave_ctx_t *)arg;
    uint8_t token;
    for (;;) {
        if (xQueueReceive(ctx->tx_q, &token, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        uint8_t dr = (uint8_t)atomic_load(&s_dr);
        uint32_t written = 0;
        (void)i2c_slave_write(ctx->handle, &dr, 1, &written, 50);
    }
}

static esp_err_t set_dr(uint8_t dr)
{
    atomic_store(&s_dr, dr);
    ESP_LOGI(TAG, "DR=0x%02X", dr);
    return ESP_OK;
}

static esp_err_t yd_init(void)
{
    atomic_store(&s_dr, DR_IDLE);
    s_ctx.tx_q = xQueueCreate(1, sizeof(uint8_t));
    if (!s_ctx.tx_q) {
        return ESP_ERR_NO_MEM;
    }

    i2c_slave_config_t cfg = {
        .i2c_port = CONFIG_DESK_I2C_PORT,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .scl_io_num = CONFIG_DESK_I2C_SCL_GPIO,
        .sda_io_num = CONFIG_DESK_I2C_SDA_GPIO,
        .slave_addr = ADDR_KEY_7BIT,
        .send_buf_depth = 64,
        .receive_buf_depth = 64,
        .addr_bit_len = I2C_ADDR_BIT_LEN_7,
    };
    esp_err_t err = i2c_new_slave_device(&cfg, &s_ctx.handle);
    if (err != ESP_OK) {
        return err;
    }
    i2c_slave_event_callbacks_t cbs = {
        .on_request = on_request_cb,
        .on_receive = on_receive_cb,
    };
    ESP_ERROR_CHECK(i2c_slave_register_event_callbacks(s_ctx.handle, &cbs, &s_ctx));
    if (xTaskCreatePinnedToCore(slave_tx_task, "yd_i2c_tx", 4096, &s_ctx,
                                configMAX_PRIORITIES - 2, NULL, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "I2C slave @0x%02X SCL=%d SDA=%d", ADDR_KEY_7BIT,
             CONFIG_DESK_I2C_SCL_GPIO, CONFIG_DESK_I2C_SDA_GPIO);
    return ESP_OK;
}

static esp_err_t yd_deinit(void)
{
    return ESP_OK;
}

static esp_err_t yd_stop(void)
{
    return set_dr(DR_IDLE);
}

static esp_err_t yd_hold_up(void)
{
    return set_dr(DR_UP);
}

static esp_err_t yd_hold_down(void)
{
    return set_dr(DR_DOWN);
}

static esp_err_t yd_goto_preset(uint8_t n)
{
    if (n == 1) {
        return set_dr(DR_P1_GOTO);
    }
    if (n == 4) {
        return set_dr(DR_P4_GOTO);
    }
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t yd_save_preset(uint8_t n)
{
    if (n == 1) {
        return set_dr(DR_P1_SAVE);
    }
    if (n == 4) {
        return set_dr(DR_P4_SAVE);
    }
    return ESP_ERR_NOT_SUPPORTED;
}

static esp_err_t yd_get_height_mm(int *out_mm)
{
    (void)out_mm;
    return ESP_ERR_NOT_SUPPORTED;
}

static desk_status_t yd_get_status(void)
{
    uint8_t dr = (uint8_t)atomic_load(&s_dr);
    switch (dr) {
    case DR_UP:
        return DESK_STATUS_MOVING_UP;
    case DR_DOWN:
        return DESK_STATUS_MOVING_DOWN;
    case DR_P1_GOTO:
    case DR_P4_GOTO:
    case DR_P1_SAVE:
    case DR_P4_SAVE:
        return DESK_STATUS_GOTO_PRESET;
    case DR_IDLE:
    default:
        return DESK_STATUS_IDLE;
    }
}

static desk_caps_t yd_get_caps(void)
{
    return (desk_caps_t){
        .hold_up_down = true,
        .preset_goto = true,
        .preset_save = true,
        .height = false,
        .preset_mask = (1u << 0) | (1u << 3), /* 1 and 4 */
    };
}

const desk_driver_t yourdesk_v1_driver = {
    .name = "yourdesk_v1",
    .init = yd_init,
    .deinit = yd_deinit,
    .stop = yd_stop,
    .hold_up = yd_hold_up,
    .hold_down = yd_hold_down,
    .goto_preset = yd_goto_preset,
    .save_preset = yd_save_preset,
    .get_height_mm = yd_get_height_mm,
    .get_status = yd_get_status,
    .get_caps = yd_get_caps,
};
