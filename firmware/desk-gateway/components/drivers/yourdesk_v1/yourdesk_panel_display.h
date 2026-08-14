/**
 * @file yourdesk_panel_display.h
 * @brief 将 ToF 实测高度编码为原厂 TM1650 面板段码。
 *
 * 控制盒侧继续只使用稳定的硬件 I2C Slave @0x24，不再尝试同时应答
 * 0x34-0x37。原厂面板的高度显示由独立 ToF 数据生成，避免显示链路影响
 * 升降命令的实时响应。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t digits[4]; /* DIG1、DIG2、DIG3、DIG4 的 TM1650 段码。 */
    int height_cm;
} yourdesk_panel_display_frame_t;

/** 将 400-2000 mm 的高度四舍五入为整数厘米并生成四位段码。 */
bool yourdesk_panel_display_encode_height(
    int height_mm, yourdesk_panel_display_frame_t *out_frame);

#ifdef __cplusplus
}
#endif
