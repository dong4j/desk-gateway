/**
 * @file yourdesk_soft_i2c_sampler.c
 * @brief Pure sampled-line frontend for the yourdesk I2C state machine.
 */
#include "yourdesk_soft_i2c_sampler.h"

/** Return whether START/STOP would interrupt an unfinished key response. */
static bool key_read_in_progress(const yourdesk_soft_i2c_sm_t *sm)
{
    return sm->phase == YOURDESK_SOFT_I2C_TX_DATA ||
           sm->phase == YOURDESK_SOFT_I2C_TX_MASTER_ACK;
}

void yourdesk_soft_i2c_sampler_init(yourdesk_soft_i2c_sampler_t *sampler,
                                    bool scl_high,
                                    bool sda_high,
                                    uint8_t initial_dr)
{
    if (!sampler) {
        return;
    }
    yourdesk_soft_i2c_sm_init(&sampler->sm, initial_dr);
    sampler->previous_scl_high = scl_high;
    sampler->previous_sda_high = sda_high;
}

yourdesk_soft_i2c_sample_result_t
yourdesk_soft_i2c_sampler_sample(yourdesk_soft_i2c_sampler_t *sampler,
                                 bool scl_high,
                                 bool sda_high,
                                 uint8_t desired_dr)
{
    yourdesk_soft_i2c_sample_result_t result = {0};
    if (!sampler) {
        return result;
    }

    /* Never replace a response byte after its first bit has reached the bus. */
    if (sampler->sm.phase != YOURDESK_SOFT_I2C_TX_DATA &&
        sampler->sm.phase != YOURDESK_SOFT_I2C_TX_MASTER_ACK) {
        yourdesk_soft_i2c_sm_set_dr(&sampler->sm, desired_dr);
    }

    if (scl_high != sampler->previous_scl_high) {
        if (scl_high) {
            result.scl_rising = true;
            yourdesk_soft_i2c_sm_scl_rising(&sampler->sm, sda_high);
        } else {
            result.scl_falling = true;
            yourdesk_soft_i2c_phase_t phase_before = sampler->sm.phase;
            uint8_t bit_count_before = sampler->sm.bit_count;
            uint8_t rx_byte_before = sampler->sm.rx_byte;
            result.protocol_event =
                yourdesk_soft_i2c_sm_scl_falling(&sampler->sm);
            if (phase_before == YOURDESK_SOFT_I2C_RX_ADDRESS &&
                bit_count_before == 8) {
                result.address_completed = true;
                result.raw_address = rx_byte_before;
            }
        }
    } else if (scl_high && sda_high != sampler->previous_sda_high) {
        result.key_read_aborted = key_read_in_progress(&sampler->sm);
        if (sda_high) {
            result.stop_detected = true;
            yourdesk_soft_i2c_sm_stop(&sampler->sm);
        } else {
            result.start_detected = true;
            yourdesk_soft_i2c_sm_start(&sampler->sm);
        }
    }

    sampler->previous_scl_high = scl_high;
    sampler->previous_sda_high = sda_high;
    result.drive_sda_low = sampler->sm.drive_sda_low;
    return result;
}
