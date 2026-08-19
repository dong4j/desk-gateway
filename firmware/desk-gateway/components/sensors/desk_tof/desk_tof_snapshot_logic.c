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
