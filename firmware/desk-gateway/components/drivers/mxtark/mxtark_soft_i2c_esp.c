/**
 * @file mxtark_soft_i2c_esp.c
 * @brief IRAM-safe GPIO bridge for the mxtark software I2C slave.
 *
 * External 2 kOhm pull-ups provide the bus-high level. DAT is configured as
 * open-drain and is only pulled low for ACK/data zero bits; writing one releases
 * it. No logging or allocation is performed from interrupt context.
 */
#include "mxtark_soft_i2c_esp.h"

#include "mxtark_soft_i2c_sm.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_cpu.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_struct.h"
#include "sdkconfig.h"

static const char *TAG = "mxtark_soft_i2c";

static DRAM_ATTR mxtark_soft_i2c_sm_t s_sm;
static DRAM_ATTR QueueHandle_t s_digit_queue;
static DRAM_ATTR QueueHandle_t s_mirror_digit_queue;
static DRAM_ATTR bool s_drive_sda_low;
static DRAM_ATTR bool s_ignore_own_sda_edge;
static DRAM_ATTR mxtark_soft_i2c_stats_t s_stats;
static DRAM_ATTR uint32_t s_last_key_read_cycles;
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

/** Count a key response that was reset before the controller completed it. */
static inline void IRAM_ATTR count_aborted_key_read(void)
{
    if (s_sm.phase == MXTARK_SOFT_I2C_TX_DATA ||
        s_sm.phase == MXTARK_SOFT_I2C_TX_MASTER_ACK) {
        s_stats.aborted_key_reads++;
    }
}

/** Classify the completed address byte without changing its ACK decision. */
static inline void IRAM_ATTR count_key_address(uint8_t raw_address)
{
    if ((raw_address >> 1) != 0x24u) {
        return;
    }
    if ((raw_address & 1u) != 0) {
        s_stats.key_read_addresses++;
    } else {
        s_stats.key_write_addresses++;
    }
}

/** Advance receive/transmit timing on both SCL edges. */
static void IRAM_ATTR scl_edge_isr(void *arg)
{
    (void)arg;
    mxtark_soft_i2c_digit_event_t event = {0};

    portENTER_CRITICAL_ISR(&s_sm_mux);
    if (line_is_high(CONFIG_DESK_I2C_SCL_GPIO)) {
        mxtark_soft_i2c_sm_scl_rising(
            &s_sm, line_is_high(CONFIG_DESK_I2C_SDA_GPIO));
    } else {
        mxtark_soft_i2c_phase_t phase_before = s_sm.phase;
        uint8_t bit_count_before = s_sm.bit_count;
        uint8_t rx_byte_before = s_sm.rx_byte;
        event = mxtark_soft_i2c_sm_scl_falling(&s_sm);
        if (phase_before == MXTARK_SOFT_I2C_RX_ADDRESS &&
            bit_count_before == 8) {
            count_key_address(rx_byte_before);
        }
        if (phase_before == MXTARK_SOFT_I2C_RX_ADDRESS &&
            bit_count_before == 9 &&
            s_sm.phase == MXTARK_SOFT_I2C_TX_DATA) {
            s_stats.key_tx_started++;
        }
        apply_sda_output();
    }
    if (event.key_read_completed) {
        uint32_t now_cycles = esp_cpu_get_cycle_count();
        if (s_last_key_read_cycles != 0) {
            uint32_t gap_cycles = now_cycles - s_last_key_read_cycles;
            if (gap_cycles > s_stats.max_key_read_gap_cycles) {
                s_stats.max_key_read_gap_cycles = gap_cycles;
            }
        }
        s_last_key_read_cycles = now_cycles;
        s_stats.completed_key_reads++;
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
    /*
     * While returning DR, every SDA transition belongs to the slave byte (or
     * its release before the ACK clock). GPIO delivery can lag until SCL is
     * high, where the live levels would otherwise misclassify that edge as a
     * START/STOP and truncate 0x47. Do not alter receive-side boundary logic.
     */
    if (mxtark_soft_i2c_sm_key_tx_active(&s_sm)) {
        s_ignore_own_sda_edge = false;
        s_stats.ignored_sda_edges_during_key_tx++;
        portEXIT_CRITICAL_ISR(&s_sm_mux);
        return;
    }
    if (s_ignore_own_sda_edge) {
        s_ignore_own_sda_edge = false;
        s_stats.ignored_own_sda_edges++;
        portEXIT_CRITICAL_ISR(&s_sm_mux);
        return;
    }
    if (!line_is_high(CONFIG_DESK_I2C_SCL_GPIO)) {
        s_stats.ignored_sda_edges_while_scl_low++;
        portEXIT_CRITICAL_ISR(&s_sm_mux);
        return;
    }

    if (line_is_high(CONFIG_DESK_I2C_SDA_GPIO)) {
        count_aborted_key_read();
        s_stats.recognized_stops++;
        mxtark_soft_i2c_sm_stop(&s_sm);
    } else {
        count_aborted_key_read();
        s_stats.recognized_starts++;
        mxtark_soft_i2c_sm_start(&s_sm);
    }
    apply_sda_output();
    portEXIT_CRITICAL_ISR(&s_sm_mux);
}

esp_err_t mxtark_soft_i2c_esp_init(QueueHandle_t digit_queue,
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
    s_stats = (mxtark_soft_i2c_stats_t){0};
    s_last_key_read_cycles = 0;
    mxtark_soft_i2c_sm_init(&s_sm, initial_dr);

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

void mxtark_soft_i2c_esp_set_dr(uint8_t dr)
{
    portENTER_CRITICAL(&s_sm_mux);
    mxtark_soft_i2c_sm_set_dr(&s_sm, dr);
    portEXIT_CRITICAL(&s_sm_mux);
}

void mxtark_soft_i2c_esp_take_stats(mxtark_soft_i2c_stats_t *stats)
{
    if (!stats) {
        return;
    }
    portENTER_CRITICAL(&s_sm_mux);
    *stats = s_stats;
    stats->phase = (uint8_t)s_sm.phase;
    stats->bit_count = s_sm.bit_count;
    stats->current_addr7 = s_sm.current_addr7;
    stats->tx_dr = s_sm.tx_dr;
    s_stats.max_key_read_gap_cycles = 0;
    portEXIT_CRITICAL(&s_sm_mux);
}
