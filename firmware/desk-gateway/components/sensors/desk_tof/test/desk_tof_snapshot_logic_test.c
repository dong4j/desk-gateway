/**
 * @file desk_tof_snapshot_logic_test.c
 * @brief 高度快照 seqlock 读端必须让出 CPU，不能在奇数序号上忙等到看门狗。
 */
#include "desk_tof_snapshot_logic.h"

#include <assert.h>
#include <stdio.h>

/** 偶数且首尾一致才是完整样本，才能拷贝高度字段。 */
static void test_even_stable_seq_is_consistent(void)
{
    assert(desk_tof_snapshot_seq_consistent(0U, 0U));
    assert(desk_tof_snapshot_seq_consistent(2U, 2U));
    assert(desk_tof_snapshot_seq_consistent(100U, 100U));
}

/** 奇数表示写端尚未完成；begin==end 也不能当完整快照用。 */
static void test_odd_seq_is_never_consistent(void)
{
    assert(!desk_tof_snapshot_seq_consistent(1U, 1U));
    assert(!desk_tof_snapshot_seq_consistent(3U, 3U));
    assert(!desk_tof_snapshot_seq_readable(1U));
    assert(desk_tof_snapshot_seq_readable(2U));
}

/** 读写交叉时 begin/end 不一致，必须重试，不能拼出混合高度。 */
static void test_torn_seq_is_not_consistent(void)
{
    assert(!desk_tof_snapshot_seq_consistent(2U, 4U));
    assert(!desk_tof_snapshot_seq_consistent(0U, 2U));
}

/** 前几次失败可以空转，覆盖写端那几条 atomic store 的正常窗口。 */
static void test_early_failures_spin_without_yield(void)
{
    for (unsigned attempt = 0; attempt < DESK_TOF_SNAPSHOT_SPIN_ATTEMPTS;
         ++attempt) {
        assert(desk_tof_snapshot_retry_action(attempt) ==
               DESK_TOF_SNAPSHOT_RETRY_SPIN);
    }
}

/**
 * 空转用尽后必须 delay，而不是 taskYIELD：低优先级的 desk_tof 写端
 * 只有在高优先级读端真正阻塞后才能跑完第二下 fetch_add。
 */
static void test_later_failures_yield_to_preempted_writer(void)
{
    unsigned first_yield = DESK_TOF_SNAPSHOT_SPIN_ATTEMPTS;
    unsigned abandon_at =
        DESK_TOF_SNAPSHOT_SPIN_ATTEMPTS + DESK_TOF_SNAPSHOT_YIELD_ATTEMPTS;
    for (unsigned attempt = first_yield; attempt < abandon_at; ++attempt) {
        assert(desk_tof_snapshot_retry_action(attempt) ==
               DESK_TOF_SNAPSHOT_RETRY_YIELD);
    }
}

/** 写端序号卡在奇数时，读端必须放弃而不是无限循环。 */
static void test_stuck_odd_seq_abandons_after_bounded_retries(void)
{
    unsigned abandon_at =
        DESK_TOF_SNAPSHOT_SPIN_ATTEMPTS + DESK_TOF_SNAPSHOT_YIELD_ATTEMPTS;
    assert(desk_tof_snapshot_retry_action(abandon_at) ==
           DESK_TOF_SNAPSHOT_RETRY_ABANDON);
    assert(desk_tof_snapshot_retry_action(abandon_at + 20U) ==
           DESK_TOF_SNAPSHOT_RETRY_ABANDON);
}

/**
 * 模拟写端被抢占、序号一直为 1 的读循环。
 * 必须出现 yield，且总次数有上限，证明不会饿死 IDLE。
 */
static void test_stuck_writer_reader_loop_yields_then_abandons(void)
{
    const uint32_t stuck_odd = 1U;
    unsigned spins = 0;
    unsigned yields = 0;
    unsigned steps = 0;
    bool abandoned = false;

    for (unsigned attempt = 0; attempt < 64U; ++attempt) {
        ++steps;
        if (desk_tof_snapshot_seq_consistent(stuck_odd, stuck_odd)) {
            assert(0 && "stuck odd seq must not look consistent");
        }
        desk_tof_snapshot_retry_t action =
            desk_tof_snapshot_retry_action(attempt);
        if (action == DESK_TOF_SNAPSHOT_RETRY_SPIN) {
            ++spins;
            continue;
        }
        if (action == DESK_TOF_SNAPSHOT_RETRY_YIELD) {
            ++yields;
            continue;
        }
        abandoned = action == DESK_TOF_SNAPSHOT_RETRY_ABANDON;
        break;
    }

    assert(abandoned);
    assert(spins == DESK_TOF_SNAPSHOT_SPIN_ATTEMPTS);
    assert(yields == DESK_TOF_SNAPSHOT_YIELD_ATTEMPTS);
    assert(steps == DESK_TOF_SNAPSHOT_SPIN_ATTEMPTS +
                        DESK_TOF_SNAPSHOT_YIELD_ATTEMPTS + 1U);
}

int main(void)
{
    test_even_stable_seq_is_consistent();
    test_odd_seq_is_never_consistent();
    test_torn_seq_is_not_consistent();
    test_early_failures_spin_without_yield();
    test_later_failures_yield_to_preempted_writer();
    test_stuck_odd_seq_abandons_after_bounded_retries();
    test_stuck_writer_reader_loop_yields_then_abandons();
    puts("desk_tof_snapshot_logic_test: ok");
    return 0;
}
