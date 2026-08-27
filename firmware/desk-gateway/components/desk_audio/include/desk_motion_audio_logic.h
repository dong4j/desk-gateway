/**
 * @file desk_motion_audio_logic.h
 * @brief 运动提示节奏的纯逻辑接口，供固件和 Host tests 共用。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 返回当前接近上限提示音间隔；0 表示此刻不应播放。
 *
 * 只使用当前配置的最高高度，避免把 90 cm 再写成第二套固定上限。
 */
uint32_t desk_motion_audio_chime_interval_ms(bool moving_up,
                                             bool height_known,
                                             int height_mm,
                                             int max_height_mm);

/** 判断档位停车高度是否已进入允许播报“已到达”的容差。 */
bool desk_motion_audio_target_reached(bool height_known, int height_mm,
                                      int target_height_mm,
                                      int tolerance_mm);

#ifdef __cplusplus
}
#endif
