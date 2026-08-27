/**
 * @file desk_motion_audio_logic.c
 * @brief 接近最高高度的三级提示节奏与档位到达判断。
 */
#include "desk_motion_audio_logic.h"

#define CHIME_RANGE_SLOW_MM 150
#define CHIME_RANGE_MEDIUM_MM 80
#define CHIME_RANGE_FAST_MM 30

#define CHIME_INTERVAL_SLOW_MS 1500U
#define CHIME_INTERVAL_MEDIUM_MS 1000U
#define CHIME_INTERVAL_FAST_MS 650U

uint32_t desk_motion_audio_chime_interval_ms(bool moving_up,
                                             bool height_known,
                                             int height_mm,
                                             int max_height_mm)
{
    if (!moving_up || !height_known || max_height_mm <= height_mm) {
        return 0;
    }
    int remaining_mm = max_height_mm - height_mm;
    if (remaining_mm <= CHIME_RANGE_FAST_MM) {
        return CHIME_INTERVAL_FAST_MS;
    }
    if (remaining_mm <= CHIME_RANGE_MEDIUM_MM) {
        return CHIME_INTERVAL_MEDIUM_MS;
    }
    if (remaining_mm <= CHIME_RANGE_SLOW_MM) {
        return CHIME_INTERVAL_SLOW_MS;
    }
    return 0;
}

bool desk_motion_audio_target_reached(bool height_known, int height_mm,
                                      int target_height_mm,
                                      int tolerance_mm)
{
    if (!height_known || target_height_mm < 0 || tolerance_mm < 0) {
        return false;
    }
    int error_mm = height_mm - target_height_mm;
    if (error_mm < 0) {
        error_mm = -error_mm;
    }
    return error_mm <= tolerance_mm;
}
