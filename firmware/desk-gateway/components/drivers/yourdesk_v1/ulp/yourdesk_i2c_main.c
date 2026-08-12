/**
 * @file yourdesk_i2c_main.c
 * @brief ULP RISC-V worker for the yourdesk multi-address I2C slave.
 *
 * The main Xtensa cores also run Wi-Fi and NimBLE, so they cannot guarantee the
 * roughly 52 us half-cycle deadline of the controller bus. This worker owns
 * RTCIO GPIO4/5 and polls them on the independent 17.5 MHz ULP core. Only the
 * desired key byte and completed digit events cross RTC shared memory.
 */
#include "../yourdesk_soft_i2c_sampler.h"

#include "sdkconfig.h"
#include "ulp_riscv_gpio.h"

#include <stdbool.h>
#include <stdint.h>

#define DIGIT_RING_CAPACITY 32u
#define DIGIT_RING_MASK     (DIGIT_RING_CAPACITY - 1u)
#define BUS_LEVEL_SCL_HIGH  (1u << 0)
#define BUS_LEVEL_SDA_HIGH  (1u << 1)

/* Main CPU -> ULP command mailbox. */
volatile uint32_t desired_dr;
volatile uint32_t digit_read_seq;

/* ULP -> main CPU status mailbox. */
volatile uint32_t worker_ready;
volatile uint32_t active_dr;
volatile uint32_t digit_write_seq;
volatile uint32_t digit_ring[DIGIT_RING_CAPACITY];

/* Cumulative bus telemetry, sampled by the normal firmware task once a second. */
volatile uint32_t stat_scl_rising;
volatile uint32_t stat_scl_falling;
volatile uint32_t stat_starts;
volatile uint32_t stat_stops;
volatile uint32_t stat_key_write_addresses;
volatile uint32_t stat_key_read_addresses;
volatile uint32_t stat_completed_key_reads;
volatile uint32_t stat_aborted_key_reads;
volatile uint32_t stat_digit_write_addresses;
volatile uint32_t stat_unsupported_addresses;
volatile uint32_t stat_digit_events;
volatile uint32_t stat_digit_ring_drops;
volatile uint32_t state_phase;
volatile uint32_t state_bit_count;
volatile uint32_t state_current_addr7;

/** Publish the current parser location for low-rate main-CPU diagnostics. */
static void snapshot_state(const yourdesk_soft_i2c_sampler_t *sampler)
{
    state_phase = (uint32_t)sampler->sm.phase;
    state_bit_count = sampler->sm.bit_count;
    state_current_addr7 = sampler->sm.current_addr7;
    active_dr = sampler->sm.tx_dr;
}

/** Classify an address byte completed by the pure state machine. */
static void count_address(uint8_t raw_address)
{
    uint8_t addr7 = (uint8_t)(raw_address >> 1);
    bool read = (raw_address & 1u) != 0;
    if (addr7 == 0x24u) {
        if (read) {
            stat_key_read_addresses++;
        } else {
            stat_key_write_addresses++;
        }
    } else if (!read && addr7 >= 0x34u && addr7 <= 0x37u) {
        stat_digit_write_addresses++;
    } else {
        stat_unsupported_addresses++;
    }
}

/** Enqueue one digit event without ever blocking the timing-critical worker. */
static void publish_digit_event(const yourdesk_soft_i2c_digit_event_t *event)
{
    uint32_t write_seq = digit_write_seq;
    uint32_t read_seq = digit_read_seq;
    if (write_seq - read_seq >= DIGIT_RING_CAPACITY) {
        stat_digit_ring_drops++;
        return;
    }

    digit_ring[write_seq & DIGIT_RING_MASK] =
        ((uint32_t)event->addr7 << 8) | event->segment;
    /* Publish the payload before advancing the producer sequence. */
    __asm__ volatile("fence rw, rw" ::: "memory");
    digit_write_seq = write_seq + 1u;
    stat_digit_events++;
}

/** Pull DAT low or release it; the external 2 kOhm resistor supplies bus high. */
static void apply_sda_output(bool drive_low, bool *was_driving_low)
{
    if (drive_low == *was_driving_low) {
        return;
    }
    *was_driving_low = drive_low;
    if (drive_low) {
        ulp_riscv_gpio_output_enable(
            (gpio_num_t)CONFIG_DESK_I2C_SDA_GPIO);
    } else {
        ulp_riscv_gpio_output_disable(
            (gpio_num_t)CONFIG_DESK_I2C_SDA_GPIO);
    }
}

/** Configure GPIO4/5 for independent ULP input and open-drain output access. */
static void configure_bus_gpio(void)
{
    gpio_num_t scl = (gpio_num_t)CONFIG_DESK_I2C_SCL_GPIO;
    gpio_num_t sda = (gpio_num_t)CONFIG_DESK_I2C_SDA_GPIO;

    ulp_riscv_gpio_init(scl);
    ulp_riscv_gpio_input_enable(scl);
    ulp_riscv_gpio_output_disable(scl);
    ulp_riscv_gpio_pullup_disable(scl);
    ulp_riscv_gpio_pulldown_disable(scl);

    ulp_riscv_gpio_init(sda);
    ulp_riscv_gpio_input_enable(sda);
    /* Release SDA before programming its active-low output level. Otherwise the
     * ownership hand-off can briefly pull the bus low and create a false START. */
    ulp_riscv_gpio_output_disable(sda);
    ulp_riscv_gpio_set_output_mode(sda, RTCIO_MODE_OUTPUT_OD);
    /* Output-enable means pull low; output-disable means release. */
    ulp_riscv_gpio_output_level(sda, 0);
    ulp_riscv_gpio_pullup_disable(sda);
    ulp_riscv_gpio_pulldown_disable(sda);
}

/**
 * Read both bus lines from one RTC register snapshot.
 *
 * Reading GPIO4 and GPIO5 separately can observe different instants and costs
 * two RTC-register accesses in the timing-critical loop. A single snapshot
 * both preserves the CLK/DAT relationship and reduces the work required to
 * observe each roughly 52 us controller half-cycle.
 */
static inline __attribute__((always_inline)) uint32_t read_bus_levels(void)
{
    uint32_t rtc_levels = REG_GET_FIELD(RTC_GPIO_IN_REG, RTC_GPIO_IN_NEXT);
    uint32_t levels = 0;
    if ((rtc_levels & BIT(CONFIG_DESK_I2C_SCL_GPIO)) != 0) {
        levels |= BUS_LEVEL_SCL_HIGH;
    }
    if ((rtc_levels & BIT(CONFIG_DESK_I2C_SDA_GPIO)) != 0) {
        levels |= BUS_LEVEL_SDA_HIGH;
    }
    return levels;
}

int main(void)
{
    configure_bus_gpio();

    uint32_t previous_bus_levels = read_bus_levels();
    bool scl_high = (previous_bus_levels & BUS_LEVEL_SCL_HIGH) != 0;
    bool sda_high = (previous_bus_levels & BUS_LEVEL_SDA_HIGH) != 0;
    bool driving_sda_low = false;
    yourdesk_soft_i2c_sampler_t sampler;
    yourdesk_soft_i2c_sampler_init(&sampler, scl_high, sda_high,
                                   (uint8_t)desired_dr);
    snapshot_state(&sampler);
    worker_ready = 1u;

    for (;;) {
        uint32_t bus_levels = read_bus_levels();
        if (bus_levels == previous_bus_levels) {
            /* Stable lines must not enter the comparatively heavy parser. */
            continue;
        }
        previous_bus_levels = bus_levels;
        scl_high = (bus_levels & BUS_LEVEL_SCL_HIGH) != 0;
        sda_high = (bus_levels & BUS_LEVEL_SDA_HIGH) != 0;

        yourdesk_soft_i2c_sample_result_t result =
            yourdesk_soft_i2c_sampler_sample(&sampler, scl_high, sda_high,
                                              (uint8_t)desired_dr);
        if (result.scl_rising) {
            stat_scl_rising++;
        }
        if (result.scl_falling) {
            stat_scl_falling++;
        }
        if (result.start_detected) {
            stat_starts++;
        }
        if (result.stop_detected) {
            stat_stops++;
        }
        if (result.key_read_aborted) {
            stat_aborted_key_reads++;
        }
        if (result.address_completed) {
            count_address(result.raw_address);
        }
        if (result.protocol_event.key_read_completed) {
            stat_completed_key_reads++;
        }
        if (result.protocol_event.ready) {
            publish_digit_event(&result.protocol_event);
        }
        apply_sda_output(result.drive_sda_low, &driving_sda_low);

        if (result.scl_rising || result.scl_falling ||
            result.start_detected || result.stop_detected) {
            snapshot_state(&sampler);
        }
    }
    return 0;
}
