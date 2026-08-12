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

#ifdef __cplusplus
}
#endif
