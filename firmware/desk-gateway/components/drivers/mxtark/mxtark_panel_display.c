/**
 * @file mxtark_panel_display.c
 * @brief 原厂 TM1650 面板整数厘米显示编码器。
 */
#include "mxtark_panel_display.h"

#include <stddef.h>

#define PANEL_HEIGHT_MIN_MM 400
#define PANEL_HEIGHT_MAX_MM 2000

/* 段码来自 captures/ 中已验证的原厂高度写入，不使用通用七段管字库。 */
static const uint8_t s_digit_segments[10] = {
    0x5F, 0x44, 0x9E, 0xD6, 0xC5,
    0xD3, 0xDB, 0x46, 0xDF, 0xD7,
};

bool mxtark_panel_display_encode_height(
    int height_mm, mxtark_panel_display_frame_t *out_frame)
{
    if (!out_frame || height_mm < PANEL_HEIGHT_MIN_MM ||
        height_mm > PANEL_HEIGHT_MAX_MM) {
        return false;
    }

    int height_cm = (height_mm + 5) / 10;
    int hundreds = height_cm / 100;
    int tens = (height_cm / 10) % 10;
    int ones = height_cm % 10;
    out_frame->digits[0] = hundreds == 0 ? 0x00 : s_digit_segments[hundreds];
    out_frame->digits[1] = s_digit_segments[tens];
    out_frame->digits[2] = s_digit_segments[ones];
    /* 实测 DIG4 镜像个位；保持这个行为避免改变原厂扫描节奏。 */
    out_frame->digits[3] = out_frame->digits[2];
    out_frame->height_cm = height_cm;
    return true;
}
