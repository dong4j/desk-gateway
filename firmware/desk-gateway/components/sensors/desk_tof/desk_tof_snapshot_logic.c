/**
 * @file desk_tof_snapshot_logic.c
 * @brief 高度快照 seqlock 读端：有限空转、真正阻塞、超时放弃。
 */
#include "desk_tof_snapshot_logic.h"

bool desk_tof_snapshot_seq_readable(uint32_t begin_seq)
{
    return (begin_seq & 1U) == 0U;
}

bool desk_tof_snapshot_seq_consistent(uint32_t begin_seq, uint32_t end_seq)
{
    return begin_seq == end_seq && desk_tof_snapshot_seq_readable(end_seq);
}

desk_tof_snapshot_retry_t desk_tof_snapshot_retry_action(
    unsigned failed_attempts)
{
    if (failed_attempts < DESK_TOF_SNAPSHOT_SPIN_ATTEMPTS) {
        return DESK_TOF_SNAPSHOT_RETRY_SPIN;
    }
    if (failed_attempts < DESK_TOF_SNAPSHOT_SPIN_ATTEMPTS +
                              DESK_TOF_SNAPSHOT_YIELD_ATTEMPTS) {
        return DESK_TOF_SNAPSHOT_RETRY_YIELD;
    }
    return DESK_TOF_SNAPSHOT_RETRY_ABANDON;
}

static void clear_height_view(desk_tof_height_view_t *view)
{
    *view = (desk_tof_height_view_t){
        .height_known = false,
        .height_mm = -1,
        .raw_height_mm = -1,
        .control_height_mm = -1,
        .height_sample_id = 0,
    };
}

void desk_tof_snapshot_resolve_height(bool seqlock_ok,
                                      const desk_tof_height_view_t *fresh,
                                      desk_tof_height_view_t *last_good,
                                      desk_tof_height_view_t *out)
{
    if (!last_good || !out) {
        return;
    }

    if (seqlock_ok && fresh) {
        if (fresh->height_known) {
            *last_good = *fresh;
            *out = *fresh;
            return;
        }
        clear_height_view(last_good);
        clear_height_view(out);
        return;
    }

    if (last_good->height_known) {
        *out = *last_good;
        return;
    }
    clear_height_view(out);
}
