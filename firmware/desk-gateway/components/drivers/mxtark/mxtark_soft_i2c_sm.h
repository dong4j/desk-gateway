/**
 * @file mxtark_soft_i2c_sm.h
 * @brief Pure bit-level I2C slave state machine for the mxtark bus.
 *
 * The desk controller addresses one key endpoint (0x24) and four TM1650 digit
 * endpoints (0x34-0x37). ESP32-S3 hardware slave mode exposes only one address,
 * so this state machine handles all five without depending on ESP-IDF. Keeping
 * it pure allows captured transactions to be replayed on the host before any
 * timing-sensitive GPIO code is flashed.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define MXTARK_SOFT_I2C_ISR_ATTR IRAM_ATTR
#else
#define MXTARK_SOFT_I2C_ISR_ATTR
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MXTARK_SOFT_I2C_IDLE = 0,
    MXTARK_SOFT_I2C_RX_ADDRESS,
    MXTARK_SOFT_I2C_RX_DATA,
    MXTARK_SOFT_I2C_TX_DATA,
    MXTARK_SOFT_I2C_TX_MASTER_ACK,
    MXTARK_SOFT_I2C_IGNORE,
} mxtark_soft_i2c_phase_t;

typedef struct {
    bool ready;
    /** True after the controller clocked the complete 0x24 DR response. */
    bool key_read_completed;
    uint8_t addr7;
    uint8_t segment;
} mxtark_soft_i2c_digit_event_t;

typedef struct {
    mxtark_soft_i2c_phase_t phase;
    uint8_t bit_count;
    uint8_t rx_byte;
    uint8_t current_addr7;
    uint8_t tx_dr;
    bool drive_sda_low;
    bool pending_digit;
    uint8_t pending_segment;
} mxtark_soft_i2c_sm_t;

/** Initialize an idle, released bus state with the supplied key byte. */
void mxtark_soft_i2c_sm_init(mxtark_soft_i2c_sm_t *sm, uint8_t initial_dr);

/** Update the byte returned by the next read from address 0x24. */
void mxtark_soft_i2c_sm_set_dr(mxtark_soft_i2c_sm_t *sm, uint8_t dr);

/**
 * Return whether the slave is currently clocking the 0x24 DR response.
 *
 * START/STOP cannot be valid in the middle of this byte or its master ACK.
 * The GPIO adapter uses this protocol fact to ignore delayed slave-owned SDA
 * interrupts without changing receive-side transaction handling.
 */
bool MXTARK_SOFT_I2C_ISR_ATTR
mxtark_soft_i2c_sm_key_tx_active(const mxtark_soft_i2c_sm_t *sm);

/** Handle SDA high-to-low while SCL is high. */
void MXTARK_SOFT_I2C_ISR_ATTR
mxtark_soft_i2c_sm_start(mxtark_soft_i2c_sm_t *sm);

/** Handle SDA low-to-high while SCL is high. */
void MXTARK_SOFT_I2C_ISR_ATTR
mxtark_soft_i2c_sm_stop(mxtark_soft_i2c_sm_t *sm);

/** Sample one bus bit on an SCL rising edge. */
void MXTARK_SOFT_I2C_ISR_ATTR
mxtark_soft_i2c_sm_scl_rising(mxtark_soft_i2c_sm_t *sm, bool sda_high);

/**
 * Advance output timing on an SCL falling edge.
 *
 * The caller must apply sm->drive_sda_low after this function. A completed
 * digit write is returned only after its data byte ACK clock has completed.
 * key_read_completed marks the equivalent completion point for a 0x24 read;
 * it is diagnostic metadata and does not alter the state-machine decisions.
 */
mxtark_soft_i2c_digit_event_t MXTARK_SOFT_I2C_ISR_ATTR
mxtark_soft_i2c_sm_scl_falling(mxtark_soft_i2c_sm_t *sm);

#ifdef __cplusplus
}
#endif
