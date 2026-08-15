/**
 * @file desk_auto_lock.h
 * @brief 单一可信设备驱动的自动童锁纯状态机。
 *
 * 本模块不访问 NVS、BLE、REST 或桌体驱动，只根据选中设备的在线信号产出
 * 锁定/解锁动作，便于在主机环境验证手动锁定不会被自动解除。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DESK_AUTO_LOCK_DEVICE_ID_LENGTH 18
#define DESK_AUTO_LOCK_AWAY_TIMEOUT_MS UINT32_C(180000)

typedef enum {
    DESK_CHILD_LOCK_REASON_NONE = 0,
    DESK_CHILD_LOCK_REASON_MANUAL = 1,
    DESK_CHILD_LOCK_REASON_AUTO_AWAY = 2,
} desk_child_lock_reason_t;

typedef enum {
    DESK_AUTO_LOCK_ACTION_NONE = 0,
    DESK_AUTO_LOCK_ACTION_LOCK,
    DESK_AUTO_LOCK_ACTION_UNLOCK,
} desk_auto_lock_action_t;

typedef struct {
    bool enabled;
    char selected_device_id[DESK_AUTO_LOCK_DEVICE_ID_LENGTH];
    desk_child_lock_reason_t lock_reason;
    bool ble_present;
    bool recent_presence;
    uint32_t away_deadline_ms;
} desk_auto_lock_state_t;

/** 只接受现有 Bond 管理公开的 `bond_<12位小写十六进制>`。 */
bool desk_auto_lock_device_id_valid(const char *device_id);

/** 从持久化配置恢复；旧版本缺少来源时，已锁定状态按手动锁定处理。 */
void desk_auto_lock_init(desk_auto_lock_state_t *state, bool enabled,
                         const char *selected_device_id, bool child_locked,
                         desk_child_lock_reason_t persisted_reason,
                         uint32_t now_ms);

/** 更新唯一检测设备；关闭时允许保留设备 ID，便于以后重新启用。 */
bool desk_auto_lock_configure(desk_auto_lock_state_t *state, bool enabled,
                              const char *selected_device_id,
                              uint32_t now_ms);

/** 选中设备的局域网心跳；其他设备 ID 被安全忽略。 */
desk_auto_lock_action_t desk_auto_lock_heartbeat(
    desk_auto_lock_state_t *state, const char *device_id, uint32_t now_ms);

/** 选中设备的加密 BLE 会话变化；在线期间不会因 REST 心跳暂停而误锁。 */
desk_auto_lock_action_t desk_auto_lock_set_ble_presence(
    desk_auto_lock_state_t *state, const char *device_id, bool present,
    uint32_t now_ms);

/** 周期检查离家截止时间。 */
desk_auto_lock_action_t desk_auto_lock_tick(desk_auto_lock_state_t *state,
                                            uint32_t now_ms);

/** 在实际童锁写入成功后同步来源。 */
void desk_auto_lock_record_lock_state(desk_auto_lock_state_t *state,
                                      bool locked,
                                      desk_child_lock_reason_t reason);

bool desk_auto_lock_detector_online(const desk_auto_lock_state_t *state,
                                    uint32_t now_ms);
const char *desk_child_lock_reason_name(desk_child_lock_reason_t reason);

#ifdef __cplusplus
}
#endif
