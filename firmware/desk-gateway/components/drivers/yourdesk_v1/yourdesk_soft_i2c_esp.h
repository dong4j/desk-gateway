/**
 * @file yourdesk_soft_i2c_esp.h
 * @brief ESP32-S3 GPIO adapter for the yourdesk_v1 multi-address I2C slave.
 *
 * The adapter owns the timing-sensitive GPIO interrupts and forwards completed
 * digit writes to a task queue. Protocol decisions stay in the host-tested pure
 * state machine.
 */
#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Cumulative, read-only telemetry for controller key polling.
 *
 * These counters diagnose whether the control box completed each 0x24 read;
 * they must never participate in motion decisions or change bus responses.
 */
typedef struct {
    uint32_t recognized_starts;
    uint32_t recognized_stops;
    uint32_t rejected_starts;
    uint32_t rejected_stops;
    uint32_t ignored_sda_edges_while_scl_low;
    uint32_t key_write_addresses;
    uint32_t key_read_addresses;
    uint32_t key_tx_started;
    uint32_t completed_key_reads;
    uint32_t aborted_key_reads;
    uint32_t max_key_read_gap_cycles;
    uint8_t phase;
    uint8_t bit_count;
    uint8_t current_addr7;
    uint8_t tx_dr;
} yourdesk_soft_i2c_stats_t;

/**
 * Configure CLK/DAT and start the software multi-address I2C slave.
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
 * Copy key-poll telemetry and clear only the interval maximum gap.
 *
 * Counters remain cumulative so a task can calculate interval deltas without
 * any ISR logging. The protocol state and configured DR are not modified.
 */
void yourdesk_soft_i2c_esp_take_stats(yourdesk_soft_i2c_stats_t *stats);

#ifdef __cplusplus
}
#endif
