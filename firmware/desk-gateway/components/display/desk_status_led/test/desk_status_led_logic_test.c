/**
 * @file desk_status_led_logic_test.c
 * @brief 红黄蓝状态灯语义的主机测试，不依赖 GPIO。
 */
#include "desk_status_led_logic.h"

#include <assert.h>
#include <stdio.h>

static desk_status_led_input_t healthy_idle(void)
{
    return (desk_status_led_input_t){
        .wifi_connected = true,
    };
}

static void assert_leds(desk_status_led_output_t out, bool red, bool yellow,
                        bool blue)
{
    assert(out.red == red);
    assert(out.yellow == yellow);
    assert(out.blue == blue);
}

static void test_idle_sta_all_off(void)
{
    desk_status_led_input_t in = healthy_idle();
    desk_status_led_output_t out = {.red = true, .yellow = true, .blue = true};
    desk_status_led_evaluate(&in, &out);
    assert_leds(out, false, false, false);
}

static void test_red_child_lock_fault_and_upward_block(void)
{
    desk_status_led_input_t in = healthy_idle();
    desk_status_led_output_t out;

    in.child_lock = true;
    desk_status_led_evaluate(&in, &out);
    assert_leds(out, true, false, false);

    in = healthy_idle();
    in.fault = true;
    desk_status_led_evaluate(&in, &out);
    assert_leds(out, true, false, false);

    in = healthy_idle();
    in.upward_blocked = true;
    desk_status_led_evaluate(&in, &out);
    assert_leds(out, true, false, false);
}

static void test_yellow_softap_or_sta_down(void)
{
    desk_status_led_input_t in = {0};
    desk_status_led_output_t out;

    in.wifi_ap = true;
    desk_status_led_evaluate(&in, &out);
    assert_leds(out, false, true, false);

    in = (desk_status_led_input_t){0};
    desk_status_led_evaluate(&in, &out);
    assert_leds(out, false, true, false);
}

static void test_blue_when_moving(void)
{
    desk_status_led_input_t in = healthy_idle();
    desk_status_led_output_t out;
    in.moving = true;
    desk_status_led_evaluate(&in, &out);
    assert_leds(out, false, false, true);
}

static void test_leds_can_combine(void)
{
    desk_status_led_input_t in = {
        .moving = true,
        .upward_blocked = true,
        .wifi_ap = true,
    };
    desk_status_led_output_t out;
    desk_status_led_evaluate(&in, &out);
    assert_leds(out, true, true, true);
}

static void test_null_args_are_safe(void)
{
    desk_status_led_output_t out = {.red = true, .yellow = true, .blue = true};
    desk_status_led_evaluate(NULL, &out);
    assert_leds(out, false, false, false);
    desk_status_led_evaluate(NULL, NULL);
}

int main(void)
{
    test_idle_sta_all_off();
    test_red_child_lock_fault_and_upward_block();
    test_yellow_softap_or_sta_down();
    test_blue_when_moving();
    test_leds_can_combine();
    test_null_args_are_safe();
    puts("desk_status_led_logic_test: ok");
    return 0;
}
