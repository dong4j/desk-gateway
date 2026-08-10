/**
 * @file yourdesk_panel_proxy.h
 * @brief ESP32-S3 master-side adapter for the original TM1650 panel.
 *
 * The control-box bus and panel bus are electrically isolated. This adapter
 * polls the original panel on GPIO6/7 and mirrors controller digit writes so
 * the panel behaves as if it were still connected directly to the desk.
 */
#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Called from panel task context when key or connection state changes. */
typedef void (*yourdesk_panel_key_callback_t)(bool connected, uint8_t dr,
                                               void *ctx);

/**
 * Start the panel-side I2C master and proxy task.
 *
 * digit_queue contains controller-originated yourdesk_soft_i2c_digit_event_t
 * values. The queue and callback context must remain valid for the application
 * lifetime.
 */
esp_err_t yourdesk_panel_proxy_init(QueueHandle_t digit_queue,
                                    yourdesk_panel_key_callback_t key_callback,
                                    void *callback_ctx);

#ifdef __cplusplus
}
#endif
