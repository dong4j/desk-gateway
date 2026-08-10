/**
 * @file tm1650_height_decoder.h
 * @brief Assemble TM1650 digit writes and decode the displayed desk height.
 *
 * The controller writes one segment byte to each of addresses 0x34-0x37.
 * This decoder is deliberately independent of ESP-IDF so captured frames can
 * be replayed in a host-side golden-vector check.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TM1650_HEIGHT_WAITING = 0,
    TM1650_HEIGHT_VALID,
    TM1650_HEIGHT_INVALID,
} tm1650_height_result_t;

typedef struct {
    uint8_t digits[4];
    uint8_t received_mask;
    uint8_t next_order_index;
} tm1650_height_decoder_t;

/** Reset an incomplete display frame. */
void tm1650_height_decoder_reset(tm1650_height_decoder_t *decoder);

/**
 * Feed one completed digit write.
 *
 * A new DIG3 (0x36) starts the observed controller write order
 * DIG3 -> DIG2 -> DIG1 -> DIG4. A height is returned only when all four
 * addresses arrive exactly once in that order; fragments must not be combined
 * across display refreshes.
 */
tm1650_height_result_t tm1650_height_decoder_feed(tm1650_height_decoder_t *decoder,
                                                   uint8_t addr7,
                                                   uint8_t segment,
                                                   int *out_height_mm);

#ifdef __cplusplus
}
#endif
