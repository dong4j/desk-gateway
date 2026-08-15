/**
 * @file mxtark_soft_i2c_sm_test.c
 * @brief Host-side waveform checks for the mxtark multi-address I2C slave.
 */
#include "mxtark_soft_i2c_sm.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/** Clock one master-written byte and return whether the slave ACKed it. */
static bool master_write_byte(mxtark_soft_i2c_sm_t *sm, uint8_t byte,
                              mxtark_soft_i2c_digit_event_t *event)
{
    for (uint8_t mask = 0x80; mask != 0; mask >>= 1) {
        mxtark_soft_i2c_sm_scl_rising(sm, (byte & mask) != 0);
        *event = mxtark_soft_i2c_sm_scl_falling(sm);
    }
    bool acked = sm->drive_sda_low;
    mxtark_soft_i2c_sm_scl_rising(sm, !sm->drive_sda_low);
    *event = mxtark_soft_i2c_sm_scl_falling(sm);
    return acked;
}

/** Read one DR response and return its completion marker to the test. */
static uint8_t master_read_dr(mxtark_soft_i2c_sm_t *sm,
                              mxtark_soft_i2c_digit_event_t *event)
{
    uint8_t value = 0;
    for (int bit = 0; bit < 8; ++bit) {
        /* A delayed SDA IRQ at this point must be consumed by the TX guard. */
        assert(mxtark_soft_i2c_sm_key_tx_active(sm));
        value = (uint8_t)((value << 1) | (sm->drive_sda_low ? 0u : 1u));
        mxtark_soft_i2c_sm_scl_rising(sm, !sm->drive_sda_low);
        *event = mxtark_soft_i2c_sm_scl_falling(sm);
    }
    assert(mxtark_soft_i2c_sm_key_tx_active(sm));
    assert(!sm->drive_sda_low);
    mxtark_soft_i2c_sm_scl_rising(sm, false); /* Controller ACKs, then STOPs. */
    *event = mxtark_soft_i2c_sm_scl_falling(sm);
    assert(!mxtark_soft_i2c_sm_key_tx_active(sm));
    return value;
}

/** Replay one complete controller poll and require one completion marker. */
static void key_poll_once(mxtark_soft_i2c_sm_t *sm, uint8_t expected_dr)
{
    mxtark_soft_i2c_digit_event_t event;

    mxtark_soft_i2c_sm_start(sm);
    assert(master_write_byte(sm, 0x48, &event)); /* 0x24 write */
    assert(master_write_byte(sm, 0x01, &event));
    assert(!event.ready);

    mxtark_soft_i2c_sm_start(sm); /* Repeated START */
    assert(master_write_byte(sm, 0x49, &event)); /* 0x24 read */
    assert(master_read_dr(sm, &event) == expected_dr);
    assert(event.key_read_completed);
    mxtark_soft_i2c_sm_stop(sm);
    assert(sm->phase == MXTARK_SOFT_I2C_IDLE);
}

/** Verify UP and DOWN produce an equal stream of completed controller polls. */
static void test_motion_poll_stream(void)
{
    mxtark_soft_i2c_sm_t sm;
    mxtark_soft_i2c_sm_init(&sm, 0x47);
    for (int i = 0; i < 512; ++i) {
        key_poll_once(&sm, 0x47);
    }
    mxtark_soft_i2c_sm_set_dr(&sm, 0x4F);
    for (int i = 0; i < 512; ++i) {
        key_poll_once(&sm, 0x4F);
    }
}

/** Verify all four digit endpoints ACK and emit their exact segment byte. */
static void test_digit_writes(void)
{
    static const uint8_t addresses[] = {0x36, 0x35, 0x34, 0x37};
    static const uint8_t segments[] = {0xC5, 0xDB, 0x00, 0xC5};
    mxtark_soft_i2c_sm_t sm;
    mxtark_soft_i2c_sm_init(&sm, 0x2E);

    for (size_t i = 0; i < sizeof(addresses); ++i) {
        mxtark_soft_i2c_digit_event_t event = {0};
        mxtark_soft_i2c_sm_start(&sm);
        assert(master_write_byte(&sm, (uint8_t)(addresses[i] << 1), &event));
        assert(master_write_byte(&sm, segments[i], &event));
        assert(event.ready);
        assert(event.addr7 == addresses[i]);
        assert(event.segment == segments[i]);
        mxtark_soft_i2c_sm_stop(&sm);
    }
}

/** Unknown and read-only digit addresses must be left unacknowledged. */
static void test_address_filter(void)
{
    mxtark_soft_i2c_sm_t sm;
    mxtark_soft_i2c_digit_event_t event;
    mxtark_soft_i2c_sm_init(&sm, 0x2E);

    mxtark_soft_i2c_sm_start(&sm);
    assert(!master_write_byte(&sm, 0x50, &event)); /* Unknown 0x28 write. */
    mxtark_soft_i2c_sm_stop(&sm);

    mxtark_soft_i2c_sm_start(&sm);
    assert(!master_write_byte(&sm, 0x69, &event)); /* Digit 0x34 read. */
    mxtark_soft_i2c_sm_stop(&sm);
}

int main(void)
{
    test_motion_poll_stream();
    test_digit_writes();
    test_address_filter();
    puts("mxtark soft I2C waveform vectors: OK");
    return 0;
}
