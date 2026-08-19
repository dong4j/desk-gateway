/**
 * @file desk_tof_snapshot_logic.h
 * @brief 高度快照 seqlock 读端策略，可在主机测试中验证。
 *
 * 写端用奇数序号标记“正在发布”。读端若在奇数上无限忙等，高优先级的
 * yd_tof_guard / yd_panel 会把双核占满，低优先级的 desk_tof 无法把序号
 * 改回偶数，IDLE 喂狗失败。因此失败几次后必须 vTaskDelay，超时则放弃。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 覆盖 publish_height() 里几条 atomic store 的正常窗口，不必让出。 */
#define DESK_TOF_SNAPSHOT_SPIN_ATTEMPTS 8U
/**
 * 空转无效后阻塞的次数。必须用 vTaskDelay(1) 而不是 taskYIELD()：
 * yield 只让给同级或更高优先级，救不了优先级 5 的写端。
 */
#define DESK_TOF_SNAPSHOT_YIELD_ATTEMPTS 3U

typedef enum {
    DESK_TOF_SNAPSHOT_RETRY_SPIN = 0,
    DESK_TOF_SNAPSHOT_RETRY_YIELD,
    DESK_TOF_SNAPSHOT_RETRY_ABANDON,
} desk_tof_snapshot_retry_t;

/** 偶数序号表示当前没有未完成的高度发布。 */
bool desk_tof_snapshot_seq_readable(uint32_t begin_seq);

/** begin 与 end 相同且为偶数，才是一组完整高度样本。 */
bool desk_tof_snapshot_seq_consistent(uint32_t begin_seq, uint32_t end_seq);

/**
 * 根据已经失败的次数决定下一步。
 *
 * @param failed_attempts 本次 snapshot 调用中已发生的不一致次数，从 0 计。
 */
desk_tof_snapshot_retry_t desk_tof_snapshot_retry_action(
    unsigned failed_attempts);

#ifdef __cplusplus
}
#endif
