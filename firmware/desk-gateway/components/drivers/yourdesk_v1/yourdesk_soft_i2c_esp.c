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
#include "hal/gpio_ll.h"
#include "soc/gpio_struct.h"
#include "sdkconfig.h"

static const char *TAG = "yourdesk_soft_i2c";

static DRAM_ATTR yourdesk_soft_i2c_sm_t s_sm;
static DRAM_ATTR QueueHandle_t s_digit_queue;
static DRAM_ATTR bool s_drive_sda_low;
static DRAM_ATTR bool s_ignore_own_sda_edge;
static portMUX_TYPE s_sm_mux = portMUX_INITIALIZER_UNLOCKED;

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
    portEXIT_CRITICAL_ISR(&s_sm_mux);

    if (event.ready && s_digit_queue) {
        BaseType_t higher_priority_task_woken = pdFALSE;
        (void)xQueueSendFromISR(s_digit_queue, &event,
                                &higher_priority_task_woken);
        if (higher_priority_task_woken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
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

esp_err_t yourdesk_soft_i2c_esp_init(QueueHandle_t digit_queue,
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
    s_drive_sda_low = false;
    s_ignore_own_sda_edge = false;
    yourdesk_soft_i2c_sm_init(&s_sm, initial_dr);

    esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
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

    ESP_LOGI(TAG, "software I2C addrs=0x24,0x34-0x37 SCL=%d SDA=%d",
             CONFIG_DESK_I2C_SCL_GPIO, CONFIG_DESK_I2C_SDA_GPIO);
    return ESP_OK;
}

void yourdesk_soft_i2c_esp_set_dr(uint8_t dr)
{
    portENTER_CRITICAL(&s_sm_mux);
    yourdesk_soft_i2c_sm_set_dr(&s_sm, dr);
    portEXIT_CRITICAL(&s_sm_mux);
}
