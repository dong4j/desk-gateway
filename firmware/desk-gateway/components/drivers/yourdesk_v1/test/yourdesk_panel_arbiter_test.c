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

/** An idle panel must not cancel a gateway motion. */
static void test_idle_panel_preserves_gateway(void)
{
    yourdesk_panel_arbiter_t arbiter;
    yourdesk_panel_arbiter_result_t result;
    yourdesk_panel_arbiter_init(&arbiter, DR_IDLE);
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
    yourdesk_panel_arbiter_panel_update(&arbiter, true, DR_DOWN, &result);
    yourdesk_panel_arbiter_panel_update(&arbiter, false, DR_IDLE, &result);
    assert(result.panel_released);
    assert(result.output_dr == DR_IDLE);
}

int main(void)
{
    test_idle_panel_preserves_gateway();
    test_panel_takeover_does_not_resume_gateway();
    test_stop_suppresses_held_panel();
    test_disconnect_fails_idle();
    puts("yourdesk panel arbiter vectors: OK");
    return 0;
}
