/**
 * @file desk_driver.h
 * @brief 厂商无关 Desk Driver 契约
 *
 * 所有控桌入口只经 desk_core 调用本接口；新增厂商 = 实现一张 ops 表。
 * 未验证的协议动作必须返回 ESP_ERR_NOT_SUPPORTED，禁止瞎发。
 */
#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DESK_STATUS_IDLE = 0,
    DESK_STATUS_MOVING_UP,
    DESK_STATUS_MOVING_DOWN,
    DESK_STATUS_GOTO_PRESET,
    DESK_STATUS_ERROR,
} desk_status_t;

typedef struct {
    bool hold_up_down;
    bool preset_goto;
    bool preset_save;
    bool height;
    /** bit0=preset1 … bit3=preset4 */
    uint8_t preset_mask;
} desk_caps_t;

typedef struct desk_driver {
    const char *name;
    esp_err_t (*init)(void);
    esp_err_t (*deinit)(void);
    esp_err_t (*stop)(void);
    esp_err_t (*hold_up)(void);
    esp_err_t (*hold_down)(void);
    /** 1-based preset index */
    esp_err_t (*goto_preset)(uint8_t n);
    esp_err_t (*save_preset)(uint8_t n);
    /** 不支持高度时返回 ESP_ERR_NOT_SUPPORTED */
    esp_err_t (*get_height_mm)(int *out_mm);
    desk_status_t (*get_status)(void);
    desk_caps_t (*get_caps)(void);
} desk_driver_t;

/**
 * @brief 注册并激活驱动（会 init；若已有活跃驱动先 deinit）。
 */
esp_err_t desk_driver_register(const desk_driver_t *drv);

/** 当前活跃驱动；未注册时为 NULL */
const desk_driver_t *desk_driver_get_active(void);

#ifdef __cplusplus
}
#endif
