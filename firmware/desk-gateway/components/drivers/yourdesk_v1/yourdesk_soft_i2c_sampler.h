/**
 * @file yourdesk_soft_i2c_sampler.h
 * @brief Sampled-line adapter for the pure yourdesk multi-address I2C state machine.
 *
 * The ULP worker polls CLK/DAT much faster than the 9.6 kHz controller bus. This
 * adapter converts successive line samples into ordered START/STOP/SCL events
 * without depending on main-CPU GPIO interrupt latency. Keeping this layer pure
 * also lets host tests replay the exact sampled-line behavior.
 */
#pragma once

#include "yourdesk_soft_i2c_sm.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    yourdesk_soft_i2c_sm_t sm;
    bool previous_scl_high;
    bool previous_sda_high;
} yourdesk_soft_i2c_sampler_t;

typedef struct {
    bool scl_rising;
    bool scl_falling;
    bool start_detected;
    bool stop_detected;
    bool address_completed;
    bool key_read_aborted;
    uint8_t raw_address;
    bool drive_sda_low;
    yourdesk_soft_i2c_digit_event_t protocol_event;
} yourdesk_soft_i2c_sample_result_t;

/** Initialize the sampler from the live idle-or-active bus levels. */
void yourdesk_soft_i2c_sampler_init(yourdesk_soft_i2c_sampler_t *sampler,
                                    bool scl_high,
                                    bool sda_high,
                                    uint8_t initial_dr);

/**
 * Consume one CLK/DAT sample and return any protocol action it produced.
 *
 * When both lines differ from the previous sample, SCL wins. DAT is allowed to
 * change during SCL low for normal data setup, so treating that simultaneous
 * sample as START/STOP would corrupt the transaction. The ULP polling interval
 * is far shorter than one controller half-cycle, making this ordering both
 * deterministic and faithful to I2C timing.
 */
yourdesk_soft_i2c_sample_result_t
yourdesk_soft_i2c_sampler_sample(yourdesk_soft_i2c_sampler_t *sampler,
                                 bool scl_high,
                                 bool sda_high,
                                 uint8_t desired_dr);

#ifdef __cplusplus
}
#endif
