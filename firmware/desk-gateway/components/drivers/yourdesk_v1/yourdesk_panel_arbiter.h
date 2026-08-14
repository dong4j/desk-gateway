/**
 * @file yourdesk_panel_arbiter.h
 * @brief Pure command arbiter for the original-panel proxy.
 *
 * The original panel must preempt a gateway motion without allowing that
 * gateway motion to resume after the panel is released. Keeping this state
 * machine independent of ESP-IDF makes the safety behavior host-testable.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t idle_dr;
    uint8_t gateway_dr;
    uint8_t output_dr;
    bool panel_enabled;
    bool panel_active;
    bool panel_suppressed;
} yourdesk_panel_arbiter_t;

typedef struct {
    uint8_t output_dr;
    bool output_changed;
    bool panel_started;
    bool panel_released;
} yourdesk_panel_arbiter_result_t;

/**
 * Normalize a raw TM1650 panel value before it enters motion arbitration.
 *
 * Only captured UP/DOWN and P1/P4 values are actionable. An idle, malformed,
 * or not-yet-reversed value becomes idle so it cannot hold wireless control
 * indefinitely after a physical key is released.
 */
uint8_t yourdesk_panel_arbiter_normalize_dr(uint8_t dr, uint8_t idle_dr);

/** Initialize both sources and the controller-facing output to idle. */
void yourdesk_panel_arbiter_init(yourdesk_panel_arbiter_t *arbiter,
                                 uint8_t idle_dr);

/**
 * Enable or disable the physical panel source.
 *
 * The arbiter starts disabled (fail closed). Re-enabling keeps the panel
 * suppressed until an idle key sample is observed, preventing a held key from
 * starting the desk immediately after child-lock release.
 */
void yourdesk_panel_arbiter_set_enabled(
    yourdesk_panel_arbiter_t *arbiter, bool enabled,
    yourdesk_panel_arbiter_result_t *result);

/**
 * Apply a gateway command.
 *
 * Non-idle commands are rejected while the panel owns the bus. Idle is always
 * accepted and suppresses a held panel key until that key is physically
 * released, so STOP and height safety cannot be immediately undone.
 */
bool yourdesk_panel_arbiter_gateway_request(
    yourdesk_panel_arbiter_t *arbiter, uint8_t dr,
    yourdesk_panel_arbiter_result_t *result);

/**
 * Apply the latest cached panel key.
 *
 * A disconnected panel and every unknown raw value are treated as idle. The
 * first supported action key cancels any gateway motion permanently; releasing
 * the panel therefore returns idle rather than resuming a stale command.
 */
void yourdesk_panel_arbiter_panel_update(
    yourdesk_panel_arbiter_t *arbiter, bool connected, uint8_t dr,
    yourdesk_panel_arbiter_result_t *result);

#ifdef __cplusplus
}
#endif
