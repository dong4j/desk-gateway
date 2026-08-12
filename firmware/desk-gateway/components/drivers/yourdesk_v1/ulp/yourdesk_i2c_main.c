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
#include "ulp_riscv_utils.h"

#include <stdbool.h>
#include <stdint.h>

#define DIGIT_RING_CAPACITY 32u
#define DIGIT_RING_MASK     (DIGIT_RING_CAPACITY - 1u)
#define BUS_LEVEL_SCL_HIGH  (1u << 0)
#define BUS_LEVEL_SDA_HIGH  (1u << 1)
#define EDGE_FLAG_SCL_RISING        (1u << 0)
#define EDGE_FLAG_SCL_FALLING       (1u << 1)
#define EDGE_FLAG_START             (1u << 2)
#define EDGE_FLAG_STOP              (1u << 3)
#define EDGE_FLAG_ADDRESS           (1u << 4)
#define EDGE_FLAG_KEY_READ_COMPLETE (1u << 5)
#define EDGE_FLAG_DIGIT_EVENT       (1u << 6)
#define EDGE_FLAG_KEY_READ_ABORTED  (1u << 7)
#define EDGE_RAW_ADDRESS_SHIFT      8u
#define EDGE_DIGIT_ADDRESS_SHIFT    16u
#define EDGE_DIGIT_SEGMENT_SHIFT    24u

/* The controller bus is about 9.6 kHz, leaving roughly 52 us (about 910 ULP
 * cycles) between adjacent CLK edges. Keep a small margin for RTCIO latency. */
#define EDGE_DEADLINE_CYCLES 900u

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
volatile uint32_t stat_max_sda_apply_cycles;
volatile uint32_t stat_max_edge_cycles;
volatile uint32_t stat_edge_deadline_misses;
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
static inline __attribute__((always_inline)) void
apply_sda_output(bool drive_low, bool *was_driving_low)
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

/** Return whether an interrupted transaction was sending the key byte. */
static inline __attribute__((always_inline)) bool
key_read_in_progress(const yourdesk_soft_i2c_sm_t *sm)
{
    return sm->phase == YOURDESK_SOFT_I2C_TX_DATA ||
           sm->phase == YOURDESK_SOFT_I2C_TX_MASTER_ACK;
}

/**
 * Advance the pure protocol state machine for one physical line transition.
 *
 * The generic sampled-line frontend intentionally returns a rich structure for
 * host tests, but that ABI creates stack clearing and memcpy work on ULP. This
 * compact word keeps the same protocol decisions while allowing DAT to be
 * updated before diagnostics run. The response byte is latched only on START,
 * so a main-CPU mailbox update cannot tear an in-flight controller read.
 */
static inline __attribute__((always_inline)) uint32_t
process_bus_edge(yourdesk_soft_i2c_sampler_t *sampler,
                 bool scl_high,
                 bool sda_high)
{
    uint32_t flags = 0;

    if (scl_high != sampler->previous_scl_high) {
        if (scl_high) {
            flags |= EDGE_FLAG_SCL_RISING;
            yourdesk_soft_i2c_sm_scl_rising(&sampler->sm, sda_high);
        } else {
            flags |= EDGE_FLAG_SCL_FALLING;
            yourdesk_soft_i2c_phase_t phase_before = sampler->sm.phase;
            uint8_t bit_count_before = sampler->sm.bit_count;
            uint8_t raw_address = sampler->sm.rx_byte;
            yourdesk_soft_i2c_digit_event_t event =
                yourdesk_soft_i2c_sm_scl_falling(&sampler->sm);

            if (phase_before == YOURDESK_SOFT_I2C_RX_ADDRESS &&
                bit_count_before == 8) {
                flags |= EDGE_FLAG_ADDRESS |
                         ((uint32_t)raw_address << EDGE_RAW_ADDRESS_SHIFT);
            }
            if (event.key_read_completed) {
                flags |= EDGE_FLAG_KEY_READ_COMPLETE;
            }
            if (event.ready) {
                flags |= EDGE_FLAG_DIGIT_EVENT |
                         ((uint32_t)event.addr7 << EDGE_DIGIT_ADDRESS_SHIFT) |
                         ((uint32_t)event.segment << EDGE_DIGIT_SEGMENT_SHIFT);
            }
        }
    } else if (scl_high && sda_high != sampler->previous_sda_high) {
        if (key_read_in_progress(&sampler->sm)) {
            flags |= EDGE_FLAG_KEY_READ_ABORTED;
        }
        if (sda_high) {
            flags |= EDGE_FLAG_STOP;
            yourdesk_soft_i2c_sm_stop(&sampler->sm);
        } else {
            uint8_t next_dr = (uint8_t)desired_dr;
            if (sampler->sm.tx_dr != next_dr) {
                yourdesk_soft_i2c_sm_set_dr(&sampler->sm, next_dr);
            }
            flags |= EDGE_FLAG_START;
            yourdesk_soft_i2c_sm_start(&sampler->sm);
        }
    }

    sampler->previous_scl_high = scl_high;
    sampler->previous_sda_high = sda_high;
    return flags;
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
    yourdesk_soft_i2c_digit_event_t pending_digit_event = {0};
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

        uint32_t edge_start_cycles = ulp_riscv_get_cpu_cycles();
        uint32_t flags = process_bus_edge(&sampler, scl_high, sda_high);

        /* The controller samples ACK/data on the next rising edge. Apply the
         * open-drain result before touching shared telemetry or digit queues. */
        apply_sda_output(sampler.sm.drive_sda_low, &driving_sda_low);
        uint32_t sda_apply_cycles =
            ulp_riscv_get_cpu_cycles() - edge_start_cycles;
        if (sda_apply_cycles > stat_max_sda_apply_cycles) {
            stat_max_sda_apply_cycles = sda_apply_cycles;
        }

        if ((flags & EDGE_FLAG_SCL_RISING) != 0) {
            stat_scl_rising++;
        }
        if ((flags & EDGE_FLAG_SCL_FALLING) != 0) {
            stat_scl_falling++;
        }
        if ((flags & EDGE_FLAG_START) != 0) {
            stat_starts++;
        }
        if ((flags & EDGE_FLAG_STOP) != 0) {
            stat_stops++;
        }
        if ((flags & EDGE_FLAG_KEY_READ_ABORTED) != 0) {
            stat_aborted_key_reads++;
        }
        if ((flags & EDGE_FLAG_ADDRESS) != 0) {
            count_address((uint8_t)(flags >> EDGE_RAW_ADDRESS_SHIFT));
        }
        if ((flags & EDGE_FLAG_KEY_READ_COMPLETE) != 0) {
            stat_completed_key_reads++;
        }
        if ((flags & EDGE_FLAG_DIGIT_EVENT) != 0) {
            /* Publishing touches shared ring memory and is deferred until STOP
             * so the ninth data clock cannot delay the following bus edge. */
            pending_digit_event = (yourdesk_soft_i2c_digit_event_t){
                .ready = true,
                .key_read_completed = false,
                .addr7 = (uint8_t)(flags >> EDGE_DIGIT_ADDRESS_SHIFT),
                .segment = (uint8_t)(flags >> EDGE_DIGIT_SEGMENT_SHIFT),
            };
        }
        if ((flags & EDGE_FLAG_STOP) != 0 && pending_digit_event.ready) {
            publish_digit_event(&pending_digit_event);
            pending_digit_event.ready = false;
        }

        if ((flags & (EDGE_FLAG_START | EDGE_FLAG_STOP | EDGE_FLAG_ADDRESS |
                      EDGE_FLAG_KEY_READ_COMPLETE | EDGE_FLAG_DIGIT_EVENT)) !=
            0) {
            snapshot_state(&sampler);
        }

        uint32_t edge_cycles =
            ulp_riscv_get_cpu_cycles() - edge_start_cycles;
        if (edge_cycles > stat_max_edge_cycles) {
            stat_max_edge_cycles = edge_cycles;
        }
        if (edge_cycles > EDGE_DEADLINE_CYCLES) {
            stat_edge_deadline_misses++;
        }
    }
    return 0;
}
