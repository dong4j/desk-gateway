/**
 * @file desk_oled_pages.c
 * @brief OLED 页面轮播与固定宽度文本格式化。
 */
#include "desk_oled_pages.h"

#include <stdio.h>
#include <string.h>

static void format_distance(char *out, size_t len, char prefix,
                            bool known, int millimetres)
{
    if (!known || millimetres < 0) {
        snprintf(out, len, "%c --.-CM", prefix);
        return;
    }
    snprintf(out, len, "%c %d.%dCM", prefix, millimetres / 10,
             millimetres % 10);
}

desk_oled_page_t desk_oled_choose_page(uint32_t elapsed_ms,
                                       bool sensor_offline,
                                       bool moving)
{
    if (moving) {
        return DESK_OLED_PAGE_MOTION;
    }
    uint32_t slot = elapsed_ms / DESK_OLED_PAGE_INTERVAL_MS;
    if (sensor_offline) {
        slot %= 4U;
        if (slot == 0U) {
            return DESK_OLED_PAGE_ALERT;
        }
        slot--;
    } else {
        slot %= 3U;
    }
    return (desk_oled_page_t)(DESK_OLED_PAGE_DISTANCE + slot);
}

void desk_oled_build_frame(desk_oled_page_t page,
                           const desk_oled_page_data_t *data,
                           desk_oled_frame_t *out_frame)
{
    if (!data || !out_frame) {
        return;
    }
    memset(out_frame, 0, sizeof(*out_frame));
    out_frame->scale = 1;
    out_frame->row_count = 4;

    switch (page) {
    case DESK_OLED_PAGE_DISTANCE:
        out_frame->scale = 2;
        out_frame->row_count = 2;
        format_distance(out_frame->rows[0], DESK_OLED_ROW_CHARS, 'H',
                        data->height_known, data->height_mm);
        format_distance(out_frame->rows[1], DESK_OLED_ROW_CHARS, 'R',
                        data->right_gap_known, data->right_gap_mm);
        break;
    case DESK_OLED_PAGE_STATUS:
        snprintf(out_frame->rows[0], DESK_OLED_ROW_CHARS, "STATE %s",
                 data->motion_name ? data->motion_name : "UNKNOWN");
        snprintf(out_frame->rows[1], DESK_OLED_ROW_CHARS, "TOF050 %s",
                 data->right_gap_known ? "OK" : "OFF");
        snprintf(out_frame->rows[2], DESK_OLED_ROW_CHARS, "TOF400 %s",
                 data->height_known ? "OK" : "OFF");
        snprintf(out_frame->rows[3], DESK_OLED_ROW_CHARS, "LOCK %s",
                 data->child_lock ? "ON" : "OFF");
        break;
    case DESK_OLED_PAGE_NETWORK:
        snprintf(out_frame->rows[0], DESK_OLED_ROW_CHARS, "WIFI %s",
                 data->wifi_ap ? "AP" : data->wifi_connected ? "STA" : "OFF");
        snprintf(out_frame->rows[1], DESK_OLED_ROW_CHARS, "%s",
                 data->ip_address ? data->ip_address : "NO IP");
        snprintf(out_frame->rows[2], DESK_OLED_ROW_CHARS, "BLE %u CONN",
                 (unsigned)data->ble_connected);
        snprintf(out_frame->rows[3], DESK_OLED_ROW_CHARS, "BOND %u/%u",
                 (unsigned)data->ble_bonded, (unsigned)data->ble_capacity);
        break;
    case DESK_OLED_PAGE_ALERT:
        snprintf(out_frame->rows[0], DESK_OLED_ROW_CHARS, "SENSOR OFFLINE");
        snprintf(out_frame->rows[1], DESK_OLED_ROW_CHARS, "TOF050 %s",
                 data->right_gap_known ? "OK" : "OFF");
        snprintf(out_frame->rows[2], DESK_OLED_ROW_CHARS, "TOF400 %s",
                 data->height_known ? "OK" : "OFF");
        snprintf(out_frame->rows[3], DESK_OLED_ROW_CHARS, "CHECK POWER");
        break;
    case DESK_OLED_PAGE_MOTION:
        out_frame->scale = 2;
        out_frame->row_count = 2;
        snprintf(out_frame->rows[0], DESK_OLED_ROW_CHARS, "%s",
                 data->motion_name ? data->motion_name : "MOVE");
        format_distance(out_frame->rows[1], DESK_OLED_ROW_CHARS, 'H',
                        data->height_known, data->height_mm);
        break;
    default:
        snprintf(out_frame->rows[0], DESK_OLED_ROW_CHARS, "DESK GATEWAY");
        break;
    }
}
