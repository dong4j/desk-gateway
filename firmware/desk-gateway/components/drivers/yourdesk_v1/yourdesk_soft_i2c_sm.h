/**
 * @file yourdesk_soft_i2c_sm.h
 * @brief Pure bit-level I2C slave state machine for the yourdesk_v1 bus.
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
#define YOURDESK_SOFT_I2C_ISR_ATTR IRAM_ATTR
#else
#define YOURDESK_SOFT_I2C_ISR_ATTR
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    YOURDESK_SOFT_I2C_IDLE = 0,
    YOURDESK_SOFT_I2C_RX_ADDRESS,
    YOURDESK_SOFT_I2C_RX_DATA,
    YOURDESK_SOFT_I2C_TX_DATA,
    YOURDESK_SOFT_I2C_TX_MASTER_ACK,
    YOURDESK_SOFT_I2C_IGNORE,
} yourdesk_soft_i2c_phase_t;

typedef struct {
    bool ready;
    /** True after the controller clocked the complete 0x24 DR response. */
    bool key_read_completed;
    uint8_t addr7;
    uint8_t segment;
} yourdesk_soft_i2c_digit_event_t;

typedef struct {
    yourdesk_soft_i2c_phase_t phase;
    uint8_t bit_count;
    uint8_t rx_byte;
    uint8_t current_addr7;
    uint8_t tx_dr;
    bool drive_sda_low;
    bool pending_digit;
    uint8_t pending_segment;
} yourdesk_soft_i2c_sm_t;

/** Initialize an idle, released bus state with the supplied key byte. */
void yourdesk_soft_i2c_sm_init(yourdesk_soft_i2c_sm_t *sm, uint8_t initial_dr);

/** Update the byte returned by the next read from address 0x24. */
void yourdesk_soft_i2c_sm_set_dr(yourdesk_soft_i2c_sm_t *sm, uint8_t dr);

/** Handle SDA high-to-low while SCL is high. */
void YOURDESK_SOFT_I2C_ISR_ATTR
yourdesk_soft_i2c_sm_start(yourdesk_soft_i2c_sm_t *sm);

/** Handle SDA low-to-high while SCL is high. */
void YOURDESK_SOFT_I2C_ISR_ATTR
yourdesk_soft_i2c_sm_stop(yourdesk_soft_i2c_sm_t *sm);

/** Sample one bus bit on an SCL rising edge. */
void YOURDESK_SOFT_I2C_ISR_ATTR
yourdesk_soft_i2c_sm_scl_rising(yourdesk_soft_i2c_sm_t *sm, bool sda_high);

/**
 * Advance output timing on an SCL falling edge.
 *
 * The caller must apply sm->drive_sda_low after this function. A completed
 * digit write is returned only after its data byte ACK clock has completed.
 * key_read_completed marks the equivalent completion point for a 0x24 read;
 * it is diagnostic metadata and does not alter the state-machine decisions.
 */
yourdesk_soft_i2c_digit_event_t YOURDESK_SOFT_I2C_ISR_ATTR
yourdesk_soft_i2c_sm_scl_falling(yourdesk_soft_i2c_sm_t *sm);

#ifdef __cplusplus
}
#endif
