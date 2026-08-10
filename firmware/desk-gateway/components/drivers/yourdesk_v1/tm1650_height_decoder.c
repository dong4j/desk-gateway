/**
 * @file tm1650_height_decoder.c
 * @brief Pure TM1650 segment-to-height decoder for yourdesk_v1.
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
#define TM1650_ALL_DIGITS_MASK 0x0Fu

/* Captures consistently show one coherent refresh in this address order. */
static const uint8_t s_frame_order[4] = {
    TM1650_ADDR_DIG3,
    TM1650_ADDR_DIG2,
    TM1650_ADDR_DIG1,
    TM1650_ADDR_DIG4,
};

enum {
    SEGMENT_UNKNOWN = -2,
    SEGMENT_BLANK = -1,
};

/** Decode the exact segment values observed on the three numeric digits. */
static int decode_segment(uint8_t segment)
{
    switch (segment) {
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

    /* DIG3 is the first write in every clean captured display frame. */
    if (addr7 == TM1650_ADDR_DIG3) {
        tm1650_height_decoder_reset(decoder);
    }
    if (decoder->next_order_index >= 4 ||
        addr7 != s_frame_order[decoder->next_order_index]) {
        /* Never let adjacent fragments manufacture a plausible height. */
        tm1650_height_decoder_reset(decoder);
        return TM1650_HEIGHT_WAITING;
    }
    decoder->digits[index] = segment;
    decoder->received_mask |= (uint8_t)(1u << index);
    decoder->next_order_index++;
    if (decoder->received_mask != TM1650_ALL_DIGITS_MASK) {
        return TM1650_HEIGHT_WAITING;
    }

    int hundreds = decode_segment(decoder->digits[0]);
    int tens = decode_segment(decoder->digits[1]);
    int ones = decode_segment(decoder->digits[2]);
    if (hundreds == SEGMENT_UNKNOWN || tens < 0 || ones < 0) {
        return TM1650_HEIGHT_INVALID;
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
