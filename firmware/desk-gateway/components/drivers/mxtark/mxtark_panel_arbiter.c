/**
 * @file mxtark_panel_arbiter.c
 * @brief Host-testable original-panel priority and STOP suppression logic.
 */
#include "mxtark_panel_arbiter.h"

#include <stddef.h>

/* Confirmed original-panel values from the archived 12 MHz captures. */
#define PANEL_DR_UP       0x47u
#define PANEL_DR_DOWN     0x4Fu
#define PANEL_DR_P1_GOTO  0x17u
#define PANEL_DR_P1_SAVE  0x57u
#define PANEL_DR_P4_GOTO  0x2Fu
#define PANEL_DR_P4_SAVE  0x6Fu

uint8_t mxtark_panel_arbiter_normalize_dr(uint8_t dr, uint8_t idle_dr)
{
    switch (dr) {
    case PANEL_DR_UP:
    case PANEL_DR_DOWN:
    case PANEL_DR_P1_GOTO:
    case PANEL_DR_P1_SAVE:
    case PANEL_DR_P4_GOTO:
    case PANEL_DR_P4_SAVE:
        return dr;
    default:
        return idle_dr;
    }
}

/** Publish a result without exposing mutable arbiter state to callers. */
static void finish_result(const mxtark_panel_arbiter_t *arbiter,
                          uint8_t previous_output,
                          mxtark_panel_arbiter_result_t *result)
{
    if (!result) {
        return;
    }
    result->output_dr = arbiter->output_dr;
    result->output_changed = previous_output != arbiter->output_dr;
}

void mxtark_panel_arbiter_init(mxtark_panel_arbiter_t *arbiter,
                                 uint8_t idle_dr)
{
    if (!arbiter) {
        return;
    }
    *arbiter = (mxtark_panel_arbiter_t){
        .idle_dr = idle_dr,
        .gateway_dr = idle_dr,
        .output_dr = idle_dr,
        .panel_enabled = false,
        .panel_active = false,
        .panel_suppressed = true,
    };
}

void mxtark_panel_arbiter_set_enabled(
    mxtark_panel_arbiter_t *arbiter, bool enabled,
    mxtark_panel_arbiter_result_t *result)
{
    if (!arbiter) {
        return;
    }
    uint8_t previous_output = arbiter->output_dr;
    bool panel_released = arbiter->panel_active;
    if (result) {
        *result = (mxtark_panel_arbiter_result_t){
            .panel_released = panel_released,
        };
    }

    arbiter->panel_enabled = enabled;
    arbiter->panel_active = false;
    /* Both transitions require a fresh physical release before accepting keys. */
    arbiter->panel_suppressed = true;
    arbiter->output_dr = arbiter->gateway_dr;
    finish_result(arbiter, previous_output, result);
}

bool mxtark_panel_arbiter_gateway_request(
    mxtark_panel_arbiter_t *arbiter, uint8_t dr,
    mxtark_panel_arbiter_result_t *result)
{
    if (!arbiter) {
        return false;
    }
    uint8_t previous_output = arbiter->output_dr;
    if (result) {
        *result = (mxtark_panel_arbiter_result_t){0};
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

void mxtark_panel_arbiter_panel_update(
    mxtark_panel_arbiter_t *arbiter, bool connected, uint8_t dr,
    mxtark_panel_arbiter_result_t *result)
{
    if (!arbiter) {
        return;
    }
    uint8_t normalized_dr =
        mxtark_panel_arbiter_normalize_dr(dr, arbiter->idle_dr);
    uint8_t previous_output = arbiter->output_dr;
    bool next_active = connected && normalized_dr != arbiter->idle_dr;
    bool panel_started = next_active && !arbiter->panel_active;
    bool panel_released = !next_active && arbiter->panel_active;

    if (result) {
        *result = (mxtark_panel_arbiter_result_t){
            .panel_started = panel_started,
            .panel_released = panel_released,
        };
    }

    if (!arbiter->panel_enabled) {
        /* Disabled input can never own or alter the controller-facing byte. */
        arbiter->panel_active = false;
        arbiter->panel_suppressed = true;
        arbiter->output_dr = arbiter->gateway_dr;
        if (result) {
            result->panel_started = false;
        }
        finish_result(arbiter, previous_output, result);
        return;
    }

    if (arbiter->panel_suppressed) {
        if (result) {
            /* A held key while re-enabling is still blocked, not a takeover. */
            result->panel_started = false;
        }
        if (!next_active) {
            arbiter->panel_active = false;
            arbiter->panel_suppressed = false;
            arbiter->output_dr = arbiter->gateway_dr;
        }
        finish_result(arbiter, previous_output, result);
        return;
    }

    if (panel_started) {
        /* Never resume a gateway command after the human releases the panel. */
        arbiter->gateway_dr = arbiter->idle_dr;
    }

    arbiter->panel_active = next_active;
    if (next_active) {
        if (!arbiter->panel_suppressed) {
            arbiter->output_dr = normalized_dr;
        }
    } else if (panel_released) {
        arbiter->panel_suppressed = false;
        arbiter->output_dr = arbiter->gateway_dr;
    }
    /* Repeated idle polls must not overwrite an active gateway command. */

    finish_result(arbiter, previous_output, result);
}
