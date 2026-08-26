/**
 * @file desk_tof_snapshot_logic_test.c
 * @brief 验证高度快照读端会让出 CPU 并最终失败关闭。
 */
#include "desk_tof_snapshot_logic.h"

#include <assert.h>
#include <stdio.h>

static void test_only_stable_even_sequence_is_consistent(void)
{
    assert(desk_tof_snapshot_seq_consistent(0U, 0U));
    assert(desk_tof_snapshot_seq_consistent(2U, 2U));
    assert(!desk_tof_snapshot_seq_consistent(1U, 1U));
    assert(!desk_tof_snapshot_seq_consistent(2U, 4U));
}

/** 卡住的奇数序号必须经过有限空转和阻塞后放弃，不能无限循环。 */
static void test_stuck_writer_yields_then_abandons(void)
{
    unsigned spins = 0;
    unsigned yields = 0;
    unsigned steps = 0;

    for (unsigned attempt = 0; attempt < 64U; ++attempt) {
        ++steps;
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
        assert(action == DESK_TOF_SNAPSHOT_RETRY_ABANDON);
        break;
    }

    assert(spins == DESK_TOF_SNAPSHOT_SPIN_ATTEMPTS);
    assert(yields == DESK_TOF_SNAPSHOT_YIELD_ATTEMPTS);
    assert(steps == DESK_TOF_SNAPSHOT_SPIN_ATTEMPTS +
                        DESK_TOF_SNAPSHOT_YIELD_ATTEMPTS + 1U);
}

int main(void)
{
    test_only_stable_even_sequence_is_consistent();
    test_stuck_writer_yields_then_abandons();
    puts("desk_tof_snapshot_logic_test: ok");
    return 0;
}
