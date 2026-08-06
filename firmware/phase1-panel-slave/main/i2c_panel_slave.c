/**
 * @file i2c_panel_slave.c
 * @brief I²C Slave：Master 写 DW 后读 DR
 *
 * 时序与真面板一致：AW:24 + DW:0x01，再 AR:24 读 1 字节。
 * on_receive：在 ISR 内丢弃写数据（期望 0x01），不进队列，避免拖累 TX。
 * on_request：只唤醒高优先级任务；i2c_slave_write 在任务里执行（驱动不可在 ISR 调）。
 * SCL ~9.6kHz、轮询 ~3.7ms，短暂时钟拉伸通常可接受。
 */
#include "i2c_panel_slave.h"

#include "desk_dr.h"

#include "driver/i2c_slave.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "i2c_slave";

typedef struct {
    i2c_slave_dev_handle_t handle;
    QueueHandle_t tx_q;
} slave_ctx_t;

static slave_ctx_t s_ctx;

/** Master 写完成：数据指针仅在回调内有效，Phase 1 只需 ACK/消费，不解析 DW */
static bool IRAM_ATTR on_receive_cb(i2c_slave_dev_handle_t i2c_slave,
                                    const i2c_slave_rx_done_event_data_t *evt_data,
                                    void *arg)
{
    (void)i2c_slave;
    (void)arg;
    (void)evt_data; /* buffer/length 存在但不使用；驱动已完成接收 */
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
    /* 长度 1 队列 + overwrite：请求风暴时不堆积，始终尽快交最新 DR */
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
        uint8_t dr = desk_dr_get();
        uint32_t written = 0;
        esp_err_t err = i2c_slave_write(ctx->handle, &dr, 1, &written, 50);
        if (err != ESP_OK) {
            ESP_LOGD(TAG, "write DR failed: %s", esp_err_to_name(err));
        }
    }
}

esp_err_t i2c_panel_slave_start(void)
{
    /* 长度 1 + overwrite：请求风暴时不堆积延迟 */
    s_ctx.tx_q = xQueueCreate(1, sizeof(uint8_t));
    if (!s_ctx.tx_q) {
        return ESP_ERR_NO_MEM;
    }

    i2c_slave_config_t cfg = {
        .i2c_port = CONFIG_DESK_I2C_PORT,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .scl_io_num = CONFIG_DESK_I2C_SCL_GPIO,
        .sda_io_num = CONFIG_DESK_I2C_SDA_GPIO,
        .slave_addr = DESK_I2C_ADDR_KEY_7BIT,
        .send_buf_depth = 64,
        .receive_buf_depth = 64,
        .addr_bit_len = I2C_ADDR_BIT_LEN_7,
    };

    esp_err_t err = i2c_new_slave_device(&cfg, &s_ctx.handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_slave_device: %s", esp_err_to_name(err));
        return err;
    }

    i2c_slave_event_callbacks_t cbs = {
        .on_request = on_request_cb,
        .on_receive = on_receive_cb,
    };
    ESP_ERROR_CHECK(i2c_slave_register_event_callbacks(s_ctx.handle, &cbs, &s_ctx));

    BaseType_t ok = xTaskCreatePinnedToCore(
        slave_tx_task, "i2c_tx", 4096, &s_ctx, configMAX_PRIORITIES - 2, NULL, 0);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG,
             "slave @0x%02X SCL=GPIO%d SDA=GPIO%d (USB power, shared GND)",
             DESK_I2C_ADDR_KEY_7BIT,
             CONFIG_DESK_I2C_SCL_GPIO,
             CONFIG_DESK_I2C_SDA_GPIO);
    return ESP_OK;
}
