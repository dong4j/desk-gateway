/**
 * @file yourdesk_panel_arbiter_test.c
 * @brief Host checks for panel priority, release, disconnect, and STOP safety.
 */
#include "yourdesk_panel_arbiter.h"

#include <assert.h>
#include <stdio.h>

#define DR_IDLE 0x2Eu
#define DR_UP   0x47u
#define DR_DOWN 0x4Fu

/** Enable a newly initialized fail-closed panel and provide the required idle. */
static void enable_panel(yourdesk_panel_arbiter_t *arbiter,
                         yourdesk_panel_arbiter_result_t *result)
{
    yourdesk_panel_arbiter_set_enabled(arbiter, true, result);
    yourdesk_panel_arbiter_panel_update(arbiter, true, DR_IDLE, result);
    assert(!arbiter->panel_suppressed);
}

/** An idle panel must not cancel a gateway motion. */
static void test_idle_panel_preserves_gateway(void)
{
    yourdesk_panel_arbiter_t arbiter;
    yourdesk_panel_arbiter_result_t result;
    yourdesk_panel_arbiter_init(&arbiter, DR_IDLE);
    enable_panel(&arbiter, &result);
    assert(yourdesk_panel_arbiter_gateway_request(&arbiter, DR_UP, &result));
    yourdesk_panel_arbiter_panel_update(&arbiter, true, DR_IDLE, &result);
    assert(result.output_dr == DR_UP);
    assert(!arbiter.panel_active);
}

/** Panel takeover cancels, rather than pauses, the gateway command. */
static void test_panel_takeover_does_not_resume_gateway(void)
{
    yourdesk_panel_arbiter_t arbiter;
    yourdesk_panel_arbiter_result_t result;
    yourdesk_panel_arbiter_init(&arbiter, DR_IDLE);
    enable_panel(&arbiter, &result);
    assert(yourdesk_panel_arbiter_gateway_request(&arbiter, DR_UP, &result));

    yourdesk_panel_arbiter_panel_update(&arbiter, true, DR_DOWN, &result);
    assert(result.panel_started);
    assert(result.output_dr == DR_DOWN);
    assert(!yourdesk_panel_arbiter_gateway_request(&arbiter, DR_UP, &result));

    yourdesk_panel_arbiter_panel_update(&arbiter, true, DR_IDLE, &result);
    assert(result.panel_released);
    assert(result.output_dr == DR_IDLE);
}

/** STOP suppresses a physically held key until the user releases it. */
static void test_stop_suppresses_held_panel(void)
{
    yourdesk_panel_arbiter_t arbiter;
    yourdesk_panel_arbiter_result_t result;
    yourdesk_panel_arbiter_init(&arbiter, DR_IDLE);
    enable_panel(&arbiter, &result);
    yourdesk_panel_arbiter_panel_update(&arbiter, true, DR_UP, &result);
    assert(result.output_dr == DR_UP);

    assert(yourdesk_panel_arbiter_gateway_request(&arbiter, DR_IDLE, &result));
    assert(result.output_dr == DR_IDLE);
    yourdesk_panel_arbiter_panel_update(&arbiter, true, DR_UP, &result);
    assert(result.output_dr == DR_IDLE);

    yourdesk_panel_arbiter_panel_update(&arbiter, true, DR_IDLE, &result);
    assert(!arbiter.panel_suppressed);
    assert(result.output_dr == DR_IDLE);
}

/** Losing the panel bus while moving must immediately return idle. */
static void test_disconnect_fails_idle(void)
{
    yourdesk_panel_arbiter_t arbiter;
    yourdesk_panel_arbiter_result_t result;
    yourdesk_panel_arbiter_init(&arbiter, DR_IDLE);
    enable_panel(&arbiter, &result);
    yourdesk_panel_arbiter_panel_update(&arbiter, true, DR_DOWN, &result);
    yourdesk_panel_arbiter_panel_update(&arbiter, false, DR_IDLE, &result);
    assert(result.panel_released);
    assert(result.output_dr == DR_IDLE);
}

/** Disabled or freshly re-enabled panels must wait for a real key release. */
static void test_disabled_panel_requires_release_before_resume(void)
{
    yourdesk_panel_arbiter_t arbiter;
    yourdesk_panel_arbiter_result_t result;
    yourdesk_panel_arbiter_init(&arbiter, DR_IDLE);
    enable_panel(&arbiter, &result);

    yourdesk_panel_arbiter_panel_update(&arbiter, true, DR_UP, &result);
    assert(result.output_dr == DR_UP);
    yourdesk_panel_arbiter_set_enabled(&arbiter, false, &result);
    assert(result.output_dr == DR_IDLE);
    assert(!arbiter.panel_active);

    yourdesk_panel_arbiter_set_enabled(&arbiter, true, &result);
    yourdesk_panel_arbiter_panel_update(&arbiter, true, DR_UP, &result);
    assert(result.output_dr == DR_IDLE);
    assert(!result.panel_started);
    assert(!arbiter.panel_active);

    yourdesk_panel_arbiter_panel_update(&arbiter, true, DR_IDLE, &result);
    assert(!arbiter.panel_suppressed);
    yourdesk_panel_arbiter_panel_update(&arbiter, true, DR_UP, &result);
    assert(result.output_dr == DR_UP);
    assert(arbiter.panel_active);
}

int main(void)
{
    test_idle_panel_preserves_gateway();
    test_panel_takeover_does_not_resume_gateway();
    test_stop_suppresses_held_panel();
    test_disconnect_fails_idle();
    test_disabled_panel_requires_release_before_resume();
    puts("yourdesk panel arbiter vectors: OK");
    return 0;
}
