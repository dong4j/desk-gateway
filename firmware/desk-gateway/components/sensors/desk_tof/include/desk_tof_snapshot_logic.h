/**
 * @file desk_tof_snapshot_logic.h
 * @brief 高度快照 seqlock 的有界读端策略。
 *
 * 高优先级安全任务不能在写端的奇数序号上无限忙等，否则低优先级
 * desk_tof 任务可能永远无法完成发布，最高高度 STOP 也就无法执行。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 正常发布窗口很短，先有限空转，避免每次读取都触发任务切换。 */
#define DESK_TOF_SNAPSHOT_SPIN_ATTEMPTS 8U
/**
 * 空转用尽后必须用 vTaskDelay(1) 真正阻塞高优先级读端。
 * taskYIELD() 不会把 CPU 交给优先级 5 的传感器写任务。
 */
#define DESK_TOF_SNAPSHOT_YIELD_ATTEMPTS 3U

typedef enum {
    DESK_TOF_SNAPSHOT_RETRY_SPIN = 0,
    DESK_TOF_SNAPSHOT_RETRY_YIELD,
    DESK_TOF_SNAPSHOT_RETRY_ABANDON,
} desk_tof_snapshot_retry_t;

/** 偶数序号表示写端当前没有未完成的发布。 */
bool desk_tof_snapshot_seq_readable(uint32_t begin_seq);

/** 首尾相同且均为偶数时，快照才是一组完整数据。 */
bool desk_tof_snapshot_seq_consistent(uint32_t begin_seq, uint32_t end_seq);

/** 根据本次读取的失败次数选择空转、阻塞或失败关闭。 */
desk_tof_snapshot_retry_t desk_tof_snapshot_retry_action(
    unsigned failed_attempts);

#ifdef __cplusplus
}
#endif
