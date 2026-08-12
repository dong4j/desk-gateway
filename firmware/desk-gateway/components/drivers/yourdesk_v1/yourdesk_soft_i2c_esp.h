/**
 * @file yourdesk_soft_i2c_esp.h
 * @brief ESP32-S3 ULP adapter for the yourdesk_v1 multi-address I2C slave.
 *
 * The independent ULP RISC-V core owns the timing-sensitive RTCIO polling and
 * forwards completed digit writes through shared memory. Protocol decisions
 * stay in the host-tested pure state machine.
 */
#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t scl_rising_edges;
    uint32_t scl_falling_edges;
    uint32_t recognized_starts;
    uint32_t recognized_stops;
    uint32_t key_write_addresses;
    uint32_t key_read_addresses;
    uint32_t completed_key_reads;
    uint32_t aborted_key_reads;
    uint32_t digit_write_addresses;
    uint32_t unsupported_addresses;
    uint32_t digit_events;
    uint32_t digit_ring_drops;
    uint32_t digit_queue_drops;
    uint32_t mirror_digit_queue_drops;
    uint32_t max_sda_apply_cycles;
    uint32_t max_edge_cycles;
    uint32_t edge_deadline_misses;
    uint8_t phase;
    uint8_t bit_count;
    uint8_t current_addr7;
    uint8_t active_dr;
} yourdesk_soft_i2c_stats_t;

/**
 * Configure RTCIO CLK/DAT and start the ULP multi-address I2C slave.
 *
 * digit_queue must accept yourdesk_soft_i2c_digit_event_t values and remain
 * valid for the adapter lifetime. mirror_digit_queue may be NULL; when set it
 * receives the same events for asynchronous forwarding to the original panel.
 */
esp_err_t yourdesk_soft_i2c_esp_init(QueueHandle_t digit_queue,
                                    QueueHandle_t mirror_digit_queue,
                                    uint8_t initial_dr);

/** Update the key byte returned by the next 0x24 read transaction. */
void yourdesk_soft_i2c_esp_set_dr(uint8_t dr);

/**
 * Copy cumulative controller-poll telemetry.
 *
 * The 0x24 bus normally completes one key read about every 3.7 ms. The caller
 * may sample this structure slowly because the ULP counters reside in shared
 * RTC memory and the timing worker never waits for the main CPU.
 */
void yourdesk_soft_i2c_esp_take_stats(yourdesk_soft_i2c_stats_t *stats);

#ifdef __cplusplus
}
#endif
