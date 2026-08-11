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

typedef struct {
    uint32_t completed_key_reads;
    uint32_t last_key_read_ms;
    uint32_t max_key_read_gap_ms;
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
 * Copy controller-poll telemetry and clear the accumulated maximum gap.
 *
 * The 0x24 bus normally completes one key read about every 3.7 ms. The caller
 * may sample this structure slowly; no per-poll logging is performed.
 */
void yourdesk_soft_i2c_esp_take_stats(yourdesk_soft_i2c_stats_t *stats);

#ifdef __cplusplus
}
#endif
