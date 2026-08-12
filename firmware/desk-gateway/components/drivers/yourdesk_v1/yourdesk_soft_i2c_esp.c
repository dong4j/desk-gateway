/**
 * @file yourdesk_soft_i2c_esp.c
 * @brief Main-CPU adapter for the ULP RISC-V yourdesk I2C worker.
 *
 * The ULP core owns every CLK/DAT transition. This adapter only initializes
 * RTCIO, updates the desired DR mailbox, and drains completed digit events into
 * normal FreeRTOS queues. No Wi-Fi/BLE/main-CPU interrupt can delay an I2C bit.
 */
#include "yourdesk_soft_i2c_esp.h"

#include "driver/rtc_io.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "ulp_riscv.h"
#include "yourdesk_i2c_ulp.h"
#include "yourdesk_soft_i2c_sm.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DIGIT_RING_CAPACITY 32u
#define DIGIT_RING_MASK     (DIGIT_RING_CAPACITY - 1u)
#define ULP_READY_TIMEOUT_MS 100u
#define DIGIT_DRAIN_PERIOD_TICKS 1u
#define DIGIT_DRAIN_TASK_PRIORITY (tskIDLE_PRIORITY + 5u)

_Static_assert(DIGIT_DRAIN_PERIOD_TICKS > 0,
               "digit drain must block for at least one scheduler tick");
_Static_assert(DIGIT_DRAIN_TASK_PRIORITY < configMAX_PRIORITIES,
               "digit drain priority must be a valid FreeRTOS priority");

static const char *TAG = "yourdesk_i2c_ulp";

extern const uint8_t yourdesk_i2c_ulp_bin_start[]
    asm("_binary_yourdesk_i2c_ulp_bin_start");
extern const uint8_t yourdesk_i2c_ulp_bin_end[]
    asm("_binary_yourdesk_i2c_ulp_bin_end");

static QueueHandle_t s_digit_queue;
static QueueHandle_t s_mirror_digit_queue;
static uint32_t s_digit_read_seq;
static uint32_t s_digit_queue_drops;
static uint32_t s_mirror_digit_queue_drops;
static bool s_started;

/** Load one shared word after all earlier ULP writes are visible. */
static inline uint32_t shared_load(const uint32_t *value)
{
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

/** Publish one shared word before the ULP consumes the related state. */
static inline void shared_store(uint32_t *target, uint32_t value)
{
    __atomic_store_n(target, value, __ATOMIC_RELEASE);
}

/** Configure the two existing desk pins as RTCIO before the ULP takes ownership. */
static esp_err_t configure_rtc_bus_gpio(void)
{
    gpio_num_t scl = (gpio_num_t)CONFIG_DESK_I2C_SCL_GPIO;
    gpio_num_t sda = (gpio_num_t)CONFIG_DESK_I2C_SDA_GPIO;
    ESP_RETURN_ON_FALSE(rtc_gpio_is_valid_gpio(scl) &&
                            rtc_gpio_is_valid_gpio(sda),
                        ESP_ERR_INVALID_ARG, TAG,
                        "SCL/SDA must be RTC-capable GPIOs");

    ESP_RETURN_ON_ERROR(rtc_gpio_init(scl), TAG, "initialize CLK RTCIO");
    ESP_RETURN_ON_ERROR(rtc_gpio_set_direction(scl, RTC_GPIO_MODE_INPUT_ONLY),
                        TAG, "configure CLK input");
    ESP_RETURN_ON_ERROR(rtc_gpio_pullup_dis(scl), TAG,
                        "disable CLK internal pull-up");
    ESP_RETURN_ON_ERROR(rtc_gpio_pulldown_dis(scl), TAG,
                        "disable CLK internal pull-down");

    ESP_RETURN_ON_ERROR(rtc_gpio_init(sda), TAG, "initialize DAT RTCIO");
    ESP_RETURN_ON_ERROR(
        rtc_gpio_set_direction(sda, RTC_GPIO_MODE_INPUT_OUTPUT_OD), TAG,
        "configure DAT open-drain");
    /* Release DAT before loading or starting the coprocessor. */
    ESP_RETURN_ON_ERROR(rtc_gpio_set_level(sda, 1), TAG, "release DAT");
    ESP_RETURN_ON_ERROR(rtc_gpio_pullup_dis(sda), TAG,
                        "disable DAT internal pull-up");
    ESP_RETURN_ON_ERROR(rtc_gpio_pulldown_dis(sda), TAG,
                        "disable DAT internal pull-down");
    return ESP_OK;
}

/** Copy ULP ring entries into the existing decoder and optional panel queues. */
static void digit_drain_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t write_seq = shared_load(&ulp_digit_write_seq);
        while (s_digit_read_seq != write_seq) {
            uint32_t packed = shared_load(
                &ulp_digit_ring[s_digit_read_seq & DIGIT_RING_MASK]);
            yourdesk_soft_i2c_digit_event_t event = {
                .ready = true,
                .key_read_completed = false,
                .addr7 = (uint8_t)((packed >> 8) & 0xFFu),
                .segment = (uint8_t)(packed & 0xFFu),
            };
            if (xQueueSend(s_digit_queue, &event, 0) != pdTRUE) {
                s_digit_queue_drops++;
            }
            if (s_mirror_digit_queue &&
                xQueueSend(s_mirror_digit_queue, &event, 0) != pdTRUE) {
                s_mirror_digit_queue_drops++;
            }
            s_digit_read_seq++;
            shared_store(&ulp_digit_read_seq, s_digit_read_seq);
        }
        /* This task only bridges a 32-entry shared ring into FreeRTOS queues.
         * It must block for a real scheduler tick: at CONFIG_FREERTOS_HZ=100,
         * pdMS_TO_TICKS(2) becomes zero and a high-priority vTaskDelay(0) loop
         * starves IDLE0 until the task watchdog fires. */
        vTaskDelay((TickType_t)DIGIT_DRAIN_PERIOD_TICKS);
    }
}

esp_err_t yourdesk_soft_i2c_esp_init(QueueHandle_t digit_queue,
                                    QueueHandle_t mirror_digit_queue,
                                    uint8_t initial_dr)
{
    ESP_RETURN_ON_FALSE(digit_queue, ESP_ERR_INVALID_ARG, TAG,
                        "digit queue is required");
    ESP_RETURN_ON_FALSE(!s_started, ESP_ERR_INVALID_STATE, TAG,
                        "ULP I2C worker already started");
    ESP_RETURN_ON_ERROR(configure_rtc_bus_gpio(), TAG,
                        "configure desk RTCIO");

    size_t binary_size =
        (size_t)(yourdesk_i2c_ulp_bin_end - yourdesk_i2c_ulp_bin_start);
    ESP_RETURN_ON_ERROR(
        ulp_riscv_load_binary(yourdesk_i2c_ulp_bin_start, binary_size), TAG,
        "load ULP I2C worker");

    s_digit_queue = digit_queue;
    s_mirror_digit_queue = mirror_digit_queue;
    s_digit_read_seq = 0;
    s_digit_queue_drops = 0;
    s_mirror_digit_queue_drops = 0;
    shared_store(&ulp_desired_dr, initial_dr);
    shared_store(&ulp_digit_read_seq, 0);
    shared_store(&ulp_worker_ready, 0);

    /* The program never returns; this period only schedules its first start. */
    ulp_set_wakeup_period(0, 1000);
    ESP_RETURN_ON_ERROR(ulp_riscv_run(), TAG, "start ULP I2C worker");

    const TickType_t ready_start_tick = xTaskGetTickCount();
    const TickType_t ready_timeout_ticks =
        pdMS_TO_TICKS(ULP_READY_TIMEOUT_MS);
    while (shared_load(&ulp_worker_ready) == 0 &&
           (xTaskGetTickCount() - ready_start_tick) < ready_timeout_ticks) {
        /* One tick is the minimum blocking delay, independent of tick rate. */
        vTaskDelay(1);
    }
    if (shared_load(&ulp_worker_ready) == 0) {
        ulp_riscv_halt();
        (void)rtc_gpio_set_level((gpio_num_t)CONFIG_DESK_I2C_SDA_GPIO, 1);
        return ESP_ERR_TIMEOUT;
    }

    if (xTaskCreatePinnedToCore(digit_drain_task, "yd_ulp_digit", 3072,
                                NULL, DIGIT_DRAIN_TASK_PRIORITY,
                                NULL, 0) != pdPASS) {
        ulp_riscv_halt();
        (void)rtc_gpio_set_level((gpio_num_t)CONFIG_DESK_I2C_SDA_GPIO, 1);
        return ESP_ERR_NO_MEM;
    }

    s_started = true;
    ESP_LOGI(TAG,
             "ULP multi-address I2C ready addrs=0x24,0x34-0x37 SCL=%d SDA=%d binary=%uB",
             CONFIG_DESK_I2C_SCL_GPIO, CONFIG_DESK_I2C_SDA_GPIO,
             (unsigned)binary_size);
    return ESP_OK;
}

void yourdesk_soft_i2c_esp_set_dr(uint8_t dr)
{
    if (s_started) {
        shared_store(&ulp_desired_dr, dr);
    }
}

void yourdesk_soft_i2c_esp_take_stats(yourdesk_soft_i2c_stats_t *stats)
{
    if (!stats) {
        return;
    }
    *stats = (yourdesk_soft_i2c_stats_t){
        .scl_rising_edges = shared_load(&ulp_stat_scl_rising),
        .scl_falling_edges = shared_load(&ulp_stat_scl_falling),
        .recognized_starts = shared_load(&ulp_stat_starts),
        .recognized_stops = shared_load(&ulp_stat_stops),
        .key_write_addresses = shared_load(&ulp_stat_key_write_addresses),
        .key_read_addresses = shared_load(&ulp_stat_key_read_addresses),
        .completed_key_reads = shared_load(&ulp_stat_completed_key_reads),
        .aborted_key_reads = shared_load(&ulp_stat_aborted_key_reads),
        .digit_write_addresses = shared_load(&ulp_stat_digit_write_addresses),
        .unsupported_addresses = shared_load(&ulp_stat_unsupported_addresses),
        .digit_events = shared_load(&ulp_stat_digit_events),
        .digit_ring_drops = shared_load(&ulp_stat_digit_ring_drops),
        .digit_queue_drops = s_digit_queue_drops,
        .mirror_digit_queue_drops = s_mirror_digit_queue_drops,
        .max_sda_apply_cycles =
            shared_load(&ulp_stat_max_sda_apply_cycles),
        .max_edge_cycles = shared_load(&ulp_stat_max_edge_cycles),
        .edge_deadline_misses =
            shared_load(&ulp_stat_edge_deadline_misses),
        .phase = (uint8_t)shared_load(&ulp_state_phase),
        .bit_count = (uint8_t)shared_load(&ulp_state_bit_count),
        .current_addr7 = (uint8_t)shared_load(&ulp_state_current_addr7),
        .active_dr = (uint8_t)shared_load(&ulp_active_dr),
    };
}
