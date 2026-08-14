/**
 * @file desk_motion_watch_test.c
 * @brief 无位移诊断状态机的主机回归测试。
 */
#include "desk_motion_watch.h"

#include <assert.h>
#include <stdio.h>

static void test_reports_stall_once(void)
{
    desk_motion_watch_t watch = {0};
    assert(desk_motion_watch_update(
               &watch, DESK_MOTION_WATCH_UP, true, 700, 560, 940, 100) ==
           DESK_MOTION_WATCH_NO_CHANGE);
    assert(desk_motion_watch_update(
               &watch, DESK_MOTION_WATCH_UP, true, 703, 560, 940, 2600) ==
           DESK_MOTION_WATCH_STALLED);
    assert(desk_motion_watch_update(
               &watch, DESK_MOTION_WATCH_UP, true, 703, 560, 940, 3000) ==
           DESK_MOTION_WATCH_NO_CHANGE);
}

static void test_progress_and_boundaries_do_not_stall(void)
{
    desk_motion_watch_t watch = {0};
    (void)desk_motion_watch_update(
        &watch, DESK_MOTION_WATCH_DOWN, true, 800, 560, 940, 0);
    assert(desk_motion_watch_update(
               &watch, DESK_MOTION_WATCH_DOWN, true, 791, 560, 940, 500) ==
           DESK_MOTION_WATCH_PROGRESS);
    assert(desk_motion_watch_update(
               &watch, DESK_MOTION_WATCH_DOWN, true, 791, 560, 940, 5000) ==
           DESK_MOTION_WATCH_PROGRESS);

    desk_motion_watch_reset(&watch);
    assert(desk_motion_watch_update(
               &watch, DESK_MOTION_WATCH_DOWN, true, 565, 560, 940, 0) ==
           DESK_MOTION_WATCH_NO_CHANGE);
    assert(!watch.tracking);
}

static void test_unknown_height_cancels_tracking(void)
{
    desk_motion_watch_t watch = {0};
    (void)desk_motion_watch_update(
        &watch, DESK_MOTION_WATCH_TARGET, true, 700, 560, 940, 0);
    (void)desk_motion_watch_update(
        &watch, DESK_MOTION_WATCH_TARGET, false, -1, 560, 940, 3000);
    assert(!watch.tracking);
}

int main(void)
{
    test_reports_stall_once();
    test_progress_and_boundaries_do_not_stall();
    test_unknown_height_cancels_tracking();
    puts("desk_motion_watch_test: OK");
    return 0;
}
