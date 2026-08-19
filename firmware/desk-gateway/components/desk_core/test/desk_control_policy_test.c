/**
 * @file desk_control_policy_test.c
 * @brief 童锁最高优先级和来源开关的宿主机回归测试
 */
#include "desk_control_policy.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/** 童锁必须覆盖所有已启用来源。 */
static void test_child_lock_denies_every_source(void)
{
    desk_control_policy_t policy = {
        .child_lock = true,
        .enabled_sources =
            DESK_CONTROL_SOURCE_BIT(DESK_CONTROL_SOURCE_COUNT) - 1u,
    };
    for (int source = DESK_CONTROL_SOURCE_REST;
         source < DESK_CONTROL_SOURCE_COUNT; source++) {
        assert(!desk_control_policy_allows(
            &policy, (desk_control_source_t)source));
    }
}

/** 默认允许远程入口，但原厂面板按键必须保持关闭。 */
static void test_default_sources_lock_original_panel(void)
{
    desk_control_policy_t policy = {
        .child_lock = false,
        .enabled_sources = DESK_CONTROL_SOURCE_DEFAULT_MASK,
    };
    assert(desk_control_policy_allows(&policy, DESK_CONTROL_SOURCE_REST));
    assert(desk_control_policy_allows(&policy,
                                      DESK_CONTROL_SOURCE_BLUETOOTH));
    assert(!desk_control_policy_allows(&policy, DESK_CONTROL_SOURCE_PANEL));
    assert(desk_control_policy_allows(&policy, DESK_CONTROL_SOURCE_CONSOLE));
    assert(!desk_control_policy_allows(&policy, DESK_CONTROL_SOURCE_MQTT));
}

/** MQTT 必须追加在枚举末尾，旧 NVS bit 0..3 不能被挤动。 */
static void test_mqtt_source_appends_without_shifting_old_bits(void)
{
    assert(DESK_CONTROL_SOURCE_REST == 0);
    assert(DESK_CONTROL_SOURCE_BLUETOOTH == 1);
    assert(DESK_CONTROL_SOURCE_PANEL == 2);
    assert(DESK_CONTROL_SOURCE_CONSOLE == 3);
    assert(DESK_CONTROL_SOURCE_MQTT == 4);
    assert(DESK_CONTROL_SOURCE_BIT(DESK_CONTROL_SOURCE_MQTT) == UINT32_C(16));
    assert((DESK_CONTROL_SOURCE_DEFAULT_MASK &
            DESK_CONTROL_SOURCE_BIT(DESK_CONTROL_SOURCE_MQTT)) == 0);
    assert((DESK_CONTROL_SOURCE_CONFIGURABLE_MASK &
            DESK_CONTROL_SOURCE_BIT(DESK_CONTROL_SOURCE_MQTT)) != 0);
    assert(strcmp(desk_control_source_name(DESK_CONTROL_SOURCE_MQTT),
                  "mqtt") == 0);
}

/** 解锁后只允许启用的来源，不能因其他来源启用而旁路。 */
static void test_source_switches_are_independent(void)
{
    desk_control_policy_t policy = {
        .child_lock = false,
        .enabled_sources = DESK_CONTROL_SOURCE_BIT(DESK_CONTROL_SOURCE_REST),
    };
    assert(desk_control_policy_allows(&policy, DESK_CONTROL_SOURCE_REST));
    assert(!desk_control_policy_allows(&policy,
                                       DESK_CONTROL_SOURCE_BLUETOOTH));
    assert(!desk_control_policy_allows(&policy, DESK_CONTROL_SOURCE_PANEL));
}

/** 非法来源和空策略必须安全拒绝。 */
static void test_invalid_policy_fails_closed(void)
{
    desk_control_policy_t policy = {
        .child_lock = false,
        .enabled_sources = DESK_CONTROL_SOURCE_DEFAULT_MASK,
    };
    assert(!desk_control_policy_allows(NULL, DESK_CONTROL_SOURCE_REST));
    assert(!desk_control_policy_allows(
        &policy, (desk_control_source_t)DESK_CONTROL_SOURCE_COUNT));
}

int main(void)
{
    test_child_lock_denies_every_source();
    test_default_sources_lock_original_panel();
    test_mqtt_source_appends_without_shifting_old_bits();
    test_source_switches_are_independent();
    test_invalid_policy_fails_closed();
    puts("desk control policy vectors: OK");
    return 0;
}
