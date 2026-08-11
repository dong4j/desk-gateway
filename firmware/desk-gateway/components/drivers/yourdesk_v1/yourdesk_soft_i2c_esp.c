/**
 * @file yourdesk_soft_i2c_esp.c
 * @brief IRAM-safe GPIO bridge for the yourdesk_v1 software I2C slave.
 *
 * External 2 kOhm pull-ups provide the bus-high level. DAT is configured as
 * open-drain and is only pulled low for ACK/data zero bits; writing one releases
 * it. No logging or allocation is performed from interrupt context.
 */
#include "yourdesk_soft_i2c_esp.h"

#include "yourdesk_soft_i2c_sm.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_struct.h"
#include "sdkconfig.h"

static const char *TAG = "yourdesk_soft_i2c";

static DRAM_ATTR yourdesk_soft_i2c_sm_t s_sm;
static DRAM_ATTR QueueHandle_t s_digit_queue;
static DRAM_ATTR QueueHandle_t s_mirror_digit_queue;
static DRAM_ATTR bool s_drive_sda_low;
static DRAM_ATTR bool s_ignore_own_sda_edge;
static DRAM_ATTR uint32_t s_completed_key_reads;
static DRAM_ATTR uint32_t s_last_key_read_ms;
static DRAM_ATTR uint32_t s_max_key_read_gap_ms;
static portMUX_TYPE s_sm_mux = portMUX_INITIALIZER_UNLOCKED;

typedef struct {
    QueueHandle_t digit_queue;
    QueueHandle_t mirror_digit_queue;
    uint8_t initial_dr;
    SemaphoreHandle_t done;
    esp_err_t result;
} soft_i2c_init_context_t;

/** Read a GPIO level without calling Flash-resident driver code from the ISR. */
static inline bool IRAM_ATTR line_is_high(gpio_num_t gpio)
{
    return gpio_ll_get_level(&GPIO, (uint32_t)gpio) != 0;
}

/** Apply the state-machine output and suppress the matching self-generated edge. */
static inline void IRAM_ATTR apply_sda_output(void)
{
    bool drive_low = s_sm.drive_sda_low;
    if (drive_low == s_drive_sda_low) {
        return;
    }
    bool was_high = line_is_high(CONFIG_DESK_I2C_SDA_GPIO);
    s_drive_sda_low = drive_low;
    gpio_ll_set_level(&GPIO, CONFIG_DESK_I2C_SDA_GPIO, drive_low ? 0u : 1u);
    /* Do not suppress a future master edge when the wired-AND level did not move. */
    s_ignore_own_sda_edge =
        was_high != line_is_high(CONFIG_DESK_I2C_SDA_GPIO);
}

/** Advance receive/transmit timing on both SCL edges. */
static void IRAM_ATTR scl_edge_isr(void *arg)
{
    (void)arg;
    yourdesk_soft_i2c_digit_event_t event = {0};

    portENTER_CRITICAL_ISR(&s_sm_mux);
    if (line_is_high(CONFIG_DESK_I2C_SCL_GPIO)) {
        yourdesk_soft_i2c_sm_scl_rising(
            &s_sm, line_is_high(CONFIG_DESK_I2C_SDA_GPIO));
    } else {
        event = yourdesk_soft_i2c_sm_scl_falling(&s_sm);
        apply_sda_output();
    }
    if (event.key_read_completed) {
        uint32_t now_ms =
            (uint32_t)xTaskGetTickCountFromISR() * portTICK_PERIOD_MS;
        if (s_last_key_read_ms != 0) {
            uint32_t gap_ms = now_ms - s_last_key_read_ms;
            if (gap_ms > s_max_key_read_gap_ms) {
                s_max_key_read_gap_ms = gap_ms;
            }
        }
        s_last_key_read_ms = now_ms;
        s_completed_key_reads++;
    }
    portEXIT_CRITICAL_ISR(&s_sm_mux);

    if (event.ready && s_digit_queue) {
        BaseType_t higher_priority_task_woken = pdFALSE;
        (void)xQueueSendFromISR(s_digit_queue, &event,
                                &higher_priority_task_woken);
        if (s_mirror_digit_queue) {
            BaseType_t mirror_task_woken = pdFALSE;
            (void)xQueueSendFromISR(s_mirror_digit_queue, &event,
                                    &mirror_task_woken);
            if (mirror_task_woken == pdTRUE) {
                higher_priority_task_woken = pdTRUE;
            }
        }
        /*
         * Do not immediately schedule height decoding between adjacent 52 us
         * bus edges. The queue is drained on the next normal scheduler turn.
         */
        (void)higher_priority_task_woken;
    }
}

/** Detect START/repeated START/STOP from DAT transitions while SCL is high. */
static void IRAM_ATTR sda_edge_isr(void *arg)
{
    (void)arg;
    portENTER_CRITICAL_ISR(&s_sm_mux);
    if (s_ignore_own_sda_edge) {
        s_ignore_own_sda_edge = false;
        portEXIT_CRITICAL_ISR(&s_sm_mux);
        return;
    }
    if (!line_is_high(CONFIG_DESK_I2C_SCL_GPIO)) {
        portEXIT_CRITICAL_ISR(&s_sm_mux);
        return;
    }

    if (line_is_high(CONFIG_DESK_I2C_SDA_GPIO)) {
        yourdesk_soft_i2c_sm_stop(&s_sm);
    } else {
        yourdesk_soft_i2c_sm_start(&s_sm);
    }
    apply_sda_output();
    portEXIT_CRITICAL_ISR(&s_sm_mux);
}

/** Configure and bind the GPIO service on the core executing this function. */
static esp_err_t init_on_current_core(QueueHandle_t digit_queue,
                                      QueueHandle_t mirror_digit_queue,
                                      uint8_t initial_dr)
{
    ESP_RETURN_ON_FALSE(digit_queue, ESP_ERR_INVALID_ARG, TAG,
                        "digit queue is required");

    gpio_config_t scl_cfg = {
        .pin_bit_mask = 1ULL << CONFIG_DESK_I2C_SCL_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&scl_cfg), TAG, "configure CLK");

    gpio_config_t sda_cfg = {
        .pin_bit_mask = 1ULL << CONFIG_DESK_I2C_SDA_GPIO,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&sda_cfg), TAG, "configure DAT");
    ESP_RETURN_ON_ERROR(gpio_set_level(CONFIG_DESK_I2C_SDA_GPIO, 1), TAG,
                        "release DAT");

    s_digit_queue = digit_queue;
    s_mirror_digit_queue = mirror_digit_queue;
    s_drive_sda_low = false;
    s_ignore_own_sda_edge = false;
    s_completed_key_reads = 0;
    s_last_key_read_ms = 0;
    s_max_key_read_gap_ms = 0;
    yourdesk_soft_i2c_sm_init(&s_sm, initial_dr);

    /*
     * Level 3 on Core 1 keeps the 9.6 kHz slave ahead of Wi-Fi/NimBLE work on
     * Core 0. FreeRTOS FromISR queue APIs remain valid at this interrupt level.
     */
    esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM |
                                             ESP_INTR_FLAG_LEVEL3);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(CONFIG_DESK_I2C_SCL_GPIO,
                                             scl_edge_isr, NULL),
                        TAG, "attach CLK interrupt");
    err = gpio_isr_handler_add(CONFIG_DESK_I2C_SDA_GPIO, sda_edge_isr, NULL);
    if (err != ESP_OK) {
        (void)gpio_isr_handler_remove(CONFIG_DESK_I2C_SCL_GPIO);
        return err;
    }

    ESP_LOGI(TAG,
             "software I2C addrs=0x24,0x34-0x37 SCL=%d SDA=%d ISR-core=%d",
             CONFIG_DESK_I2C_SCL_GPIO, CONFIG_DESK_I2C_SDA_GPIO,
             xPortGetCoreID());
    return ESP_OK;
}

/** Install the global GPIO ISR service from Core 1, then wake the caller. */
static void soft_i2c_init_task(void *arg)
{
    soft_i2c_init_context_t *ctx = (soft_i2c_init_context_t *)arg;
    ctx->result = init_on_current_core(ctx->digit_queue,
                                       ctx->mirror_digit_queue,
                                       ctx->initial_dr);
    xSemaphoreGive(ctx->done);
    vTaskDelete(NULL);
}

esp_err_t yourdesk_soft_i2c_esp_init(QueueHandle_t digit_queue,
                                    QueueHandle_t mirror_digit_queue,
                                    uint8_t initial_dr)
{
    ESP_RETURN_ON_FALSE(digit_queue, ESP_ERR_INVALID_ARG, TAG,
                        "digit queue is required");

    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    if (!done) {
        return ESP_ERR_NO_MEM;
    }
    soft_i2c_init_context_t ctx = {
        .digit_queue = digit_queue,
        .mirror_digit_queue = mirror_digit_queue,
        .initial_dr = initial_dr,
        .done = done,
        .result = ESP_FAIL,
    };
    if (xTaskCreatePinnedToCore(soft_i2c_init_task, "yd_i2c_init", 3072,
                                &ctx, configMAX_PRIORITIES - 1,
                                NULL, 1) != pdPASS) {
        vSemaphoreDelete(done);
        return ESP_ERR_NO_MEM;
    }

    /* The stack-backed context remains valid until the short init task exits. */
    (void)xSemaphoreTake(done, portMAX_DELAY);
    vSemaphoreDelete(done);
    return ctx.result;
}

void yourdesk_soft_i2c_esp_set_dr(uint8_t dr)
{
    portENTER_CRITICAL(&s_sm_mux);
    yourdesk_soft_i2c_sm_set_dr(&s_sm, dr);
    portEXIT_CRITICAL(&s_sm_mux);
}

void yourdesk_soft_i2c_esp_take_stats(yourdesk_soft_i2c_stats_t *stats)
{
    if (!stats) {
        return;
    }
    portENTER_CRITICAL(&s_sm_mux);
    stats->completed_key_reads = s_completed_key_reads;
    stats->last_key_read_ms = s_last_key_read_ms;
    stats->max_key_read_gap_ms = s_max_key_read_gap_ms;
    s_max_key_read_gap_ms = 0;
    portEXIT_CRITICAL(&s_sm_mux);
}
