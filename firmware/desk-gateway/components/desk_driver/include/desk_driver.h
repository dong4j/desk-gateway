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

/* Product control values use the filtered TOF400C distance without calibration. */
#define DESK_MAX_HEIGHT_MM_MIN             560
#define DESK_MAX_HEIGHT_MM_MAX             940
#define DESK_PRESET1_HEIGHT_MM_DEFAULT     560
#define DESK_PRESET4_HEIGHT_MM_DEFAULT     870

typedef struct {
    bool hold_up_down;
    /** 由驱动在本地真实高度链路中停止，不能依赖调用方补发 STOP。 */
    bool raise_to_max;
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
    /**
     * 有界上升到已配置的最高安全高度。
     *
     * 支持该动作的驱动必须使用真实高度在设备侧持续判断并自动停止；
     * 高度未知或安全链路不可用时返回错误，禁止退化成普通 hold_up。
     */
    esp_err_t (*raise_to_max)(void);
    /**
     * 可选的控制盒故障重置入口。
     *
     * 驱动只负责开始输出已验证的厂商协议码；持续时间和最终 STOP 由
     * desk_core 统一管理，避免 HTTP 连接中断后重置码永久保持。
     */
    esp_err_t (*reset_controller)(void);
    /** 1-based preset index */
    esp_err_t (*goto_preset)(uint8_t n);
    esp_err_t (*save_preset)(uint8_t n);
    /** 不支持高度时返回 ESP_ERR_NOT_SUPPORTED */
    esp_err_t (*get_height_mm)(int *out_mm);
    /** 驱动必须在真实高度事件路径中执行该上限，而不能依赖 Web 轮询。 */
    esp_err_t (*set_max_height_mm)(int max_height_mm);
    /** 配置网关侧档位目标；驱动的 goto_preset 必须读取这里下发的值。 */
    esp_err_t (*set_preset_heights_mm)(int preset1_height_mm,
                                       int preset4_height_mm);
    /** 预测或真实触顶后，在重新获得安全下降高度前禁止继续上升。 */
    bool (*is_upward_blocked)(void);
    /**
     * 可选的原厂控制面板入口开关。
     *
     * 禁用后驱动必须立即释放面板控制权；重新启用时必须等待物理按键
     * 先松开，禁止把锁定期间一直按住的按键直接恢复为运动。
     */
    esp_err_t (*set_panel_enabled)(bool enabled);
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
