/**
 * @file yourdesk_v1.c
 * @brief yourdesk_v1：I²C Slave 回 DR + 实验性 TM1650 高度监听
 *
 * 超时/童锁在 desk_core；本文件维护当前 DR、应答主机轮询。GPIO 高度
 * 监听经真机验证会干扰控桌链路，因此只能在显式实验配置下启用。
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

#if CONFIG_DESK_YOURDESK_HEIGHT_SNIFFER_EXPERIMENTAL
#include "tm1650_height_decoder.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_intr_alloc.h"

/* The experimental sniffer remains active while Flash cache is disabled. */
#ifndef CONFIG_GPIO_CTRL_FUNC_IN_IRAM
#error "yourdesk_v1 height sniffer requires CONFIG_GPIO_CTRL_FUNC_IN_IRAM=y"
#endif
#endif

static const char *TAG = "yourdesk_v1";

#define ADDR_KEY_7BIT  0x24u
#define DR_IDLE        0x2Eu
#define DR_UP          0x47u
#define DR_DOWN        0x4Fu
#define DR_P1_GOTO     0x17u
#define DR_P1_SAVE     0x57u
#define DR_P4_GOTO     0x2Fu
#define DR_P4_SAVE     0x6Fu

#if CONFIG_DESK_YOURDESK_HEIGHT_SNIFFER_EXPERIMENTAL
#define ADDR_DIG1_7BIT 0x34u
#define ADDR_DIG4_7BIT 0x37u
/* Static captures refresh every 2-3 s; allow one missed frame without UI flicker. */
#define HEIGHT_STALE_MS 5000u

typedef struct {
    uint8_t addr7;
    uint8_t segment;
} digit_event_t;
#endif

typedef struct {
    i2c_slave_dev_handle_t handle;
    QueueHandle_t tx_q;
#if CONFIG_DESK_YOURDESK_HEIGHT_SNIFFER_EXPERIMENTAL
    QueueHandle_t digit_q;
#endif
} slave_ctx_t;

#if CONFIG_DESK_YOURDESK_HEIGHT_SNIFFER_EXPERIMENTAL
/** ISR-only I²C frame state; the listener never drives CLK or DAT. */
typedef struct {
    bool in_frame;
    uint8_t bit_index;
    uint8_t byte_accumulator;
    uint8_t byte_count;
    uint8_t bytes[2];
} bus_sniffer_state_t;
#endif

static slave_ctx_t s_ctx;
static atomic_uint_fast8_t s_dr;

#if CONFIG_DESK_YOURDESK_HEIGHT_SNIFFER_EXPERIMENTAL
static atomic_int s_height_mm;
static atomic_uint_fast32_t s_height_tick;
static bus_sniffer_state_t s_sniffer;
static portMUX_TYPE s_sniffer_mux = portMUX_INITIALIZER_UNLOCKED;

/** Sample one bus bit on every SCL rising edge. */
static void IRAM_ATTR sniffer_scl_rising_isr(void *arg)
{
    (void)arg;
    portENTER_CRITICAL_ISR(&s_sniffer_mux);
    if (!s_sniffer.in_frame) {
        portEXIT_CRITICAL_ISR(&s_sniffer_mux);
        return;
    }

    if (s_sniffer.bit_index < 8) {
        s_sniffer.byte_accumulator =
            (uint8_t)((s_sniffer.byte_accumulator << 1) |
                      (gpio_get_level(CONFIG_DESK_I2C_SDA_GPIO) ? 1u : 0u));
    }
    s_sniffer.bit_index++;
    if (s_sniffer.bit_index == 9) {
        /* The ninth bit is ACK/NACK. Keep the preceding byte either way. */
        if (s_sniffer.byte_count < sizeof(s_sniffer.bytes)) {
            s_sniffer.bytes[s_sniffer.byte_count] = s_sniffer.byte_accumulator;
        }
        s_sniffer.byte_count++;
        s_sniffer.bit_index = 0;
        s_sniffer.byte_accumulator = 0;
    }
    portEXIT_CRITICAL_ISR(&s_sniffer_mux);
}

/** Detect START/STOP from DAT edges while SCL is high and queue complete digit writes. */
static void IRAM_ATTR sniffer_sda_edge_isr(void *arg)
{
    slave_ctx_t *ctx = (slave_ctx_t *)arg;
    if (!gpio_get_level(CONFIG_DESK_I2C_SCL_GPIO)) {
        return; /* Normal data transition while SCL is low. */
    }

    digit_event_t event = {0};
    bool queue_event = false;
    portENTER_CRITICAL_ISR(&s_sniffer_mux);
    if (!gpio_get_level(CONFIG_DESK_I2C_SDA_GPIO)) {
        /* START or repeated START: begin a fresh address phase. */
        s_sniffer.in_frame = true;
        s_sniffer.bit_index = 0;
        s_sniffer.byte_accumulator = 0;
        s_sniffer.byte_count = 0;
        s_sniffer.bytes[0] = 0;
        s_sniffer.bytes[1] = 0;
    } else {
        /* STOP: a digit write is exactly address+W followed by one data byte. */
        if (s_sniffer.in_frame && s_sniffer.byte_count == 2 &&
            (s_sniffer.bytes[0] & 1u) == 0) {
            uint8_t addr7 = (uint8_t)(s_sniffer.bytes[0] >> 1);
            if (addr7 >= ADDR_DIG1_7BIT && addr7 <= ADDR_DIG4_7BIT) {
                event.addr7 = addr7;
                event.segment = s_sniffer.bytes[1];
                queue_event = true;
            }
        }
        s_sniffer.in_frame = false;
    }
    portEXIT_CRITICAL_ISR(&s_sniffer_mux);

    if (queue_event && ctx->digit_q) {
        BaseType_t hp = pdFALSE;
        (void)xQueueSendFromISR(ctx->digit_q, &event, &hp);
        if (hp == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }
}

/** Attach read-only GPIO interrupts without changing the I²C peripheral routing. */
static esp_err_t start_digit_sniffer(slave_ctx_t *ctx)
{
    esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    ESP_RETURN_ON_ERROR(gpio_set_intr_type(CONFIG_DESK_I2C_SCL_GPIO,
                                           GPIO_INTR_POSEDGE),
                        TAG, "set CLK interrupt");
    ESP_RETURN_ON_ERROR(gpio_set_intr_type(CONFIG_DESK_I2C_SDA_GPIO,
                                           GPIO_INTR_ANYEDGE),
                        TAG, "set DAT interrupt");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(CONFIG_DESK_I2C_SCL_GPIO,
                                             sniffer_scl_rising_isr, ctx),
                        TAG, "add CLK handler");
    err = gpio_isr_handler_add(CONFIG_DESK_I2C_SDA_GPIO, sniffer_sda_edge_isr, ctx);
    if (err != ESP_OK) {
        gpio_isr_handler_remove(CONFIG_DESK_I2C_SCL_GPIO);
        return err;
    }
    return ESP_OK;
}

/** Assemble digit events away from ISR context and publish only valid heights. */
static void height_decode_task(void *arg)
{
    slave_ctx_t *ctx = (slave_ctx_t *)arg;
    tm1650_height_decoder_t decoder;
    tm1650_height_decoder_reset(&decoder);
    TickType_t last_event_tick = 0;
    uint32_t last_invalid_raw = UINT32_MAX;
    digit_event_t event;

    for (;;) {
        if (xQueueReceive(ctx->digit_q, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        TickType_t now = xTaskGetTickCount();
        if (last_event_tick != 0 &&
            now - last_event_tick > pdMS_TO_TICKS(50)) {
            tm1650_height_decoder_reset(&decoder);
        }
        last_event_tick = now;

        int height_mm = -1;
        tm1650_height_result_t result =
            tm1650_height_decoder_feed(&decoder, event.addr7, event.segment,
                                        &height_mm);
        if (result == TM1650_HEIGHT_VALID) {
            int previous = atomic_exchange(&s_height_mm, height_mm);
            atomic_store(&s_height_tick, (uint_fast32_t)now);
            last_invalid_raw = UINT32_MAX;
            if (previous != height_mm) {
                ESP_LOGI(TAG, "height=%d.%d cm raw=%02X %02X %02X %02X",
                         height_mm / 10, height_mm % 10,
                         decoder.digits[0], decoder.digits[1],
                         decoder.digits[2], decoder.digits[3]);
            }
        } else if (result == TM1650_HEIGHT_INVALID) {
            uint32_t raw = ((uint32_t)decoder.digits[0] << 24) |
                           ((uint32_t)decoder.digits[1] << 16) |
                           ((uint32_t)decoder.digits[2] << 8) |
                           decoder.digits[3];
            /* Log each unknown pattern once so bus noise cannot flood UART. */
            if (raw != last_invalid_raw) {
                last_invalid_raw = raw;
                ESP_LOGW(TAG, "unknown height raw=%02X %02X %02X %02X",
                         decoder.digits[0], decoder.digits[1],
                         decoder.digits[2], decoder.digits[3]);
            }
        }
    }
}
#endif

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
#if CONFIG_DESK_YOURDESK_HEIGHT_SNIFFER_EXPERIMENTAL
    atomic_store(&s_height_mm, -1);
    atomic_store(&s_height_tick, 0);
#endif
    s_ctx.tx_q = xQueueCreate(1, sizeof(uint8_t));
    if (!s_ctx.tx_q) {
        return ESP_ERR_NO_MEM;
    }
#if CONFIG_DESK_YOURDESK_HEIGHT_SNIFFER_EXPERIMENTAL
    s_ctx.digit_q = xQueueCreate(32, sizeof(digit_event_t));
    if (!s_ctx.digit_q) {
        return ESP_ERR_NO_MEM;
    }
#endif

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
#if CONFIG_DESK_YOURDESK_HEIGHT_SNIFFER_EXPERIMENTAL
    if (xTaskCreatePinnedToCore(height_decode_task, "yd_height", 4096, &s_ctx,
                                configMAX_PRIORITIES - 4, NULL, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    err = start_digit_sniffer(&s_ctx);
    if (err != ESP_OK) {
        /* Height is optional; never sacrifice the already working motion path. */
        ESP_LOGE(TAG, "height sniffer unavailable: %s", esp_err_to_name(err));
    }
#else
    ESP_LOGI(TAG, "experimental GPIO height sniffer disabled");
#endif
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
    if (!out_mm) {
        return ESP_ERR_INVALID_ARG;
    }
#if CONFIG_DESK_YOURDESK_HEIGHT_SNIFFER_EXPERIMENTAL
    int height_mm = atomic_load(&s_height_mm);
    uint_fast32_t last_tick = atomic_load(&s_height_tick);
    TickType_t now = xTaskGetTickCount();
    if (height_mm < 0 || last_tick == 0 ||
        now - (TickType_t)last_tick > pdMS_TO_TICKS(HEIGHT_STALE_MS)) {
        return ESP_ERR_INVALID_STATE;
    }
    *out_mm = height_mm;
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
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
#if CONFIG_DESK_YOURDESK_HEIGHT_SNIFFER_EXPERIMENTAL
        .height = true,
#else
        .height = false,
#endif
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
