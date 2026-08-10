/**
 * @file yourdesk_panel_arbiter.c
 * @brief Host-testable original-panel priority and STOP suppression logic.
 */
#include "yourdesk_panel_arbiter.h"

#include <stddef.h>

/** Publish a result without exposing mutable arbiter state to callers. */
static void finish_result(const yourdesk_panel_arbiter_t *arbiter,
                          uint8_t previous_output,
                          yourdesk_panel_arbiter_result_t *result)
{
    if (!result) {
        return;
    }
    result->output_dr = arbiter->output_dr;
    result->output_changed = previous_output != arbiter->output_dr;
}

void yourdesk_panel_arbiter_init(yourdesk_panel_arbiter_t *arbiter,
                                 uint8_t idle_dr)
{
    if (!arbiter) {
        return;
    }
    *arbiter = (yourdesk_panel_arbiter_t){
        .idle_dr = idle_dr,
        .gateway_dr = idle_dr,
        .output_dr = idle_dr,
        .panel_active = false,
        .panel_suppressed = false,
    };
}

bool yourdesk_panel_arbiter_gateway_request(
    yourdesk_panel_arbiter_t *arbiter, uint8_t dr,
    yourdesk_panel_arbiter_result_t *result)
{
    if (!arbiter) {
        return false;
    }
    uint8_t previous_output = arbiter->output_dr;
    if (result) {
        *result = (yourdesk_panel_arbiter_result_t){0};
    }

    if (dr != arbiter->idle_dr && arbiter->panel_active) {
        finish_result(arbiter, previous_output, result);
        return false;
    }

    arbiter->gateway_dr = dr;
    if (dr == arbiter->idle_dr && arbiter->panel_active) {
        /* A safety/STOP request must remain effective until panel release. */
        arbiter->panel_suppressed = true;
    }
    arbiter->output_dr = dr;
    finish_result(arbiter, previous_output, result);
    return true;
}

void yourdesk_panel_arbiter_panel_update(
    yourdesk_panel_arbiter_t *arbiter, bool connected, uint8_t dr,
    yourdesk_panel_arbiter_result_t *result)
{
    if (!arbiter) {
        return;
    }
    uint8_t previous_output = arbiter->output_dr;
    bool next_active = connected && dr != arbiter->idle_dr;
    bool panel_started = next_active && !arbiter->panel_active;
    bool panel_released = !next_active && arbiter->panel_active;

    if (result) {
        *result = (yourdesk_panel_arbiter_result_t){
            .panel_started = panel_started,
            .panel_released = panel_released,
        };
    }

    if (panel_started) {
        /* Never resume a gateway command after the human releases the panel. */
        arbiter->gateway_dr = arbiter->idle_dr;
    }

    arbiter->panel_active = next_active;
    if (next_active) {
        if (!arbiter->panel_suppressed) {
            arbiter->output_dr = dr;
        }
    } else if (panel_released) {
        arbiter->panel_suppressed = false;
        arbiter->output_dr = arbiter->gateway_dr;
    }
    /* Repeated idle polls must not overwrite an active gateway command. */

    finish_result(arbiter, previous_output, result);
}
