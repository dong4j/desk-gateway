/**
 * @file desk_core.h
 * @brief 统一控制面：超时、童锁、扇出到 desk_driver
 *
 * UART / Web / 未来 BLE 只调这里，禁止直连厂商驱动。
 */
#pragma once

#include "desk_driver.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    desk_status_t status;
    int height_mm; /* -1 = unknown */
    bool height_known;
    bool height_sim; /* true = 本地模拟，非嗅探真值 */
    bool child_lock;
    bool upward_blocked;
    int max_height_mm;
    const char *driver;
} desk_core_snapshot_t;

esp_err_t desk_core_init(const desk_driver_t *drv);
esp_err_t desk_core_stop(void);
esp_err_t desk_core_hold_up(void);
esp_err_t desk_core_hold_down(void);
/** Submit an upward rotary event; continuous events start and maintain motion. */
esp_err_t desk_core_jog_up(void);
/** Submit a downward rotary event; continuous events start and maintain motion. */
esp_err_t desk_core_jog_down(void);
esp_err_t desk_core_goto_preset(uint8_t n);
esp_err_t desk_core_save_preset(uint8_t n);
esp_err_t desk_core_set_child_lock(bool enabled);
bool desk_core_get_child_lock(void);
esp_err_t desk_core_set_max_height_mm(int max_height_mm);
int desk_core_get_max_height_mm(void);
desk_core_snapshot_t desk_core_snapshot(void);

#ifdef __cplusplus
}
#endif
