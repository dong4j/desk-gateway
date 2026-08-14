/**
 * @file yourdesk_panel_display_test.c
 * @brief 验证 ToF 高度到原厂 TM1650 段码的转换。
 */
#include "yourdesk_panel_display.h"

#include <assert.h>
#include <stdio.h>

static void test_two_digit_height(void)
{
    yourdesk_panel_display_frame_t frame;
    assert(yourdesk_panel_display_encode_height(752, &frame));
    assert(frame.height_cm == 75);
    assert(frame.digits[0] == 0x00);
    assert(frame.digits[1] == 0x46);
    assert(frame.digits[2] == 0xD3);
    assert(frame.digits[3] == 0xD3);
}

static void test_three_digit_height(void)
{
    yourdesk_panel_display_frame_t frame;
    assert(yourdesk_panel_display_encode_height(1020, &frame));
    assert(frame.height_cm == 102);
    assert(frame.digits[0] == 0x44);
    assert(frame.digits[1] == 0x5F);
    assert(frame.digits[2] == 0x9E);
    assert(frame.digits[3] == 0x9E);
}

static void test_rounding_and_bounds(void)
{
    yourdesk_panel_display_frame_t frame;
    assert(yourdesk_panel_display_encode_height(1295, &frame));
    assert(frame.height_cm == 130);
    assert(frame.digits[0] == 0x44);
    assert(frame.digits[1] == 0xD6);
    assert(frame.digits[2] == 0x5F);
    assert(!yourdesk_panel_display_encode_height(399, &frame));
    assert(!yourdesk_panel_display_encode_height(2001, &frame));
    assert(!yourdesk_panel_display_encode_height(750, NULL));
}

int main(void)
{
    test_two_digit_height();
    test_three_digit_height();
    test_rounding_and_bounds();
    puts("yourdesk_panel_display_test: ok");
    return 0;
}
