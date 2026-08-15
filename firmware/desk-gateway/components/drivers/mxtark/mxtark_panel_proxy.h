/**
 * @file mxtark_panel_proxy.h
 * @brief ESP32-S3 master-side adapter for the original TM1650 panel.
 *
 * The control-box bus and panel bus are electrically isolated. This adapter
 * polls the original panel on GPIO6/7 and writes the independent TOF400C
 * height back to its display without disturbing the controller-side I2C bus.
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Called from panel task context when key or connection state changes. */
typedef void (*mxtark_panel_key_callback_t)(bool connected, uint8_t dr,
                                               void *ctx);

/**
 * Start the panel-side open-drain software I2C master and proxy task.
 *
 * The callback context must remain valid for the application lifetime. The
 * task treats every NACK or timeout as an immediate idle key sample.
 */
esp_err_t mxtark_panel_proxy_init(mxtark_panel_key_callback_t key_callback,
                                    void *callback_ctx);

#ifdef __cplusplus
}
#endif
