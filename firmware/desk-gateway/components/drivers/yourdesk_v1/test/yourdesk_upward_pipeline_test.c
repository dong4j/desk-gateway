/**
 * @file yourdesk_upward_pipeline_test.c
 * @brief End-to-end host replay for imperial height decoding and preset motion.
 *
 * The isolated decoder and safety tests previously passed while their runtime
 * coupling still stopped a real ascent early. This test replays the sparse
 * upper-travel sequence as one pipeline so decoder freshness and preset
 * stopping are verified together without any maximum-height intervention.
 */
#include "tm1650_height_decoder.h"
#include "yourdesk_preset_logic.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define STEP_SLACK_MM 20
#define PRESET_STOP_MARGIN_MM 5

typedef struct {
    uint32_t at_ms;
    uint8_t digits[4];
    int expected_mm;
} upward_sample_t;

/** Decode one complete controller-ordered TM1650 display frame. */
static int decode_frame(const uint8_t digits[4])
{
    tm1650_height_decoder_t decoder;
    tm1650_height_decoder_reset(&decoder);
    int height_mm = -1;

    assert(tm1650_height_decoder_feed(&decoder, 0x36, digits[2],
                                      &height_mm) ==
           TM1650_HEIGHT_WAITING);
    assert(tm1650_height_decoder_feed(&decoder, 0x35, digits[1],
                                      &height_mm) ==
           TM1650_HEIGHT_WAITING);
    assert(tm1650_height_decoder_feed(&decoder, 0x34, digits[0],
                                      &height_mm) ==
           TM1650_HEIGHT_VALID);
    assert(tm1650_height_decoder_feed(&decoder, 0x37, digits[3],
                                      &height_mm) ==
           TM1650_HEIGHT_WAITING);
    return height_mm;
}

/**
 * Replay the real upper-travel imperial cadence without permitting an early
 * feedback-driven stop. The final 40.0 in frame must satisfy the 102 cm preset.
 */
static void expect_sparse_imperial_ascent_reaches_preset(void)
{
    static const upward_sample_t samples[] = {
        {0,    {0xD6, 0x66, 0xD3, 0x00}, 953},
        {2540, {0xD6, 0xFF, 0xD3, 0x00}, 978},
        {3740, {0xD6, 0xF7, 0x5F, 0x00}, 991},
        {4940, {0xD6, 0xF7, 0xD3, 0x00}, 1003},
        {6140, {0xC5, 0x7F, 0x5F, 0x00}, 1016},
    };

    int previous_mm = -1;
    uint32_t previous_ms = 0;
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
        int height_mm = decode_frame(samples[i].digits);
        assert(height_mm == samples[i].expected_mm);

        if (previous_mm >= 0) {
            int elapsed_ms = (int)(samples[i].at_ms - previous_ms);
            assert(yourdesk_height_transition_valid(
                previous_mm, height_mm, elapsed_ms, YOURDESK_PRESET_UP,
                false, YOURDESK_HEIGHT_TRANSITION_MAX_SPEED_MM_PER_S,
                STEP_SLACK_MM));
        }
        previous_mm = height_mm;
        previous_ms = samples[i].at_ms;
    }

    assert(yourdesk_preset_reached(previous_mm, 1020,
                                   PRESET_STOP_MARGIN_MM,
                                   YOURDESK_PRESET_UP));
}

int main(void)
{
    expect_sparse_imperial_ascent_reaches_preset();
    puts("yourdesk upward pipeline vectors: OK");
    return 0;
}
