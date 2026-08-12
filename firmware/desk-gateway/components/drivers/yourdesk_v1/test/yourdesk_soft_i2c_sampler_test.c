/**
 * @file yourdesk_soft_i2c_sampler_test.c
 * @brief Host waveform tests for the ULP sampled-line I2C frontend.
 */
#include "yourdesk_soft_i2c_sampler.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    yourdesk_soft_i2c_sampler_t sampler;
    bool master_scl_high;
    bool master_sda_high;
    bool slave_drives_sda_low;
    uint8_t desired_dr;
    uint32_t completed_key_reads;
    uint32_t digit_events;
    yourdesk_soft_i2c_digit_event_t last_digit_event;
} sampled_bus_t;

/** Feed one physical bus level into the sampler and apply its open-drain output. */
static yourdesk_soft_i2c_sample_result_t sample_bus(sampled_bus_t *bus)
{
    bool physical_sda_high =
        bus->master_sda_high && !bus->slave_drives_sda_low;
    yourdesk_soft_i2c_sample_result_t result =
        yourdesk_soft_i2c_sampler_sample(&bus->sampler,
                                         bus->master_scl_high,
                                         physical_sda_high,
                                         bus->desired_dr);
    bool output_changed = bus->slave_drives_sda_low != result.drive_sda_low;
    bus->slave_drives_sda_low = result.drive_sda_low;
    if (result.protocol_event.key_read_completed) {
        bus->completed_key_reads++;
    }
    if (result.protocol_event.ready) {
        bus->digit_events++;
        bus->last_digit_event = result.protocol_event;
    }

    /* Let the next sample observe a slave-generated DAT transition during CLK low. */
    if (output_changed && !bus->master_scl_high) {
        physical_sda_high =
            bus->master_sda_high && !bus->slave_drives_sda_low;
        (void)yourdesk_soft_i2c_sampler_sample(&bus->sampler,
                                               false,
                                               physical_sda_high,
                                               bus->desired_dr);
    }
    return result;
}

static void set_bus(sampled_bus_t *bus, bool scl_high, bool sda_high)
{
    bus->master_scl_high = scl_high;
    bus->master_sda_high = sda_high;
    (void)sample_bus(bus);
}

static bool physical_sda_high(const sampled_bus_t *bus)
{
    return bus->master_sda_high && !bus->slave_drives_sda_low;
}

static void start_condition(sampled_bus_t *bus)
{
    set_bus(bus, true, true);
    set_bus(bus, true, false);
}

static void repeated_start(sampled_bus_t *bus)
{
    set_bus(bus, false, true);
    set_bus(bus, true, true);
    set_bus(bus, true, false);
}

static void stop_condition(sampled_bus_t *bus)
{
    set_bus(bus, false, false);
    set_bus(bus, true, false);
    set_bus(bus, true, true);
}

/** Clock one controller-written byte and return the slave ACK level. */
static bool write_byte(sampled_bus_t *bus, uint8_t byte)
{
    for (uint8_t mask = 0x80; mask != 0; mask >>= 1) {
        set_bus(bus, false, (byte & mask) != 0);
        set_bus(bus, true, (byte & mask) != 0);
        set_bus(bus, false, (byte & mask) != 0);
    }
    set_bus(bus, false, true);
    set_bus(bus, true, true);
    bool acked = !physical_sda_high(bus);
    set_bus(bus, false, true);
    return acked;
}

/** Read the slave DR byte and finish with the controller's ACK clock. */
static uint8_t read_dr(sampled_bus_t *bus)
{
    uint8_t value = 0;
    bus->master_sda_high = true;
    for (int bit = 0; bit < 8; ++bit) {
        set_bus(bus, true, true);
        value = (uint8_t)((value << 1) |
                          (physical_sda_high(bus) ? 1u : 0u));
        set_bus(bus, false, true);
    }
    set_bus(bus, false, false);
    set_bus(bus, true, false);
    set_bus(bus, false, false);
    return value;
}

static void key_poll_once(sampled_bus_t *bus, uint8_t expected_dr)
{
    uint32_t completed_before = bus->completed_key_reads;
    start_condition(bus);
    assert(write_byte(bus, 0x48));
    assert(write_byte(bus, 0x01));
    repeated_start(bus);
    assert(write_byte(bus, 0x49));
    assert(read_dr(bus) == expected_dr);
    stop_condition(bus);
    assert(bus->completed_key_reads == completed_before + 1);
}

/** Verify stable line samples cannot manufacture protocol edges or bus output. */
static void test_duplicate_samples_are_inert(void)
{
    sampled_bus_t bus = {
        .master_scl_high = true,
        .master_sda_high = true,
        .desired_dr = 0x2E,
    };
    yourdesk_soft_i2c_sampler_init(&bus.sampler, true, true, bus.desired_dr);

    for (int i = 0; i < 1024; ++i) {
        yourdesk_soft_i2c_sample_result_t result = sample_bus(&bus);
        assert(!result.scl_rising);
        assert(!result.scl_falling);
        assert(!result.start_detected);
        assert(!result.stop_detected);
        assert(!result.address_completed);
        assert(!result.key_read_aborted);
        assert(!result.protocol_event.ready);
        assert(!result.protocol_event.key_read_completed);
        assert(!result.drive_sda_low);
    }
    assert(bus.completed_key_reads == 0);
    assert(bus.digit_events == 0);
}

/** Require uninterrupted UP/DOWN polling through the sampled-line frontend. */
static void test_motion_stream(void)
{
    sampled_bus_t bus = {
        .master_scl_high = true,
        .master_sda_high = true,
        .desired_dr = 0x47,
    };
    yourdesk_soft_i2c_sampler_init(&bus.sampler, true, true, bus.desired_dr);

    for (int i = 0; i < 512; ++i) {
        key_poll_once(&bus, 0x47);
    }
    bus.desired_dr = 0x4F;
    for (int i = 0; i < 512; ++i) {
        key_poll_once(&bus, 0x4F);
    }
    assert(bus.completed_key_reads == 1024);
}

/** Require all four digit addresses to survive the same sampled-line path. */
static void test_digit_stream(void)
{
    static const uint8_t addresses[] = {0x34, 0x35, 0x36, 0x37};
    static const uint8_t segments[] = {0x00, 0xDB, 0xC5, 0xC5};
    sampled_bus_t bus = {
        .master_scl_high = true,
        .master_sda_high = true,
        .desired_dr = 0x2E,
    };
    yourdesk_soft_i2c_sampler_init(&bus.sampler, true, true, bus.desired_dr);

    for (size_t i = 0; i < sizeof(addresses); ++i) {
        start_condition(&bus);
        assert(write_byte(&bus, (uint8_t)(addresses[i] << 1)));
        assert(write_byte(&bus, segments[i]));
        stop_condition(&bus);
        assert(bus.last_digit_event.addr7 == addresses[i]);
        assert(bus.last_digit_event.segment == segments[i]);
    }
    assert(bus.digit_events == 4);
}

int main(void)
{
    test_duplicate_samples_are_inert();
    test_motion_stream();
    test_digit_stream();
    puts("yourdesk ULP sampled-line vectors: OK");
    return 0;
}
