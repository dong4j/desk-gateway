/**
 * @file tm1650_height_decoder.c
 * @brief Pure TM1650 segment-to-height decoder for mxtark.
 *
 * Segment values come from archived captures, including the monotonic
 * 77 -> 64 sequence in preset_1_goto.sr. Unknown codes are rejected instead
 * of guessed because a wrong displayed height is worse than an unknown one.
 */
#include "tm1650_height_decoder.h"

#include <stdbool.h>
#include <stddef.h>

#define TM1650_ADDR_DIG1 0x34u
#define TM1650_ADDR_DIG2 0x35u
#define TM1650_ADDR_DIG3 0x36u
#define TM1650_ADDR_DIG4 0x37u
#define TM1650_HEIGHT_DIGITS_MASK 0x07u
#define TM1650_SEGMENT_DECIMAL_POINT 0x20u

/*
 * The three numeric digits are written first in captured refreshes. DIG4 is a
 * mirror/status digit and can lag while the desk moves, so it must not block a
 * fresh height from being published.
 */
static const uint8_t s_frame_order[3] = {
    TM1650_ADDR_DIG3,
    TM1650_ADDR_DIG2,
    TM1650_ADDR_DIG1,
};

enum {
    SEGMENT_UNKNOWN = -2,
    SEGMENT_BLANK = -1,
};

/** Decode the exact segment values observed on the three numeric digits. */
static int decode_segment(uint8_t segment)
{
    /* The panel adds bit 0x20 to the digit preceding the decimal point. */
    switch (segment & (uint8_t)~TM1650_SEGMENT_DECIMAL_POINT) {
    case 0x00:
        return SEGMENT_BLANK;
    case 0x5F:
        return 0;
    case 0x44:
        return 1;
    case 0x9E:
        return 2;
    case 0xD6:
        return 3;
    case 0xC5:
        return 4;
    case 0xD3:
        return 5;
    case 0xDB:
        return 6;
    case 0x46:
        return 7;
    case 0xDF:
        return 8;
    case 0xD7:
        return 9;
    default:
        return SEGMENT_UNKNOWN;
    }
}

/** Convert a 7-bit digit address into the decoder's DIG1-based array index. */
static int digit_index(uint8_t addr7)
{
    if (addr7 < TM1650_ADDR_DIG1 || addr7 > TM1650_ADDR_DIG4) {
        return -1;
    }
    return (int)(addr7 - TM1650_ADDR_DIG1);
}

/** Decode the three numeric digit registers into millimetres. */
static tm1650_height_result_t decode_height_digits(const uint8_t digits[3],
                                                   int *out_height_mm)
{
    int hundreds = decode_segment(digits[0]);
    int tens = decode_segment(digits[1]);
    int ones = decode_segment(digits[2]);
    if (hundreds == SEGMENT_UNKNOWN || tens < 0 || ones < 0) {
        return TM1650_HEIGHT_INVALID;
    }

    bool hundreds_decimal =
        (digits[0] & TM1650_SEGMENT_DECIMAL_POINT) != 0;
    bool tens_decimal = (digits[1] & TM1650_SEGMENT_DECIMAL_POINT) != 0;
    bool ones_decimal = (digits[2] & TM1650_SEGMENT_DECIMAL_POINT) != 0;
    if (hundreds_decimal || ones_decimal) {
        /* Only xx.x inch has been observed; other positions are corruption. */
        return TM1650_HEIGHT_INVALID;
    }

    if (tens_decimal) {
        if (hundreds < 0) {
            return TM1650_HEIGHT_INVALID;
        }
        int tenths_inches = hundreds * 100 + tens * 10 + ones;
        /* Convert 0.1 inch to the nearest millimetre without floating point. */
        int height_mm = (tenths_inches * 254 + 50) / 100;
        if (height_mm < 400 || height_mm > 2000) {
            return TM1650_HEIGHT_INVALID;
        }
        *out_height_mm = height_mm;
        return TM1650_HEIGHT_VALID;
    }

    int height_cm = hundreds == SEGMENT_BLANK
                        ? tens * 10 + ones
                        : hundreds * 100 + tens * 10 + ones;
    /* Reject corrupted but decodable furniture heights. */
    if (height_cm < 40 || height_cm > 200) {
        return TM1650_HEIGHT_INVALID;
    }

    *out_height_mm = height_cm * 10;
    return TM1650_HEIGHT_VALID;
}

void tm1650_height_decoder_reset(tm1650_height_decoder_t *decoder)
{
    if (!decoder) {
        return;
    }
    decoder->received_mask = 0;
    decoder->next_order_index = 0;
    for (size_t i = 0; i < 4; ++i) {
        decoder->digits[i] = 0;
    }
}

tm1650_height_result_t tm1650_height_decoder_feed(tm1650_height_decoder_t *decoder,
                                                   uint8_t addr7,
                                                   uint8_t segment,
                                                   int *out_height_mm)
{
    if (!decoder || !out_height_mm) {
        return TM1650_HEIGHT_INVALID;
    }

    int index = digit_index(addr7);
    if (index < 0) {
        return TM1650_HEIGHT_WAITING;
    }

    if (addr7 == TM1650_ADDR_DIG4) {
        /* A delayed mirror may interleave the next numeric refresh. Ignore it. */
        decoder->digits[index] = segment;
        return TM1650_HEIGHT_WAITING;
    }

    /* DIG3 is the first write in every clean captured display frame. */
    if (addr7 == TM1650_ADDR_DIG3) {
        tm1650_height_decoder_reset(decoder);
    }
    if (decoder->next_order_index >= 3 ||
        addr7 != s_frame_order[decoder->next_order_index]) {
        /* Never let adjacent fragments manufacture a plausible height. */
        tm1650_height_decoder_reset(decoder);
        return TM1650_HEIGHT_WAITING;
    }
    decoder->digits[index] = segment;
    decoder->received_mask |= (uint8_t)(1u << index);
    decoder->next_order_index++;
    if ((decoder->received_mask & TM1650_HEIGHT_DIGITS_MASK) !=
        TM1650_HEIGHT_DIGITS_MASK) {
        return TM1650_HEIGHT_WAITING;
    }

    return decode_height_digits(decoder->digits, out_height_mm);
}

void tm1650_height_cache_reset(tm1650_height_cache_t *cache)
{
    if (!cache) {
        return;
    }
    cache->received_mask = 0;
    for (size_t i = 0; i < 3; ++i) {
        cache->digits[i] = 0;
        cache->seen_ms[i] = 0;
    }
}

tm1650_height_result_t tm1650_height_cache_feed(
    tm1650_height_cache_t *cache, uint8_t addr7, uint8_t segment,
    uint32_t now_ms, uint32_t max_age_ms, int *out_height_mm,
    uint32_t *out_oldest_age_ms)
{
    if (!cache || !out_height_mm || !out_oldest_age_ms) {
        return TM1650_HEIGHT_INVALID;
    }
    int index = digit_index(addr7);
    if (index < 0 || index >= 3) {
        return TM1650_HEIGHT_WAITING;
    }

    int value = decode_segment(segment);
    bool decimal = (segment & TM1650_SEGMENT_DECIMAL_POINT) != 0;
    if (value == SEGMENT_UNKNOWN || (index > 0 && value == SEGMENT_BLANK) ||
        (decimal && index != 1)) {
        /* Corrupted observations must not evict the last valid digit. */
        return TM1650_HEIGHT_INVALID;
    }
    cache->digits[index] = segment;
    cache->seen_ms[index] = now_ms;
    cache->received_mask |= (uint8_t)(1u << index);
    if (cache->received_mask != TM1650_HEIGHT_DIGITS_MASK) {
        return TM1650_HEIGHT_WAITING;
    }

    uint32_t oldest_age_ms = 0;
    for (size_t i = 0; i < 3; ++i) {
        uint32_t age_ms = now_ms - cache->seen_ms[i];
        if (age_ms > max_age_ms) {
            return TM1650_HEIGHT_WAITING;
        }
        if (age_ms > oldest_age_ms) {
            oldest_age_ms = age_ms;
        }
    }

    tm1650_height_result_t result =
        decode_height_digits(cache->digits, out_height_mm);
    /*
     * A decoded value consumes the current three-digit generation.  Keeping
     * received_mask set would let one digit from the next refresh combine
     * with two older digits.  In imperial mode that turned 27.5 -> 28.0 into
     * a fabricated 28.5 value, which then caused the direction filter to
     * reject the real 28.0 frame.
     */
    cache->received_mask = 0;
    if (result == TM1650_HEIGHT_VALID) {
        *out_oldest_age_ms = oldest_age_ms;
    }
    return result;
}
