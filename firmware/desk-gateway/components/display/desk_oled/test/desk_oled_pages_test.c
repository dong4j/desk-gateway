/**
 * @file desk_oled_pages_test.c
 * @brief OLED 页面选择与文本布局的主机测试。
 */
#include "desk_oled_pages.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static desk_oled_page_data_t sample_data(void)
{
    return (desk_oled_page_data_t){
        .height_mm = 752,
        .height_known = true,
        .right_gap_mm = 84,
        .right_gap_known = true,
        .motion_name = "IDLE",
        .wifi_connected = true,
        .ip_address = "192.168.1.8",
        .ble_connected = 1,
        .ble_bonded = 2,
        .ble_capacity = 3,
    };
}

static void test_page_schedule(void)
{
    assert(desk_oled_choose_page(0, false, false) ==
           DESK_OLED_PAGE_DISTANCE);
    assert(desk_oled_choose_page(4000, false, false) ==
           DESK_OLED_PAGE_STATUS);
    assert(desk_oled_choose_page(8000, false, false) ==
           DESK_OLED_PAGE_NETWORK);
    assert(desk_oled_choose_page(0, true, false) == DESK_OLED_PAGE_ALERT);
    assert(desk_oled_choose_page(4000, true, false) ==
           DESK_OLED_PAGE_DISTANCE);
    assert(desk_oled_choose_page(0, true, true) == DESK_OLED_PAGE_MOTION);
}

static void test_distance_and_network_frames(void)
{
    desk_oled_page_data_t data = sample_data();
    desk_oled_frame_t frame;
    desk_oled_build_frame(DESK_OLED_PAGE_DISTANCE, &data, &frame);
    assert(frame.scale == 2);
    assert(strcmp(frame.rows[0], "H 75.2CM") == 0);
    assert(strcmp(frame.rows[1], "R 8.4CM") == 0);

    desk_oled_build_frame(DESK_OLED_PAGE_NETWORK, &data, &frame);
    assert(strcmp(frame.rows[0], "WIFI STA") == 0);
    assert(strcmp(frame.rows[1], "192.168.1.8") == 0);
    assert(strcmp(frame.rows[2], "BLE 1 CONN") == 0);
    assert(strcmp(frame.rows[3], "BOND 2/3") == 0);
}

static void test_offline_frame(void)
{
    desk_oled_page_data_t data = sample_data();
    data.height_known = false;
    desk_oled_frame_t frame;
    desk_oled_build_frame(DESK_OLED_PAGE_ALERT, &data, &frame);
    assert(strcmp(frame.rows[0], "SENSOR OFFLINE") == 0);
    assert(strcmp(frame.rows[1], "TOF050 OK") == 0);
    assert(strcmp(frame.rows[2], "TOF400 OFF") == 0);
}

int main(void)
{
    test_page_schedule();
    test_distance_and_network_frames();
    test_offline_frame();
    puts("desk_oled_pages_test: ok");
    return 0;
}
