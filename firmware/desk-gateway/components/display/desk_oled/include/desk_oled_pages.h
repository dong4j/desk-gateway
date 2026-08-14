/**
 * @file desk_oled_pages.h
 * @brief OLED 页面选择与文本布局，保持为可在主机测试的纯 C 模块。
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DESK_OLED_ROW_COUNT 4
#define DESK_OLED_ROW_CHARS 22
#define DESK_OLED_PAGE_INTERVAL_MS 4000U

typedef enum {
    DESK_OLED_PAGE_DISTANCE = 0,
    DESK_OLED_PAGE_STATUS,
    DESK_OLED_PAGE_NETWORK,
    DESK_OLED_PAGE_ALERT,
    DESK_OLED_PAGE_MOTION,
} desk_oled_page_t;

typedef struct {
    int height_mm;
    bool height_known;
    int right_gap_mm;
    bool right_gap_known;
    const char *motion_name;
    bool moving;
    bool child_lock;
    bool wifi_connected;
    bool wifi_ap;
    const char *ip_address;
    size_t ble_connected;
    size_t ble_bonded;
    size_t ble_capacity;
} desk_oled_page_data_t;

typedef struct {
    uint8_t scale;
    uint8_t row_count;
    char rows[DESK_OLED_ROW_COUNT][DESK_OLED_ROW_CHARS];
} desk_oled_frame_t;

/** 运动优先；存在离线传感器时每四个周期插入一次告警页。 */
desk_oled_page_t desk_oled_choose_page(uint32_t elapsed_ms,
                                       bool sensor_offline,
                                       bool moving);

/** 将快照转换成不超过 128x32 屏幕边界的 ASCII 文本行。 */
void desk_oled_build_frame(desk_oled_page_t page,
                           const desk_oled_page_data_t *data,
                           desk_oled_frame_t *out_frame);
