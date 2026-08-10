/**
 * @file yourdesk_panel_proxy.c
 * @brief Transaction-level bridge from ESP32-S3 to the original TM1650 panel.
 *
 * The panel is a slave, so transparent Phase 2 bridging requires ESP32 to be a
 * master on this isolated side. Key reads are cached asynchronously; the
 * timing-sensitive control-box slave ISR never waits for a second I2C bus.
 */
#include "yourdesk_panel_proxy.h"

#include "yourdesk_soft_i2c_sm.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <stddef.h>

#if CONFIG_DESK_YOURDESK_PANEL_PROXY

#include "driver/i2c.h"
#include "esp_rom_sys.h"

static const char *TAG = "yourdesk_panel";

#define PANEL_KEY_ADDR_7BIT   0x24u
#define PANEL_DIGIT_ADDR_MIN  0x34u
#define PANEL_DIGIT_ADDR_MAX  0x37u
#define PANEL_IDLE_DR         0x2Eu
#define PANEL_CONTROL_DW      0x01u
#define PANEL_I2C_SPEED_HZ    9600u
#define PANEL_XFER_TIMEOUT_MS 10
#define PANEL_WRITE_READ_GAP_US 30u
#define PANEL_POLL_GAP_US       95u
#define PANEL_DIGIT_BURST_MAX 8

typedef struct {
    i2c_port_t port;
    QueueHandle_t digit_queue;
    yourdesk_panel_key_callback_t key_callback;
    void *callback_ctx;
    /* Reused by the single proxy task to avoid heap churn every 4 ms. */
    uint8_t key_cmd_buffer[I2C_LINK_RECOMMENDED_SIZE(2)];
    uint8_t digit_cmd_buffer[I2C_LINK_RECOMMENDED_SIZE(1)];
} panel_proxy_ctx_t;

static panel_proxy_ctx_t s_panel;

/** Send one complete address+data+STOP transaction on the panel bus. */
static esp_err_t write_panel_byte(uint8_t address, uint8_t data,
                                  uint8_t *command_buffer,
                                  size_t command_buffer_size)
{
    if (!command_buffer || command_buffer_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_cmd_handle_t cmd = i2c_cmd_link_create_static(
        command_buffer, command_buffer_size);
    if (!cmd) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = i2c_master_start(cmd);
    if (err == ESP_OK) {
        err = i2c_master_write_byte(
            cmd, (uint8_t)((address << 1) | I2C_MASTER_WRITE), true);
    }
    if (err == ESP_OK) {
        err = i2c_master_write_byte(cmd, data, true);
    }
    if (err == ESP_OK) {
        err = i2c_master_stop(cmd);
    }
    if (err == ESP_OK) {
        err = i2c_master_cmd_begin(
            s_panel.port, cmd, pdMS_TO_TICKS(PANEL_XFER_TIMEOUT_MS));
    }
    i2c_cmd_link_delete_static(cmd);
    return err;
}

/** Execute the two STOP-separated key transactions seen in the raw capture. */
static esp_err_t poll_panel_key(uint8_t *dr)
{
    if (!dr) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = write_panel_byte(
        PANEL_KEY_ADDR_7BIT, PANEL_CONTROL_DW, s_panel.key_cmd_buffer,
        sizeof(s_panel.key_cmd_buffer));
    if (err != ESP_OK) {
        return err;
    }

    /* idle_12mhz_full.sr measures about 29 us from write STOP to read START. */
    esp_rom_delay_us(PANEL_WRITE_READ_GAP_US);

    i2c_cmd_handle_t cmd = i2c_cmd_link_create_static(
        s_panel.key_cmd_buffer, sizeof(s_panel.key_cmd_buffer));
    if (!cmd) {
        return ESP_ERR_NO_MEM;
    }

    err = i2c_master_start(cmd);
    if (err == ESP_OK) {
        err = i2c_master_write_byte(
            cmd, (uint8_t)((PANEL_KEY_ADDR_7BIT << 1) | I2C_MASTER_READ),
            true);
    }
    if (err == ESP_OK) {
        /* The controller ACKs the only DR byte before issuing STOP. */
        err = i2c_master_read_byte(cmd, dr, I2C_MASTER_ACK);
    }
    if (err == ESP_OK) {
        err = i2c_master_stop(cmd);
    }
    if (err == ESP_OK) {
        err = i2c_master_cmd_begin(
            s_panel.port, cmd, pdMS_TO_TICKS(PANEL_XFER_TIMEOUT_MS));
    }
    i2c_cmd_link_delete_static(cmd);
    return err;
}

/** Forward one controller display write to its matching panel digit address. */
static esp_err_t forward_digit(const yourdesk_soft_i2c_digit_event_t *event)
{
    if (!event || event->addr7 < PANEL_DIGIT_ADDR_MIN ||
        event->addr7 > PANEL_DIGIT_ADDR_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    return write_panel_byte(event->addr7, event->segment,
                            s_panel.digit_cmd_buffer,
                            sizeof(s_panel.digit_cmd_buffer));
}

/**
 * Poll keys and drain display writes without coupling either operation to the
 * control-box ISR. A failed key transaction is immediately published as idle.
 */
static void panel_proxy_task(void *arg)
{
    (void)arg;
    bool connected = false;
    uint8_t published_dr = PANEL_IDLE_DR;
    yourdesk_soft_i2c_digit_event_t event;

    for (;;) {
        for (int i = 0; i < PANEL_DIGIT_BURST_MAX; ++i) {
            if (xQueueReceive(s_panel.digit_queue, &event, 0) != pdTRUE) {
                break;
            }
            (void)forward_digit(&event);
        }

        uint8_t dr = PANEL_IDLE_DR;
        esp_err_t err = poll_panel_key(&dr);
        bool next_connected = err == ESP_OK;
        if (!next_connected) {
            dr = PANEL_IDLE_DR; /* NACK/timeout must never leave motion latched. */
        }

        bool connection_changed = next_connected != connected;
        bool dr_changed = dr != published_dr;
        if (connection_changed || dr_changed) {
            if (connection_changed) {
                if (next_connected) {
                    ESP_LOGI(TAG, "original panel connected raw DR=0x%02X",
                             dr);
                } else {
                    ESP_LOGI(TAG, "original panel disconnected");
                }
            } else if (next_connected) {
                ESP_LOGI(TAG, "panel raw DR=0x%02X", dr);
            }
            connected = next_connected;
            published_dr = dr;
            s_panel.key_callback(connected, published_dr,
                                 s_panel.callback_ctx);
        }
        /* Original idle capture measures about 95 us before the next write. */
        esp_rom_delay_us(PANEL_POLL_GAP_US);
    }
}

esp_err_t yourdesk_panel_proxy_init(QueueHandle_t digit_queue,
                                    yourdesk_panel_key_callback_t key_callback,
                                    void *callback_ctx)
{
    ESP_RETURN_ON_FALSE(digit_queue && key_callback, ESP_ERR_INVALID_ARG, TAG,
                        "digit queue and key callback are required");
    ESP_RETURN_ON_FALSE(CONFIG_DESK_PANEL_I2C_SCL_GPIO !=
                            CONFIG_DESK_PANEL_I2C_SDA_GPIO,
                        ESP_ERR_INVALID_ARG, TAG,
                        "panel CLK and DAT GPIO must differ");
    ESP_RETURN_ON_FALSE(CONFIG_DESK_PANEL_I2C_SCL_GPIO !=
                                CONFIG_DESK_I2C_SCL_GPIO &&
                            CONFIG_DESK_PANEL_I2C_SCL_GPIO !=
                                CONFIG_DESK_I2C_SDA_GPIO &&
                            CONFIG_DESK_PANEL_I2C_SDA_GPIO !=
                                CONFIG_DESK_I2C_SCL_GPIO &&
                            CONFIG_DESK_PANEL_I2C_SDA_GPIO !=
                                CONFIG_DESK_I2C_SDA_GPIO,
                        ESP_ERR_INVALID_ARG, TAG,
                        "panel and control-box buses must be isolated");

    s_panel = (panel_proxy_ctx_t){
        .port = (i2c_port_t)CONFIG_DESK_PANEL_I2C_PORT,
        .digit_queue = digit_queue,
        .key_callback = key_callback,
        .callback_ctx = callback_ctx,
    };
    i2c_config_t bus_config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = CONFIG_DESK_PANEL_I2C_SDA_GPIO,
        .scl_io_num = CONFIG_DESK_PANEL_I2C_SCL_GPIO,
        .sda_pullup_en = false,
        .scl_pullup_en = false,
        .master.clk_speed = PANEL_I2C_SPEED_HZ,
        .clk_flags = 0,
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(s_panel.port, &bus_config), TAG,
                        "configure panel I2C master");
    ESP_RETURN_ON_ERROR(
        i2c_driver_install(s_panel.port, I2C_MODE_MASTER, 0, 0, 0), TAG,
        "install panel I2C master");

    if (xTaskCreatePinnedToCore(panel_proxy_task, "yd_panel", 4096, NULL,
                                configMAX_PRIORITIES - 5, NULL, 1) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG,
             "panel proxy SCL=%d SDA=%d 9.6kHz split-STOP ACK+STOP",
             CONFIG_DESK_PANEL_I2C_SCL_GPIO,
             CONFIG_DESK_PANEL_I2C_SDA_GPIO);
    return ESP_OK;
}

#else

esp_err_t yourdesk_panel_proxy_init(QueueHandle_t digit_queue,
                                    yourdesk_panel_key_callback_t key_callback,
                                    void *callback_ctx)
{
    (void)digit_queue;
    (void)key_callback;
    (void)callback_ctx;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
