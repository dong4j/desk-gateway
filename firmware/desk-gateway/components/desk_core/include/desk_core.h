/**
 * @file desk_core.h
 * @brief 统一控制面：方向相关运动保护、童锁、扇出到 desk_driver
 *
 * UART / Web / 未来 BLE 只调这里，禁止直连厂商驱动。
 */
#pragma once

#include "desk_control_policy.h"
#include "desk_height_presets.h"
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
    bool raise_to_max_supported;
    bool controller_reset_supported;
    bool controller_reset_active;
    bool controller_reset_recommended;
    int max_height_mm;
    int preset1_height_mm;
    int preset4_height_mm;
    uint32_t enabled_sources;
    const char *driver;
} desk_core_snapshot_t;

#define DESK_CORE_HEIGHT_PRESET_CAPACITY \
    (DESK_HEIGHT_PRESET_CUSTOM_CAPACITY + 2)

typedef struct {
    char id[DESK_HEIGHT_PRESET_ID_BUFFER_LENGTH];
    char name[DESK_HEIGHT_PRESET_NAME_BUFFER_LENGTH];
    int height_mm;
    bool built_in;
    bool deletable;
} desk_core_height_preset_t;

typedef struct {
    desk_core_height_preset_t presets[DESK_CORE_HEIGHT_PRESET_CAPACITY];
    size_t count;
    size_t custom_capacity;
} desk_core_height_preset_snapshot_t;

typedef enum {
    DESK_CORE_EVENT_STOP_ACCEPTED = 0,
    DESK_CORE_EVENT_MOTION_ACCEPTED,
} desk_core_event_kind_t;

typedef struct {
    desk_core_event_kind_t kind;
    desk_control_source_t source;
} desk_core_event_t;

/**
 * desk_core 接受命令后的轻量通知。
 *
 * 回调可能来自 REST、面板、BLE 或定时器所在任务，只能做无阻塞转发；
 * 不允许在回调中再次调用 desk_core，避免控制链递归。
 */
typedef void (*desk_core_event_listener_t)(const desk_core_event_t *event,
                                           void *context);

esp_err_t desk_core_init(const desk_driver_t *drv);
/** 启动阶段注册单一观察者；传 NULL 可取消注册。 */
void desk_core_set_event_listener(desk_core_event_listener_t listener,
                                  void *context);
/** STOP 是安全操作，不受童锁或来源开关限制。 */
esp_err_t desk_core_stop(void);
esp_err_t desk_core_hold_up(desk_control_source_t source);
esp_err_t desk_core_hold_down(desk_control_source_t source);
/** 使用驱动本地真实高度闭环上升到最高安全高度。 */
esp_err_t desk_core_raise_to_max(desk_control_source_t source);
/**
 * 启动厂商控制盒故障重置；固件负责定时结束，STOP 可随时中断。
 */
esp_err_t desk_core_reset_controller(desk_control_source_t source);
/** Submit an upward rotary event; continuous events start and maintain motion. */
esp_err_t desk_core_jog_up(desk_control_source_t source);
/** Submit a downward rotary event; continuous events start and maintain motion. */
esp_err_t desk_core_jog_down(desk_control_source_t source);
esp_err_t desk_core_goto_preset(desk_control_source_t source, uint8_t n);
/** 按稳定档位 ID 启动设备侧闭环运动，支持内置与自定义档位。 */
esp_err_t desk_core_goto_height_preset(desk_control_source_t source,
                                       const char *id);
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
void desk_core_get_height_presets(desk_core_height_preset_snapshot_t *out);
esp_err_t desk_core_create_height_preset(const char *name, int height_mm,
                                         char *out_id, size_t out_id_size);
esp_err_t desk_core_update_height_preset(const char *id, const char *name,
                                         int height_mm);
esp_err_t desk_core_delete_height_preset(const char *id);
desk_core_snapshot_t desk_core_snapshot(void);

#ifdef __cplusplus
}
#endif
