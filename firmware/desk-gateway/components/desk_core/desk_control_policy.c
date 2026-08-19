/**
 * @file desk_control_policy.c
 * @brief 与硬件无关、可在宿主机测试的控制权限判断
 */
#include "desk_control_policy.h"

#include <stddef.h>

bool desk_control_source_is_valid(desk_control_source_t source)
{
    return source >= DESK_CONTROL_SOURCE_REST &&
           source < DESK_CONTROL_SOURCE_COUNT;
}

const char *desk_control_source_name(desk_control_source_t source)
{
    switch (source) {
    case DESK_CONTROL_SOURCE_REST:
        return "rest";
    case DESK_CONTROL_SOURCE_BLUETOOTH:
        return "bluetooth";
    case DESK_CONTROL_SOURCE_PANEL:
        return "panel";
    case DESK_CONTROL_SOURCE_CONSOLE:
        return "console";
    case DESK_CONTROL_SOURCE_MQTT:
        return "mqtt";
    case DESK_CONTROL_SOURCE_COUNT:
    default:
        return "unknown";
    }
}

bool desk_control_policy_source_enabled(const desk_control_policy_t *policy,
                                        desk_control_source_t source)
{
    if (!policy || !desk_control_source_is_valid(source)) {
        return false;
    }
    return (policy->enabled_sources & DESK_CONTROL_SOURCE_BIT(source)) != 0;
}

bool desk_control_policy_allows(const desk_control_policy_t *policy,
                                desk_control_source_t source)
{
    return policy && !policy->child_lock &&
           desk_control_policy_source_enabled(policy, source);
}
