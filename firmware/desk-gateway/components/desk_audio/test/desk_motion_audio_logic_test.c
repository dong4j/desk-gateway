/**
 * @file desk_motion_audio_logic_test.c
 * @brief 变频提示边界和档位完成容差的 Host tests。
 */
#include "desk_motion_audio_logic.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(desk_motion_audio_chime_interval_ms(true, true, 749, 900) == 0);
    assert(desk_motion_audio_chime_interval_ms(true, true, 750, 900) == 1500);
    assert(desk_motion_audio_chime_interval_ms(true, true, 820, 900) == 1000);
    assert(desk_motion_audio_chime_interval_ms(true, true, 870, 900) == 650);
    assert(desk_motion_audio_chime_interval_ms(true, true, 900, 900) == 0);
    assert(desk_motion_audio_chime_interval_ms(false, true, 870, 900) == 0);
    assert(desk_motion_audio_chime_interval_ms(true, false, -1, 900) == 0);

    assert(desk_motion_audio_target_reached(true, 879, 870, 10));
    assert(!desk_motion_audio_target_reached(true, 881, 870, 10));
    assert(!desk_motion_audio_target_reached(false, -1, 870, 10));

    puts("desk_motion_audio_logic_test: ok");
    return 0;
}
