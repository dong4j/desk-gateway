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
#define DR_RESET 0x7Fu
#define DR_UNKNOWN_RELEASE 0x00u

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

/** An unknown post-key sample releases the panel and restores Web control. */
static void test_unknown_release_restores_gateway_control(void)
{
    yourdesk_panel_arbiter_t arbiter;
    yourdesk_panel_arbiter_result_t result;
    yourdesk_panel_arbiter_init(&arbiter, DR_IDLE);
    enable_panel(&arbiter, &result);

    assert(yourdesk_panel_arbiter_gateway_request(&arbiter, DR_UP, &result));
    yourdesk_panel_arbiter_panel_update(&arbiter, true, DR_DOWN, &result);
    assert(result.panel_started);
    assert(!yourdesk_panel_arbiter_gateway_request(&arbiter, DR_UP, &result));

    yourdesk_panel_arbiter_panel_update(&arbiter, true, DR_UNKNOWN_RELEASE,
                                        &result);
    assert(result.panel_released);
    assert(!arbiter.panel_active);
    assert(result.output_dr == DR_IDLE);
    assert(yourdesk_panel_arbiter_gateway_request(&arbiter, DR_UP, &result));
    assert(result.output_dr == DR_UP);
}

/** A connected panel with an unknown value must not preempt Web movement. */
static void test_unknown_idle_preserves_gateway(void)
{
    yourdesk_panel_arbiter_t arbiter;
    yourdesk_panel_arbiter_result_t result;
    yourdesk_panel_arbiter_init(&arbiter, DR_IDLE);
    enable_panel(&arbiter, &result);

    assert(yourdesk_panel_arbiter_gateway_request(&arbiter, DR_UP, &result));
    yourdesk_panel_arbiter_panel_update(&arbiter, true, 0xFFu, &result);
    assert(!arbiter.panel_active);
    assert(result.output_dr == DR_UP);
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

/** Reset uses the captured combined-key code and remains interruptible by STOP. */
static void test_gateway_reset_obeys_panel_and_stop_safety(void)
{
    yourdesk_panel_arbiter_t arbiter;
    yourdesk_panel_arbiter_result_t result;
    yourdesk_panel_arbiter_init(&arbiter, DR_IDLE);
    enable_panel(&arbiter, &result);

    assert(yourdesk_panel_arbiter_gateway_request(&arbiter, DR_RESET,
                                                  &result));
    assert(result.output_dr == DR_RESET);
    assert(yourdesk_panel_arbiter_gateway_request(&arbiter, DR_IDLE, &result));
    assert(result.output_dr == DR_IDLE);

    yourdesk_panel_arbiter_panel_update(&arbiter, true, DR_UP, &result);
    assert(result.output_dr == DR_UP);
    assert(!yourdesk_panel_arbiter_gateway_request(&arbiter, DR_RESET,
                                                   &result));
}

int main(void)
{
    test_idle_panel_preserves_gateway();
    test_panel_takeover_does_not_resume_gateway();
    test_unknown_release_restores_gateway_control();
    test_unknown_idle_preserves_gateway();
    test_stop_suppresses_held_panel();
    test_disconnect_fails_idle();
    test_disabled_panel_requires_release_before_resume();
    test_gateway_reset_obeys_panel_and_stop_safety();
    puts("yourdesk panel arbiter vectors: OK");
    return 0;
}
