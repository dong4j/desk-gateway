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
           TM1650_HEIGHT_VALID);
    assert(height_mm == expected_mm);

    /* A lagging mirror digit neither blocks nor republishes the height. */
    assert(tm1650_height_decoder_feed(&decoder, 0x37, dig4, &height_mm) ==
           TM1650_HEIGHT_WAITING);
    assert(height_mm == expected_mm);
}

/** Require a complete numeric frame to be rejected as malformed. */
static void expect_invalid_height(uint8_t dig1, uint8_t dig2, uint8_t dig3)
{
    tm1650_height_decoder_t decoder;
    tm1650_height_decoder_reset(&decoder);
    int height_mm = -1;

    assert(tm1650_height_decoder_feed(&decoder, 0x36, dig3, &height_mm) ==
           TM1650_HEIGHT_WAITING);
    assert(tm1650_height_decoder_feed(&decoder, 0x35, dig2, &height_mm) ==
           TM1650_HEIGHT_WAITING);
    assert(tm1650_height_decoder_feed(&decoder, 0x34, dig1, &height_mm) ==
           TM1650_HEIGHT_INVALID);
    assert(height_mm == -1);
}

/** Verify that a new frame works even when the previous DIG4 never arrived. */
static void expect_missing_mirror_recovery(void)
{
    tm1650_height_decoder_t decoder;
    tm1650_height_decoder_reset(&decoder);
    int height_mm = -1;

    assert(tm1650_height_decoder_feed(&decoder, 0x36, 0xC5, &height_mm) ==
           TM1650_HEIGHT_WAITING);
    assert(tm1650_height_decoder_feed(&decoder, 0x35, 0xDB, &height_mm) ==
           TM1650_HEIGHT_WAITING);
    assert(tm1650_height_decoder_feed(&decoder, 0x34, 0x00, &height_mm) ==
           TM1650_HEIGHT_VALID);
    assert(height_mm == 640);

    /* The next DIG3 is the authoritative boundary, not the missing DIG4. */
    assert(tm1650_height_decoder_feed(&decoder, 0x36, 0x9E, &height_mm) ==
           TM1650_HEIGHT_WAITING);
    /* A delayed old mirror must not reset the new numeric frame. */
    assert(tm1650_height_decoder_feed(&decoder, 0x37, 0xC5, &height_mm) ==
           TM1650_HEIGHT_WAITING);
    assert(tm1650_height_decoder_feed(&decoder, 0x35, 0x5F, &height_mm) ==
           TM1650_HEIGHT_WAITING);
    assert(tm1650_height_decoder_feed(&decoder, 0x34, 0x44, &height_mm) ==
           TM1650_HEIGHT_VALID);
    assert(height_mm == 1020);
}

/** Replay upward fragments that never form adjacent 20 ms display frames. */
static void expect_fragmented_up_cache(void)
{
    tm1650_height_cache_t cache;
    tm1650_height_cache_reset(&cache);
    int height_mm = -1;
    uint32_t oldest_age_ms = 0;

    /* Initial complete numeric writes establish the 64 cm cached value. */
    assert(tm1650_height_cache_feed(&cache, 0x36, 0xC5, 0, 1500,
                                    &height_mm, &oldest_age_ms) ==
           TM1650_HEIGHT_WAITING);
    assert(tm1650_height_cache_feed(&cache, 0x35, 0xDB, 2, 1500,
                                    &height_mm, &oldest_age_ms) ==
           TM1650_HEIGHT_WAITING);
    assert(tm1650_height_cache_feed(&cache, 0x34, 0x00, 4, 1500,
                                    &height_mm, &oldest_age_ms) ==
           TM1650_HEIGHT_VALID);
    assert(height_mm == 640);
    assert(oldest_age_ms == 4);

    /* A lone next-generation digit cannot reuse the previous two digits. */
    assert(tm1650_height_cache_feed(&cache, 0x36, 0xD3, 849, 1500,
                                    &height_mm, &oldest_age_ms) ==
           TM1650_HEIGHT_WAITING);

    /* An invalid segment must not evict the last valid ones digit. */
    assert(tm1650_height_cache_feed(&cache, 0x36, 0xDD, 1200, 1500,
                                    &height_mm, &oldest_age_ms) ==
           TM1650_HEIGHT_INVALID);

    /* Once the other digits are stale, all three must be refreshed again. */
    assert(tm1650_height_cache_feed(&cache, 0x36, 0xDB, 2000, 1500,
                                    &height_mm, &oldest_age_ms) ==
           TM1650_HEIGHT_WAITING);
    assert(tm1650_height_cache_feed(&cache, 0x35, 0xDB, 2002, 1500,
                                    &height_mm, &oldest_age_ms) ==
           TM1650_HEIGHT_WAITING);
    assert(tm1650_height_cache_feed(&cache, 0x34, 0x00, 2004, 1500,
                                    &height_mm, &oldest_age_ms) ==
           TM1650_HEIGHT_VALID);
    assert(height_mm == 660);
    assert(oldest_age_ms == 4);
}

/** Verify fragmented register updates also understand xx.x inch frames. */
static void expect_imperial_cache(void)
{
    tm1650_height_cache_t cache;
    tm1650_height_cache_reset(&cache);
    int height_mm = -1;
    uint32_t oldest_age_ms = 0;

    assert(tm1650_height_cache_feed(&cache, 0x36, 0x5F, 0, 1500,
                                    &height_mm, &oldest_age_ms) ==
           TM1650_HEIGHT_WAITING);
    assert(tm1650_height_cache_feed(&cache, 0x35, 0xF3, 2, 1500,
                                    &height_mm, &oldest_age_ms) ==
           TM1650_HEIGHT_WAITING);
    assert(tm1650_height_cache_feed(&cache, 0x34, 0x9E, 4, 1500,
                                    &height_mm, &oldest_age_ms) ==
           TM1650_HEIGHT_VALID);
    assert(height_mm == 635);
    assert(oldest_age_ms == 4);

    /* A decimal point on any digit except DIG2 is not a supported unit frame. */
    assert(tm1650_height_cache_feed(&cache, 0x36, 0x7F, 10, 1500,
                                    &height_mm, &oldest_age_ms) ==
           TM1650_HEIGHT_INVALID);
}

/** Ensure adjacent imperial refreshes cannot synthesize a half-inch jump. */
static void expect_imperial_cache_generation_isolation(void)
{
    tm1650_height_cache_t cache;
    tm1650_height_cache_reset(&cache);
    int height_mm = -1;
    uint32_t oldest_age_ms = 0;

    /* Establish 27.5 in (698.5 mm, rounded to 699 mm). */
    assert(tm1650_height_cache_feed(&cache, 0x36, 0xD3, 0, 1500,
                                    &height_mm, &oldest_age_ms) ==
           TM1650_HEIGHT_WAITING);
    assert(tm1650_height_cache_feed(&cache, 0x35, 0x66, 2, 1500,
                                    &height_mm, &oldest_age_ms) ==
           TM1650_HEIGHT_WAITING);
    assert(tm1650_height_cache_feed(&cache, 0x34, 0x9E, 4, 1500,
                                    &height_mm, &oldest_age_ms) ==
           TM1650_HEIGHT_VALID);
    assert(height_mm == 699);

    /*
     * Updating only the decimal tens digit must not combine 28.x with the
     * stale 5 and publish the fabricated 28.5 in (724 mm) seen in hardware.
     */
    assert(tm1650_height_cache_feed(&cache, 0x35, 0xFF, 400, 1500,
                                    &height_mm, &oldest_age_ms) ==
           TM1650_HEIGHT_WAITING);
    assert(height_mm == 699);

    /* Once every digit is fresh, the real 28.0 in frame is published. */
    assert(tm1650_height_cache_feed(&cache, 0x36, 0x5F, 402, 1500,
                                    &height_mm, &oldest_age_ms) ==
           TM1650_HEIGHT_WAITING);
    assert(tm1650_height_cache_feed(&cache, 0x34, 0x9E, 404, 1500,
                                    &height_mm, &oldest_age_ms) ==
           TM1650_HEIGHT_VALID);
    assert(height_mm == 711);
    assert(oldest_age_ms == 4);
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
           TM1650_HEIGHT_VALID);
    assert(tm1650_height_decoder_feed(&decoder, 0x37, 0xC5, &height_mm) ==
           TM1650_HEIGHT_WAITING);
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
    /* Original panel imperial mode: 25.0 in and inferred 40.0 in. */
    expect_height(0x9E, 0xF3, 0x5F, 0x00, 635);
    expect_height(0xC5, 0x7F, 0x5F, 0x00, 1016);
    expect_invalid_height(0xBE, 0xD3, 0x5F);
    expect_invalid_height(0x9E, 0xD3, 0x7F);
    expect_missing_mirror_recovery();
    expect_fragmented_up_cache();
    expect_imperial_cache();
    expect_imperial_cache_generation_isolation();
    expect_malformed_order_rejected();

    puts("tm1650 height golden vectors: OK");
    return 0;
}
