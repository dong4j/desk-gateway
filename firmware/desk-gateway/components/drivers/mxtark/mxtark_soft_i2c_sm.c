/**
 * @file mxtark_soft_i2c_sm.c
 * @brief Pure multi-address I2C state machine for mxtark.
 */
#include "mxtark_soft_i2c_sm.h"

#include <stddef.h>

#define ADDR_KEY_7BIT  0x24u
#define ADDR_DIG1_7BIT 0x34u
#define ADDR_DIG4_7BIT 0x37u

/** Return whether this transaction must be acknowledged by the replacement panel. */
static bool MXTARK_SOFT_I2C_ISR_ATTR
address_is_supported(uint8_t addr7, bool read)
{
    if (addr7 == ADDR_KEY_7BIT) {
        return true;
    }
    return !read && addr7 >= ADDR_DIG1_7BIT && addr7 <= ADDR_DIG4_7BIT;
}

/** Reset transaction-local state while preserving the current DR response. */
static void MXTARK_SOFT_I2C_ISR_ATTR
reset_transaction(mxtark_soft_i2c_sm_t *sm,
                  mxtark_soft_i2c_phase_t phase)
{
    sm->phase = phase;
    sm->bit_count = 0;
    sm->rx_byte = 0;
    sm->current_addr7 = 0;
    sm->drive_sda_low = false;
    sm->pending_digit = false;
    sm->pending_segment = 0;
}

void mxtark_soft_i2c_sm_init(mxtark_soft_i2c_sm_t *sm, uint8_t initial_dr)
{
    if (!sm) {
        return;
    }
    sm->tx_dr = initial_dr;
    reset_transaction(sm, MXTARK_SOFT_I2C_IDLE);
}

void mxtark_soft_i2c_sm_set_dr(mxtark_soft_i2c_sm_t *sm, uint8_t dr)
{
    if (sm) {
        sm->tx_dr = dr;
    }
}

bool mxtark_soft_i2c_sm_key_tx_active(const mxtark_soft_i2c_sm_t *sm)
{
    return sm && (sm->phase == MXTARK_SOFT_I2C_TX_DATA ||
                  sm->phase == MXTARK_SOFT_I2C_TX_MASTER_ACK);
}

void mxtark_soft_i2c_sm_start(mxtark_soft_i2c_sm_t *sm)
{
    if (sm) {
        reset_transaction(sm, MXTARK_SOFT_I2C_RX_ADDRESS);
    }
}

void mxtark_soft_i2c_sm_stop(mxtark_soft_i2c_sm_t *sm)
{
    if (sm) {
        reset_transaction(sm, MXTARK_SOFT_I2C_IDLE);
    }
}

void mxtark_soft_i2c_sm_scl_rising(mxtark_soft_i2c_sm_t *sm, bool sda_high)
{
    if (!sm) {
        return;
    }

    switch (sm->phase) {
    case MXTARK_SOFT_I2C_RX_ADDRESS:
    case MXTARK_SOFT_I2C_RX_DATA:
        if (sm->bit_count < 8) {
            sm->rx_byte = (uint8_t)((sm->rx_byte << 1) | (sda_high ? 1u : 0u));
            sm->bit_count++;
        } else if (sm->bit_count == 8) {
            /* Ninth rising edge: the master samples our ACK. */
            sm->bit_count = 9;
        }
        break;

    case MXTARK_SOFT_I2C_TX_DATA:
        if (sm->bit_count < 8) {
            sm->bit_count++;
        }
        break;

    case MXTARK_SOFT_I2C_TX_MASTER_ACK:
        /* The controller uses ACK+STOP after the single DR byte. */
        sm->bit_count = 1;
        break;

    case MXTARK_SOFT_I2C_IDLE:
    case MXTARK_SOFT_I2C_IGNORE:
    default:
        break;
    }
}

/** Prepare the ACK following a completed address byte. */
static void MXTARK_SOFT_I2C_ISR_ATTR
prepare_address_ack(mxtark_soft_i2c_sm_t *sm)
{
    uint8_t addr7 = (uint8_t)(sm->rx_byte >> 1);
    bool read = (sm->rx_byte & 1u) != 0;
    bool supported = address_is_supported(addr7, read);

    sm->current_addr7 = addr7;
    sm->drive_sda_low = supported;
    if (!supported) {
        sm->phase = MXTARK_SOFT_I2C_IGNORE;
    }
}

/** Start transmitting the current DR byte, placing its MSB during SCL low. */
static void MXTARK_SOFT_I2C_ISR_ATTR
begin_dr_transmit(mxtark_soft_i2c_sm_t *sm)
{
    sm->phase = MXTARK_SOFT_I2C_TX_DATA;
    sm->bit_count = 0;
    sm->drive_sda_low = (sm->tx_dr & 0x80u) == 0;
}

mxtark_soft_i2c_digit_event_t
mxtark_soft_i2c_sm_scl_falling(mxtark_soft_i2c_sm_t *sm)
{
    mxtark_soft_i2c_digit_event_t event = {0};
    if (!sm) {
        return event;
    }

    switch (sm->phase) {
    case MXTARK_SOFT_I2C_RX_ADDRESS:
        if (sm->bit_count == 8) {
            prepare_address_ack(sm);
        } else if (sm->bit_count == 9) {
            bool read = (sm->rx_byte & 1u) != 0;
            sm->drive_sda_low = false;
            if (sm->current_addr7 == ADDR_KEY_7BIT && read) {
                begin_dr_transmit(sm);
            } else {
                sm->phase = MXTARK_SOFT_I2C_RX_DATA;
                sm->bit_count = 0;
                sm->rx_byte = 0;
            }
        }
        break;

    case MXTARK_SOFT_I2C_RX_DATA:
        if (sm->bit_count == 8) {
            /* Every supported write byte is acknowledged; only digit data is emitted. */
            sm->drive_sda_low = true;
            if (sm->current_addr7 >= ADDR_DIG1_7BIT &&
                sm->current_addr7 <= ADDR_DIG4_7BIT) {
                sm->pending_digit = true;
                sm->pending_segment = sm->rx_byte;
            }
        } else if (sm->bit_count == 9) {
            sm->drive_sda_low = false;
            if (sm->pending_digit) {
                event.ready = true;
                event.addr7 = sm->current_addr7;
                event.segment = sm->pending_segment;
            }
            sm->pending_digit = false;
            sm->bit_count = 0;
            sm->rx_byte = 0;
        }
        break;

    case MXTARK_SOFT_I2C_TX_DATA:
        if (sm->bit_count < 8) {
            uint8_t mask = (uint8_t)(0x80u >> sm->bit_count);
            sm->drive_sda_low = (sm->tx_dr & mask) == 0;
        } else {
            /* Release SDA before the master ACK clock. */
            sm->drive_sda_low = false;
            sm->phase = MXTARK_SOFT_I2C_TX_MASTER_ACK;
            sm->bit_count = 0;
        }
        break;

    case MXTARK_SOFT_I2C_TX_MASTER_ACK:
        if (sm->bit_count == 1) {
            /* The controller sampled the whole DR byte and its ACK completed. */
            event.key_read_completed = true;
            sm->drive_sda_low = false;
            sm->phase = MXTARK_SOFT_I2C_IGNORE;
            sm->bit_count = 0;
        }
        break;

    case MXTARK_SOFT_I2C_IGNORE:
        sm->drive_sda_low = false;
        break;

    case MXTARK_SOFT_I2C_IDLE:
    default:
        break;
    }
    return event;
}
