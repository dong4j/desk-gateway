/**
 * @file desk_core.h
 * @brief 统一控制面：方向相关运动保护、童锁、扇出到 desk_driver
 *
 * UART / Web / 未来 BLE 只调这里，禁止直连厂商驱动。
 */
#pragma once

#include "desk_control_policy.h"
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
    int preset1_height_mm;
    int preset4_height_mm;
    uint32_t enabled_sources;
    const char *driver;
} desk_core_snapshot_t;

esp_err_t desk_core_init(const desk_driver_t *drv);
/** STOP 是安全操作，不受童锁或来源开关限制。 */
esp_err_t desk_core_stop(void);
esp_err_t desk_core_hold_up(desk_control_source_t source);
esp_err_t desk_core_hold_down(desk_control_source_t source);
/** Submit an upward rotary event; continuous events start and maintain motion. */
esp_err_t desk_core_jog_up(desk_control_source_t source);
/** Submit a downward rotary event; continuous events start and maintain motion. */
esp_err_t desk_core_jog_down(desk_control_source_t source);
esp_err_t desk_core_goto_preset(desk_control_source_t source, uint8_t n);
esp_err_t desk_core_save_preset(desk_control_source_t source, uint8_t n);
esp_err_t desk_core_set_child_lock(bool enabled);
bool desk_core_get_child_lock(void);
esp_err_t desk_core_set_source_enabled(desk_control_source_t source,
                                       bool enabled);
bool desk_core_get_source_enabled(desk_control_source_t source);
esp_err_t desk_core_set_max_height_mm(int max_height_mm);
int desk_core_get_max_height_mm(void);
/** 两个档位作为一组校验和持久化，避免多入口观察到半更新状态。 */
esp_err_t desk_core_set_preset_heights_mm(int preset1_height_mm,
                                          int preset4_height_mm);
desk_core_snapshot_t desk_core_snapshot(void);

#ifdef __cplusplus
}
#endif
