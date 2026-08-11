/**
 * @file desk_control_policy.h
 * @brief 升降桌控制来源与童锁优先级策略
 *
 * 所有会驱动桌体运动的入口都必须声明来源，并经过同一份策略判断。
 * STOP 不走本策略，由 desk_core 始终放行，确保任何状态下都能安全停车。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 控制入口标识。
 *
 * 后续接入 HA、MQTT 等入口时只需在末尾追加枚举值，并让新入口调用带
 * source 参数的 desk_core API，禁止直接调用 desk_driver。
 */
typedef enum {
    DESK_CONTROL_SOURCE_REST = 0,
    DESK_CONTROL_SOURCE_BLUETOOTH,
    DESK_CONTROL_SOURCE_PANEL,
    DESK_CONTROL_SOURCE_CONSOLE,
    DESK_CONTROL_SOURCE_COUNT,
} desk_control_source_t;

#define DESK_CONTROL_SOURCE_BIT(source) (UINT32_C(1) << (uint32_t)(source))

#define DESK_CONTROL_SOURCE_CONFIGURABLE_MASK \
    (DESK_CONTROL_SOURCE_BIT(DESK_CONTROL_SOURCE_REST) | \
     DESK_CONTROL_SOURCE_BIT(DESK_CONTROL_SOURCE_BLUETOOTH) | \
     DESK_CONTROL_SOURCE_BIT(DESK_CONTROL_SOURCE_PANEL))

#define DESK_CONTROL_SOURCE_DEFAULT_MASK \
    (DESK_CONTROL_SOURCE_CONFIGURABLE_MASK | \
     DESK_CONTROL_SOURCE_BIT(DESK_CONTROL_SOURCE_CONSOLE))

typedef struct {
    bool child_lock;
    uint32_t enabled_sources;
} desk_control_policy_t;

/** 判断来源枚举是否有效。 */
bool desk_control_source_is_valid(desk_control_source_t source);

/** 返回用于日志和 API 的稳定来源名称。 */
const char *desk_control_source_name(desk_control_source_t source);

/** 判断来源自身是否启用，不考虑童锁。 */
bool desk_control_policy_source_enabled(const desk_control_policy_t *policy,
                                        desk_control_source_t source);

/**
 * 最终授权判断：童锁优先于所有来源开关。
 *
 * 该函数只判断启动或维持运动的命令；STOP 必须由上层始终放行。
 */
bool desk_control_policy_allows(const desk_control_policy_t *policy,
                                desk_control_source_t source);

#ifdef __cplusplus
}
#endif
