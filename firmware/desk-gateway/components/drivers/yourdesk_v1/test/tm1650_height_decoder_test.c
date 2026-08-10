/**
 * @file tm1650_height_decoder_test.c
 * @brief Host-side golden-vector checks for the TM1650 height decoder.
 *
 * The vectors are derived from archived logic-analyzer captures. Keeping this
 * test independent of ESP-IDF makes protocol regressions cheap to detect.
 */
#include "tm1650_height_decoder.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

/** Feed one controller-ordered display frame and require the expected height. */
static void expect_height(uint8_t dig1, uint8_t dig2, uint8_t dig3,
                          uint8_t dig4, int expected_mm)
{
    tm1650_height_decoder_t decoder;
    tm1650_height_decoder_reset(&decoder);
    int height_mm = -1;

    assert(tm1650_height_decoder_feed(&decoder, 0x36, dig3, &height_mm) ==
           TM1650_HEIGHT_WAITING);
    assert(tm1650_height_decoder_feed(&decoder, 0x35, dig2, &height_mm) ==
           TM1650_HEIGHT_WAITING);
    assert(tm1650_height_decoder_feed(&decoder, 0x34, dig1, &height_mm) ==
           TM1650_HEIGHT_WAITING);
    assert(tm1650_height_decoder_feed(&decoder, 0x37, dig4, &height_mm) ==
           TM1650_HEIGHT_VALID);
    assert(height_mm == expected_mm);
}

/** Ensure fragments from adjacent refreshes cannot be combined into a height. */
static void expect_malformed_order_rejected(void)
{
    tm1650_height_decoder_t decoder;
    tm1650_height_decoder_reset(&decoder);
    int height_mm = -1;

    assert(tm1650_height_decoder_feed(&decoder, 0x36, 0xC5, &height_mm) ==
           TM1650_HEIGHT_WAITING);
    assert(tm1650_height_decoder_feed(&decoder, 0x34, 0x00, &height_mm) ==
           TM1650_HEIGHT_WAITING);
    assert(tm1650_height_decoder_feed(&decoder, 0x35, 0xDB, &height_mm) ==
           TM1650_HEIGHT_WAITING);
    assert(tm1650_height_decoder_feed(&decoder, 0x37, 0xC5, &height_mm) ==
           TM1650_HEIGHT_WAITING);
    assert(height_mm == -1);

    /* A fresh, correctly ordered DIG3 must recover without reinitialization. */
    assert(tm1650_height_decoder_feed(&decoder, 0x36, 0xC5, &height_mm) ==
           TM1650_HEIGHT_WAITING);
    assert(tm1650_height_decoder_feed(&decoder, 0x35, 0xDB, &height_mm) ==
           TM1650_HEIGHT_WAITING);
    assert(tm1650_height_decoder_feed(&decoder, 0x34, 0x00, &height_mm) ==
           TM1650_HEIGHT_WAITING);
    assert(tm1650_height_decoder_feed(&decoder, 0x37, 0xC5, &height_mm) ==
           TM1650_HEIGHT_VALID);
    assert(height_mm == 640);
}

/** Verify the full 0-9 map plus the four named capture anchors. */
int main(void)
{
    static const uint8_t segments[10] = {
        0x5F, 0x44, 0x9E, 0xD6, 0xC5,
        0xD3, 0xDB, 0x46, 0xDF, 0xD7,
    };

    /* 70-79 exercises every ones digit while keeping the height plausible. */
    for (int digit = 0; digit < 10; ++digit) {
        expect_height(0x00, segments[7], segments[digit], segments[digit],
                      (70 + digit) * 10);
    }

    expect_height(0x00, 0xDB, 0xC5, 0xC5, 640);
    expect_height(0x00, 0xDF, 0x9E, 0x9E, 820);
    expect_height(0x44, 0x5F, 0x9E, 0x9E, 1020);
    expect_height(0x44, 0x9E, 0x44, 0x44, 1210);
    expect_malformed_order_rejected();

    puts("tm1650 height golden vectors: OK");
    return 0;
}
