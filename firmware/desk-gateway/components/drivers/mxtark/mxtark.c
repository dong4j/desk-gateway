/**
 * @file mxtark.c
 * @brief mxtark：通过硬件 I²C Slave 稳定应答按键 DR
 *
 * 超时/童锁在 desk_core；本文件维护当前 DR、应答主机轮询。产品默认路径只用
 * ESP32-S3 硬件 I²C Slave 响应 0x24。软件多地址和 GPIO 高度嗅探仅保留为
 * 协议实验，不得进入正常控桌固件；产品路径使用 TOF400C 高度与 TOF050C
 * 右侧距离执行档位闭环和上升安全限制。
 * 键码契约见 docs/architecture/protocol-reverse-notes.md §18。
 */
#include "mxtark.h"
#include "mxtark_panel_arbiter.h"
#include "mxtark_preset_logic.h"

#include "desk_tof.h"
#include "driver/i2c_slave.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <inttypes.h>
#include <limits.h>
#include <stdatomic.h>

#if CONFIG_DESK_MXTARK_SOFT_I2C_MULTI_ADDRESS && \
    CONFIG_DESK_MXTARK_HEIGHT_SNIFFER_EXPERIMENTAL
#error "software multi-address I2C and passive GPIO sniffer are mutually exclusive"
#endif

#if CONFIG_DESK_MXTARK_SOFT_I2C_MULTI_ADDRESS || \
    CONFIG_DESK_MXTARK_HEIGHT_SNIFFER_EXPERIMENTAL
#define MXTARK_HEIGHT_INPUT_ENABLED 1
#include "tm1650_height_decoder.h"
#include "mxtark_soft_i2c_sm.h"
#else
#define MXTARK_HEIGHT_INPUT_ENABLED 0
#endif

#if CONFIG_DESK_TOF_ENABLE || MXTARK_HEIGHT_INPUT_ENABLED
#define MXTARK_CLOSED_LOOP_ENABLED 1
#else
#define MXTARK_CLOSED_LOOP_ENABLED 0
#endif

#if CONFIG_DESK_MXTARK_SOFT_I2C_MULTI_ADDRESS
#include "mxtark_soft_i2c_esp.h"
#endif

#if CONFIG_DESK_MXTARK_PANEL_PROXY
#include "mxtark_panel_proxy.h"
#endif

#if CONFIG_DESK_MXTARK_HEIGHT_SNIFFER_EXPERIMENTAL
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_intr_alloc.h"

/* The experimental sniffer remains active while Flash cache is disabled. */
#ifndef CONFIG_GPIO_CTRL_FUNC_IN_IRAM
#error "mxtark height sniffer requires CONFIG_GPIO_CTRL_FUNC_IN_IRAM=y"
#endif
#endif

static const char *TAG = "mxtark";

#define ADDR_KEY_7BIT  0x24u
#define DR_IDLE        0x2Eu
#define DR_UP          0x47u
#define DR_DOWN        0x4Fu
#define DR_RESET       0x7Fu
#define DR_P1_GOTO     0x17u
#define DR_P1_SAVE     0x57u
#define DR_P4_GOTO     0x2Fu
#define DR_P4_SAVE     0x6Fu

/* Stop slightly inside the target to absorb normal motor and protocol latency. */
#define PRESET_STOP_MARGIN_MM 5
/* 静止实测抖动为 6 mm；重新点击同一档位时留出额外回差，避免反向点动。 */
#define PRESET_START_MARGIN_MM 8
/* 停车后等待五个 100 ms 测距周期，再使用稳定高度决定是否校正。 */
#define PRESET_SETTLE_MS 500
#define PRESET_SETTLE_TOLERANCE_MM 5
/* 校正必须有界；传感器或机械异常时不能在目标两侧持续振荡。 */
#define PRESET_MAX_CORRECTIONS 2U

/*
 * A clean TM1650 refresh is about 7 ms. The wider 20 ms window tolerates task
 * scheduling but still prevents unrelated motor-noise fragments being merged.
 */
#define HEIGHT_FRAME_WINDOW_MS          20
/* Capture replay shows valid upward digit fragments remain useful for 1.5 s. */
#define HEIGHT_DIGIT_CACHE_MS           1500
#define HEIGHT_STEP_SLACK_MM            20
#define HEIGHT_SAFETY_POLL_MS           50
#define HEIGHT_MOTION_DIAG_INTERVAL_MS  1000

#if MXTARK_HEIGHT_INPUT_ENABLED
#define ADDR_DIG1_7BIT 0x34u
#define ADDR_DIG4_7BIT 0x37u
#endif

typedef struct {
#if !CONFIG_DESK_MXTARK_SOFT_I2C_MULTI_ADDRESS
    i2c_slave_dev_handle_t handle;
    QueueHandle_t tx_q;
#endif
#if MXTARK_HEIGHT_INPUT_ENABLED
    QueueHandle_t digit_q;
#endif
} slave_ctx_t;

#if CONFIG_DESK_MXTARK_HEIGHT_SNIFFER_EXPERIMENTAL
/** ISR-only I²C frame state; the listener never drives CLK or DAT. */
typedef struct {
    bool in_frame;
    uint8_t bit_index;
    uint8_t byte_accumulator;
    uint8_t byte_count;
    uint8_t bytes[2];
} bus_sniffer_state_t;
#endif

static slave_ctx_t s_ctx;
static atomic_uint_fast8_t s_dr;
static esp_err_t set_dr(uint8_t dr);
static void publish_controller_dr(uint8_t dr);

#if CONFIG_DESK_MXTARK_PANEL_PROXY
static mxtark_panel_arbiter_t s_panel_arbiter;
static portMUX_TYPE s_panel_arbiter_mux = portMUX_INITIALIZER_UNLOCKED;
static atomic_bool s_panel_active;
static atomic_bool s_panel_connected;
#endif

#if MXTARK_CLOSED_LOOP_ENABLED
static atomic_int s_max_height_mm;
static atomic_int s_preset1_height_mm;
static atomic_int s_preset4_height_mm;
static atomic_int s_preset_target_mm;
static atomic_int s_preset_direction;
static mxtark_preset_control_t s_preset_control;
static portMUX_TYPE s_preset_control_mux = portMUX_INITIALIZER_UNLOCKED;

/** Cancel closed-loop preset tracking before any manual or explicit stop command. */
static void cancel_preset_motion(void)
{
    portENTER_CRITICAL(&s_preset_control_mux);
    mxtark_preset_control_reset(&s_preset_control);
    atomic_store(&s_preset_target_mm, -1);
    atomic_store(&s_preset_direction, 0);
    portEXIT_CRITICAL(&s_preset_control_mux);
}

#if CONFIG_DESK_TOF_ENABLE
/** Install one new ToF target and ignore the height sample used to choose direction. */
static void start_preset_motion(int target_mm,
                                mxtark_preset_direction_t direction,
                                uint32_t current_sample_id)
{
    portENTER_CRITICAL(&s_preset_control_mux);
    mxtark_preset_control_start(&s_preset_control, target_mm, direction,
                                current_sample_id);
    atomic_store(&s_preset_target_mm, target_mm);
    atomic_store(&s_preset_direction, direction);
    portEXIT_CRITICAL(&s_preset_control_mux);
}
#endif
#endif

#if CONFIG_DESK_TOF_ENABLE
/** Read the already-filtered ToF snapshot used by every upward safety path. */
static bool tof_upward_blocked(desk_tof_snapshot_t snapshot)
{
    /* 最高位保护与档位停车共用低延迟高度，不能继续落后两个测距周期。 */
    int safety_height_mm = snapshot.control_height_mm >= 0
                               ? snapshot.control_height_mm
                               : snapshot.height_mm;
    return mxtark_tof_upward_blocked(
        snapshot.height_known, safety_height_mm,
        snapshot.right_gap_known, snapshot.right_gap_mm,
        atomic_load(&s_max_height_mm));
}

/** Log one rejected/stopped UP command with both sensor inputs. */
static void log_tof_upward_block(const char *source,
                                 desk_tof_snapshot_t snapshot)
{
    ESP_LOGW(TAG,
             "block upward source=%s stable=%d control=%d known=%d right_gap=%d known=%d max=%d",
             source, snapshot.height_mm, snapshot.control_height_mm,
             (int)snapshot.height_known,
             snapshot.right_gap_mm, (int)snapshot.right_gap_known,
             atomic_load(&s_max_height_mm));
}
#endif

#if MXTARK_HEIGHT_INPUT_ENABLED
static atomic_int s_height_mm;
static atomic_uint_fast32_t s_motion_epoch;
static atomic_uint_fast32_t s_height_epoch;
static atomic_uint_fast32_t s_height_tick;

/** Convert a wrapped FreeRTOS tick delta into bounded milliseconds. */
static int elapsed_ms_since(uint_fast32_t then, TickType_t now)
{
    if (then == 0) {
        return INT_MAX;
    }
    uint32_t elapsed_ticks = (uint32_t)(now - (TickType_t)then);
    uint64_t elapsed_ms = (uint64_t)elapsed_ticks * portTICK_PERIOD_MS;
    return elapsed_ms > INT_MAX ? INT_MAX : (int)elapsed_ms;
}

/** Return the current command direction for height plausibility checks. */
static mxtark_preset_direction_t current_height_direction(void)
{
    uint8_t dr = (uint8_t)atomic_load(&s_dr);
    if (dr == DR_UP) {
        return MXTARK_PRESET_UP;
    }
    if (dr == DR_DOWN) {
        return MXTARK_PRESET_DOWN;
    }
    return MXTARK_PRESET_STOP;
}

/** Require the next complete controller frame to establish a fresh baseline. */
static void begin_height_resync(void)
{
    atomic_fetch_add(&s_motion_epoch, 1);
}

#if CONFIG_DESK_MOTION_DIAGNOSTICS && \
    CONFIG_DESK_MXTARK_SOFT_I2C_MULTI_ADDRESS
/** Convert the fixed-frequency ESP32-S3 cycle counter used by ISR telemetry. */
static uint32_t motion_diag_cycles_to_us(uint32_t cycles)
{
    return cycles / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
}

/** Name an incomplete software-I2C phase without reading mutable ISR state. */
static const char *motion_diag_phase_name(uint8_t phase)
{
    switch ((mxtark_soft_i2c_phase_t)phase) {
    case MXTARK_SOFT_I2C_IDLE:
        return "idle";
    case MXTARK_SOFT_I2C_RX_ADDRESS:
        return "rx_addr";
    case MXTARK_SOFT_I2C_RX_DATA:
        return "rx_data";
    case MXTARK_SOFT_I2C_TX_DATA:
        return "tx_data";
    case MXTARK_SOFT_I2C_TX_MASTER_ACK:
        return "tx_ack";
    case MXTARK_SOFT_I2C_IGNORE:
    default:
        return "ignore";
    }
}

/**
 * Print one interval of controller polling without influencing motion state.
 * key_addr is write/read; key_tx is started/completed.
 */
static void motion_bus_diag_log(
    const char *stage,
    uint8_t dr,
    TickType_t interval_tick,
    TickType_t now,
    const mxtark_soft_i2c_stats_t *baseline,
    const mxtark_soft_i2c_stats_t *stats)
{
    ESP_LOGI(TAG,
             "motion bus stage=%s dir=%s dt=%" PRIu32
             " ms key_addr=%" PRIu32 "/%" PRIu32
             " key_tx=%" PRIu32 "/%" PRIu32
             " abort=%" PRIu32 " key_gap_max=%" PRIu32
             " us start_stop=%" PRIu32 "/%" PRIu32
             " sda_ignored=%" PRIu32 "/%" PRIu32
             " tx_guard=%" PRIu32
             " sm=%s:%u@0x%02X tx_dr=0x%02X",
             stage, dr == DR_UP ? "up" : "down",
             (uint32_t)(now - interval_tick) * portTICK_PERIOD_MS,
             stats->key_write_addresses - baseline->key_write_addresses,
             stats->key_read_addresses - baseline->key_read_addresses,
             stats->key_tx_started - baseline->key_tx_started,
             stats->completed_key_reads - baseline->completed_key_reads,
             stats->aborted_key_reads - baseline->aborted_key_reads,
             motion_diag_cycles_to_us(stats->max_key_read_gap_cycles),
             stats->recognized_starts - baseline->recognized_starts,
             stats->recognized_stops - baseline->recognized_stops,
             stats->ignored_own_sda_edges - baseline->ignored_own_sda_edges,
             stats->ignored_sda_edges_while_scl_low -
                 baseline->ignored_sda_edges_while_scl_low,
             stats->ignored_sda_edges_during_key_tx -
                 baseline->ignored_sda_edges_during_key_tx,
             motion_diag_phase_name(stats->phase), stats->bit_count,
             stats->current_addr7, stats->tx_dr);
}
#endif

#if CONFIG_DESK_MOTION_DIAGNOSTICS
/** Report motion and bus delivery without making any stop decision. */
static void motion_diagnostics_task(void *arg)
{
    (void)arg;
    TickType_t last_diag_tick = 0;
#if CONFIG_DESK_MOTION_DIAGNOSTICS && \
    CONFIG_DESK_MXTARK_SOFT_I2C_MULTI_ADDRESS
    bool bus_diag_active = false;
    uint8_t bus_diag_dr = DR_IDLE;
    TickType_t bus_diag_tick = 0;
    mxtark_soft_i2c_stats_t bus_diag_baseline = {0};
#endif
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(HEIGHT_SAFETY_POLL_MS));
        TickType_t now = xTaskGetTickCount();
        uint8_t dr = (uint8_t)atomic_load(&s_dr);
        if (dr != DR_UP && dr != DR_DOWN) {
#if CONFIG_DESK_MOTION_DIAGNOSTICS && \
    CONFIG_DESK_MXTARK_SOFT_I2C_MULTI_ADDRESS
            if (bus_diag_active) {
                mxtark_soft_i2c_stats_t stats = {0};
                mxtark_soft_i2c_esp_take_stats(&stats);
                motion_bus_diag_log("end", bus_diag_dr, bus_diag_tick, now,
                                    &bus_diag_baseline, &stats);
                bus_diag_active = false;
            }
#endif
            last_diag_tick = 0;
            continue;
        }

#if CONFIG_DESK_MOTION_DIAGNOSTICS && \
    CONFIG_DESK_MXTARK_SOFT_I2C_MULTI_ADDRESS
        if (!bus_diag_active || dr != bus_diag_dr) {
            if (bus_diag_active) {
                mxtark_soft_i2c_stats_t stats = {0};
                mxtark_soft_i2c_esp_take_stats(&stats);
                motion_bus_diag_log("end", bus_diag_dr, bus_diag_tick, now,
                                    &bus_diag_baseline, &stats);
            }
            mxtark_soft_i2c_esp_take_stats(&bus_diag_baseline);
            bus_diag_active = true;
            bus_diag_dr = dr;
            bus_diag_tick = now;
            ESP_LOGI(TAG,
                     "motion bus begin dir=%s dr=0x%02X sm=%s:%u@0x%02X tx_dr=0x%02X",
                     dr == DR_UP ? "up" : "down", dr,
                     motion_diag_phase_name(bus_diag_baseline.phase),
                     bus_diag_baseline.bit_count,
                     bus_diag_baseline.current_addr7,
                     bus_diag_baseline.tx_dr);
        } else if (elapsed_ms_since((uint_fast32_t)bus_diag_tick, now) >=
                   HEIGHT_MOTION_DIAG_INTERVAL_MS) {
            mxtark_soft_i2c_stats_t stats = {0};
            mxtark_soft_i2c_esp_take_stats(&stats);
            motion_bus_diag_log("run", bus_diag_dr, bus_diag_tick, now,
                                &bus_diag_baseline, &stats);
            bus_diag_baseline = stats;
            bus_diag_tick = now;
        }
#endif
        if (last_diag_tick == 0 ||
            elapsed_ms_since((uint_fast32_t)last_diag_tick, now) >=
                HEIGHT_MOTION_DIAG_INTERVAL_MS) {
            int height_mm = atomic_load(&s_height_mm);
            int height_age_ms = elapsed_ms_since(
                atomic_load(&s_height_tick), now);
            ESP_LOGI(TAG,
                     "motion state dir=%s dr=0x%02X height=%d height_age=%d ms max=%d target=%d",
                     dr == DR_UP ? "up" : "down", dr, height_mm,
                     height_age_ms, atomic_load(&s_max_height_mm),
                     atomic_load(&s_preset_target_mm));
            last_diag_tick = now;
        }
    }
}
#endif

#endif

#if MXTARK_HEIGHT_INPUT_ENABLED && !CONFIG_DESK_TOF_ENABLE
/** Stop once the measured height reaches or crosses the preset target. */
static void stop_preset_if_reached(int height_mm)
{
    int target_mm = atomic_load(&s_preset_target_mm);
    int direction = atomic_load(&s_preset_direction);
    if (target_mm < 0 || direction == 0) {
        return;
    }

    bool reached = mxtark_preset_reached(
        height_mm, target_mm, PRESET_STOP_MARGIN_MM,
        (mxtark_preset_direction_t)direction);
    if (!reached) {
        return;
    }

    /* CAS prevents an old height frame from cancelling a newer preset request. */
    int expected = target_mm;
    if (!atomic_compare_exchange_strong(&s_preset_target_mm, &expected, -1)) {
        return;
    }
    atomic_store(&s_preset_direction, 0);
    ESP_LOGI(TAG, "preset target reached: current=%d mm target=%d mm",
             height_mm, target_mm);
    (void)set_dr(DR_IDLE);
}
#endif

#if CONFIG_DESK_TOF_ENABLE
/**
 * Advance the ToF preset state using each sensor sample at most once.
 *
 * The pure state machine decides when to stop, settle, correct, or give up;
 * this adapter applies the resulting DR byte and preserves the existing upward
 * obstacle gate for every correction.
 */
static void update_tof_preset_control(desk_tof_snapshot_t snapshot,
                                      uint32_t now_ms)
{
    mxtark_preset_action_t action;
    unsigned int corrections;
    int target_mm;
#if CONFIG_DESK_MOTION_DIAGNOSTICS
    mxtark_preset_phase_t phase;
    int direction;
    bool consumed_sample;
#endif

    portENTER_CRITICAL(&s_preset_control_mux);
    target_mm = s_preset_control.target_mm;
#if CONFIG_DESK_MOTION_DIAGNOSTICS
    consumed_sample = snapshot.height_sample_id !=
                      s_preset_control.last_sample_id;
#endif
    action = mxtark_preset_control_update(
        &s_preset_control, snapshot.control_height_mm, snapshot.height_mm,
        snapshot.height_sample_id, now_ms, PRESET_STOP_MARGIN_MM,
        PRESET_SETTLE_TOLERANCE_MM, PRESET_SETTLE_MS,
        PRESET_MAX_CORRECTIONS);
    corrections = s_preset_control.correction_count;
    atomic_store(&s_preset_target_mm, s_preset_control.target_mm);
    atomic_store(&s_preset_direction, s_preset_control.direction);
#if CONFIG_DESK_MOTION_DIAGNOSTICS
    phase = s_preset_control.phase;
    direction = (int)s_preset_control.direction;
#endif
    portEXIT_CRITICAL(&s_preset_control_mux);

#if CONFIG_DESK_MOTION_DIAGNOSTICS
    if (consumed_sample) {
        ESP_LOGI(TAG,
                 "preset sample id=%" PRIu32 " phase=%d dir=%d raw=%d control=%d stable=%d target=%d corrections=%u",
                 snapshot.height_sample_id, (int)phase, direction,
                 snapshot.raw_height_mm, snapshot.control_height_mm,
                 snapshot.height_mm, target_mm, corrections);
    }
#endif

    esp_err_t err = ESP_OK;
    switch (action) {
    case MXTARK_PRESET_ACTION_STOP_AND_SETTLE:
        ESP_LOGI(TAG,
                 "preset stop: control=%d stable=%d target=%d; settling",
                 snapshot.control_height_mm, snapshot.height_mm, target_mm);
        err = set_dr(DR_IDLE);
        break;
    case MXTARK_PRESET_ACTION_MOVE_UP:
        if (tof_upward_blocked(snapshot)) {
            log_tof_upward_block("preset correction", snapshot);
            err = ESP_ERR_INVALID_STATE;
        } else {
            ESP_LOGI(TAG,
                     "preset correction %u/%u: stable=%d target=%d direction=up",
                     corrections, PRESET_MAX_CORRECTIONS,
                     snapshot.height_mm, target_mm);
            err = set_dr(DR_UP);
        }
        break;
    case MXTARK_PRESET_ACTION_MOVE_DOWN:
        ESP_LOGI(TAG,
                 "preset correction %u/%u: stable=%d target=%d direction=down",
                 corrections, PRESET_MAX_CORRECTIONS,
                 snapshot.height_mm, target_mm);
        err = set_dr(DR_DOWN);
        break;
    case MXTARK_PRESET_ACTION_COMPLETE:
        ESP_LOGI(TAG, "preset settled: stable=%d target=%d",
                 snapshot.height_mm, target_mm);
        break;
    case MXTARK_PRESET_ACTION_CORRECTION_LIMIT:
        ESP_LOGW(TAG,
                 "preset correction limit: stable=%d target=%d tolerance=%d",
                 snapshot.height_mm, target_mm,
                 PRESET_SETTLE_TOLERANCE_MM);
        err = set_dr(DR_IDLE);
        break;
    case MXTARK_PRESET_ACTION_NONE:
    default:
        break;
    }
    if (err != ESP_OK) {
        cancel_preset_motion();
        (void)set_dr(DR_IDLE);
    }
}

/**
 * Enforce the ToF ceiling/obstacle policy during motion and close preset loops.
 *
 * Entry checks reject unsafe commands before motion; this task is still
 * required because a safe command can later reach 94 cm or encounter an
 * obstacle below 80 cm. STOP goes through the panel arbiter so a held original
 * panel key remains suppressed until it is released.
 */
static void tof_control_task(void *arg)
{
    (void)arg;
    while (true) {
        uint8_t dr = (uint8_t)atomic_load(&s_dr);
        int target_mm = atomic_load(&s_preset_target_mm);
        if (dr == DR_UP || dr == DR_DOWN || target_mm >= 0) {
            desk_tof_snapshot_t snapshot = desk_tof_snapshot();
            if (target_mm >= 0 && !snapshot.height_known) {
                ESP_LOGW(TAG, "stop preset: TOF400C height unavailable");
                cancel_preset_motion();
                (void)set_dr(DR_IDLE);
            } else if (dr == DR_UP && tof_upward_blocked(snapshot)) {
                log_tof_upward_block("motion", snapshot);
                cancel_preset_motion();
                (void)set_dr(DR_IDLE);
            } else if (target_mm >= 0) {
                uint32_t now_ms =
                    (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
                update_tof_preset_control(snapshot, now_ms);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(HEIGHT_SAFETY_POLL_MS));
    }
}
#endif

#if CONFIG_DESK_MXTARK_HEIGHT_SNIFFER_EXPERIMENTAL
static bus_sniffer_state_t s_sniffer;
static portMUX_TYPE s_sniffer_mux = portMUX_INITIALIZER_UNLOCKED;

/** Sample one bus bit on every SCL rising edge. */
static void IRAM_ATTR sniffer_scl_rising_isr(void *arg)
{
    (void)arg;
    portENTER_CRITICAL_ISR(&s_sniffer_mux);
    if (!s_sniffer.in_frame) {
        portEXIT_CRITICAL_ISR(&s_sniffer_mux);
        return;
    }

    if (s_sniffer.bit_index < 8) {
        s_sniffer.byte_accumulator =
            (uint8_t)((s_sniffer.byte_accumulator << 1) |
                      (gpio_get_level(CONFIG_DESK_I2C_SDA_GPIO) ? 1u : 0u));
    }
    s_sniffer.bit_index++;
    if (s_sniffer.bit_index == 9) {
        /* The ninth bit is ACK/NACK. Keep the preceding byte either way. */
        if (s_sniffer.byte_count < sizeof(s_sniffer.bytes)) {
            s_sniffer.bytes[s_sniffer.byte_count] = s_sniffer.byte_accumulator;
        }
        s_sniffer.byte_count++;
        s_sniffer.bit_index = 0;
        s_sniffer.byte_accumulator = 0;
    }
    portEXIT_CRITICAL_ISR(&s_sniffer_mux);
}

/** Detect START/STOP from DAT edges while SCL is high and queue complete digit writes. */
static void IRAM_ATTR sniffer_sda_edge_isr(void *arg)
{
    slave_ctx_t *ctx = (slave_ctx_t *)arg;
    if (!gpio_get_level(CONFIG_DESK_I2C_SCL_GPIO)) {
        return; /* Normal data transition while SCL is low. */
    }

    mxtark_soft_i2c_digit_event_t event = {0};
    bool queue_event = false;
    portENTER_CRITICAL_ISR(&s_sniffer_mux);
    if (!gpio_get_level(CONFIG_DESK_I2C_SDA_GPIO)) {
        /* START or repeated START: begin a fresh address phase. */
        s_sniffer.in_frame = true;
        s_sniffer.bit_index = 0;
        s_sniffer.byte_accumulator = 0;
        s_sniffer.byte_count = 0;
        s_sniffer.bytes[0] = 0;
        s_sniffer.bytes[1] = 0;
    } else {
        /* STOP: a digit write is exactly address+W followed by one data byte. */
        if (s_sniffer.in_frame && s_sniffer.byte_count == 2 &&
            (s_sniffer.bytes[0] & 1u) == 0) {
            uint8_t addr7 = (uint8_t)(s_sniffer.bytes[0] >> 1);
            if (addr7 >= ADDR_DIG1_7BIT && addr7 <= ADDR_DIG4_7BIT) {
                event.addr7 = addr7;
                event.segment = s_sniffer.bytes[1];
                event.ready = true;
                queue_event = true;
            }
        }
        s_sniffer.in_frame = false;
    }
    portEXIT_CRITICAL_ISR(&s_sniffer_mux);

    if (queue_event && ctx->digit_q) {
        BaseType_t hp = pdFALSE;
        (void)xQueueSendFromISR(ctx->digit_q, &event, &hp);
        if (hp == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }
}

/** Attach read-only GPIO interrupts without changing the I²C peripheral routing. */
static esp_err_t start_digit_sniffer(slave_ctx_t *ctx)
{
    esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    ESP_RETURN_ON_ERROR(gpio_set_intr_type(CONFIG_DESK_I2C_SCL_GPIO,
                                           GPIO_INTR_POSEDGE),
                        TAG, "set CLK interrupt");
    ESP_RETURN_ON_ERROR(gpio_set_intr_type(CONFIG_DESK_I2C_SDA_GPIO,
                                           GPIO_INTR_ANYEDGE),
                        TAG, "set DAT interrupt");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(CONFIG_DESK_I2C_SCL_GPIO,
                                             sniffer_scl_rising_isr, ctx),
                        TAG, "add CLK handler");
    err = gpio_isr_handler_add(CONFIG_DESK_I2C_SDA_GPIO, sniffer_sda_edge_isr, ctx);
    if (err != ESP_OK) {
        gpio_isr_handler_remove(CONFIG_DESK_I2C_SCL_GPIO);
        return err;
    }
    return ESP_OK;
}

#endif

#if MXTARK_HEIGHT_INPUT_ENABLED
/** Assemble digit events away from ISR context and publish only valid heights. */
static void height_decode_task(void *arg)
{
    slave_ctx_t *ctx = (slave_ctx_t *)arg;
    tm1650_height_decoder_t decoder;
    tm1650_height_cache_t cache;
    tm1650_height_decoder_reset(&decoder);
    tm1650_height_cache_reset(&cache);
    TickType_t frame_start_tick = 0;
    uint32_t last_invalid_raw = UINT32_MAX;
    mxtark_soft_i2c_digit_event_t event;

    for (;;) {
        if (xQueueReceive(ctx->digit_q, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        TickType_t now = xTaskGetTickCount();
        uint32_t now_ms = (uint32_t)now * portTICK_PERIOD_MS;

        int cached_height_mm = -1;
        uint32_t cached_oldest_age_ms = 0;
        tm1650_height_result_t cache_result = tm1650_height_cache_feed(
            &cache, event.addr7, event.segment, now_ms,
            HEIGHT_DIGIT_CACHE_MS, &cached_height_mm,
            &cached_oldest_age_ms);

        tm1650_height_result_t frame_result = TM1650_HEIGHT_WAITING;
        int frame_height_mm = -1;
        if (event.addr7 == 0x36u) {
            tm1650_height_decoder_reset(&decoder);
            frame_start_tick = now;
        } else if (frame_start_tick == 0 ||
                   now - frame_start_tick >
                       pdMS_TO_TICKS(HEIGHT_FRAME_WINDOW_MS)) {
            /* An incomplete or late fragment cannot borrow bytes from a new frame. */
            tm1650_height_decoder_reset(&decoder);
            frame_start_tick = 0;
        }
        if (frame_start_tick != 0) {
            frame_result = tm1650_height_decoder_feed(
                &decoder, event.addr7, event.segment, &frame_height_mm);
        }
        if (frame_result == TM1650_HEIGHT_VALID ||
            frame_result == TM1650_HEIGHT_INVALID) {
            frame_start_tick = 0;
        }

        int previous = atomic_load(&s_height_mm);
        mxtark_preset_direction_t direction = current_height_direction();
        bool complete_frame = frame_result == TM1650_HEIGHT_VALID;
        bool cached_motion_sample =
            !complete_frame && previous >= 0 &&
            direction != MXTARK_PRESET_STOP &&
            cache_result == TM1650_HEIGHT_VALID;
        if (!complete_frame && !cached_motion_sample) {
            if (frame_result == TM1650_HEIGHT_INVALID) {
                uint32_t raw = ((uint32_t)decoder.digits[0] << 24) |
                               ((uint32_t)decoder.digits[1] << 16) |
                               ((uint32_t)decoder.digits[2] << 8) |
                               decoder.digits[3];
                /* Log each unknown pattern once so bus noise cannot flood UART. */
                if (raw != last_invalid_raw) {
                    last_invalid_raw = raw;
                    ESP_LOGW(TAG,
                             "unknown height raw=%02X %02X %02X %02X",
                             decoder.digits[0], decoder.digits[1],
                             decoder.digits[2], decoder.digits[3]);
                }
            }
            continue;
        }

        int height_mm = complete_frame ? frame_height_mm : cached_height_mm;
        int elapsed_ms = previous < 0
                             ? 0
                             : elapsed_ms_since(atomic_load(&s_height_tick),
                                                now);
        uint_fast32_t motion_epoch = atomic_load(&s_motion_epoch);
        bool resync_pending = atomic_load(&s_height_epoch) != motion_epoch;
        /* Only a complete controller frame may bypass a stale baseline. */
        bool transition_resync = resync_pending && complete_frame;
        bool accepted = mxtark_height_transition_valid(
            previous, height_mm, elapsed_ms, direction, transition_resync,
            MXTARK_HEIGHT_TRANSITION_MAX_SPEED_MM_PER_S,
            HEIGHT_STEP_SLACK_MM);
        last_invalid_raw = UINT32_MAX;
        if (accepted) {
            /*
             * Only a transition accepted for the current motion may drive the
             * published height or either upward safety path. A mixed or stale
            * raw frame must never manufacture an early ceiling.
             */
            atomic_store(&s_height_mm, height_mm);
            if (complete_frame || previous != height_mm) {
                /* A repeated cached value is stale evidence, not a new speed anchor. */
                atomic_store(&s_height_tick, (uint_fast32_t)now);
                /* Preserve a concurrently started newer motion as pending. */
                atomic_store(&s_height_epoch, motion_epoch);
            }
        }
        if (accepted && previous != height_mm) {
            if (complete_frame) {
                ESP_LOGI(TAG, "height=%d.%d cm%s raw=%02X %02X %02X %02X",
                         height_mm / 10, height_mm % 10,
                         resync_pending ? " (resync)" : "",
                         decoder.digits[0], decoder.digits[1],
                         decoder.digits[2], decoder.digits[3]);
            } else {
                ESP_LOGI(TAG,
                         "height=%d.%d cm (cached age=%" PRIu32
                         " ms) raw=%02X %02X %02X",
                         height_mm / 10, height_mm % 10,
                         cached_oldest_age_ms, cache.digits[0],
                         cache.digits[1], cache.digits[2]);
            }
        } else if (!accepted) {
            ESP_LOGW(TAG,
                     "reject height transition: previous=%d candidate=%d elapsed=%d ms direction=%d source=%s",
                     previous, height_mm, elapsed_ms, (int)direction,
                     complete_frame ? "frame" : "cache");
        }
        if (accepted) {
#if !CONFIG_DESK_TOF_ENABLE
            stop_preset_if_reached(height_mm);
#endif
        }
    }
}
#endif

#if CONFIG_DESK_MXTARK_PANEL_PROXY
/**
 * Merge a panel key into the controller-facing DR byte.
 *
 * This callback runs in the panel software-master task, never in either bus
 * ISR. A new physical key permanently cancels any gateway command; when the
 * legacy controller-height experiment is enabled it also resets that baseline.
 */
static void panel_key_update(bool connected, uint8_t dr, void *ctx)
{
    (void)ctx;
    mxtark_panel_arbiter_result_t result;
    portENTER_CRITICAL(&s_panel_arbiter_mux);
    mxtark_panel_arbiter_panel_update(&s_panel_arbiter, connected, dr,
                                        &result);
    atomic_store(&s_panel_active, s_panel_arbiter.panel_active);
    atomic_store(&s_panel_connected, connected);
    portEXIT_CRITICAL(&s_panel_arbiter_mux);

#if MXTARK_CLOSED_LOOP_ENABLED
    if (result.panel_started) {
        cancel_preset_motion();
#if MXTARK_HEIGHT_INPUT_ENABLED
        begin_height_resync();
#endif
    } else if (result.output_changed &&
               (result.output_dr == DR_UP || result.output_dr == DR_DOWN)) {
#if MXTARK_HEIGHT_INPUT_ENABLED
        begin_height_resync();
#endif
    }
#endif
    if (result.panel_started) {
        ESP_LOGI(TAG, "original panel took control DR=0x%02X", dr);
    } else if (result.panel_released) {
        ESP_LOGI(TAG, "original panel released");
    }
    if (result.output_changed) {
#if CONFIG_DESK_TOF_ENABLE
        if (result.output_dr == DR_UP) {
            desk_tof_snapshot_t snapshot = desk_tof_snapshot();
            if (tof_upward_blocked(snapshot)) {
                log_tof_upward_block("panel", snapshot);
                cancel_preset_motion();
                (void)set_dr(DR_IDLE);
                return;
            }
        }
#endif
        publish_controller_dr(result.output_dr);
    }
}

/** Return true while a physical panel key owns the command path. */
static bool panel_has_priority(void)
{
    return atomic_load(&s_panel_active);
}

/** Apply the core's effective panel permission through the tested arbiter. */
static esp_err_t yd_set_panel_enabled(bool enabled)
{
    mxtark_panel_arbiter_result_t result;
    portENTER_CRITICAL(&s_panel_arbiter_mux);
    mxtark_panel_arbiter_set_enabled(&s_panel_arbiter, enabled, &result);
    atomic_store(&s_panel_active, s_panel_arbiter.panel_active);
    portEXIT_CRITICAL(&s_panel_arbiter_mux);
    if (result.output_changed) {
        publish_controller_dr(result.output_dr);
    }
    ESP_LOGI(TAG, "original panel input %s", enabled ? "enabled" : "disabled");
    return ESP_OK;
}
#else
static bool panel_has_priority(void)
{
    return false;
}

static esp_err_t yd_set_panel_enabled(bool enabled)
{
    (void)enabled;
    return ESP_ERR_NOT_SUPPORTED;
}
#endif

#if !CONFIG_DESK_MXTARK_SOFT_I2C_MULTI_ADDRESS
static bool IRAM_ATTR on_receive_cb(i2c_slave_dev_handle_t i2c_slave,
                                    const i2c_slave_rx_done_event_data_t *evt_data,
                                    void *arg)
{
    (void)i2c_slave;
    (void)evt_data;
    (void)arg;
    return false;
}

static bool IRAM_ATTR on_request_cb(i2c_slave_dev_handle_t i2c_slave,
                                    const i2c_slave_request_event_data_t *evt_data,
                                    void *arg)
{
    (void)i2c_slave;
    (void)evt_data;
    slave_ctx_t *ctx = (slave_ctx_t *)arg;
    uint8_t token = 1;
    BaseType_t hp = pdFALSE;
    xQueueOverwriteFromISR(ctx->tx_q, &token, &hp);
    return hp == pdTRUE;
}

/** Keep the hardware TX path stocked with the current key byte. */
static void write_controller_dr(uint8_t dr)
{
    uint32_t written = 0;
    (void)i2c_slave_write(s_ctx.handle, &dr, 1, &written, 50);
}

static void slave_tx_task(void *arg)
{
    slave_ctx_t *ctx = (slave_ctx_t *)arg;
    uint8_t token;
    for (;;) {
        if (xQueueReceive(ctx->tx_q, &token, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        write_controller_dr((uint8_t)atomic_load(&s_dr));
    }
}
#endif

/** Publish the already-arbitrated byte to the control-box-side slave. */
static void publish_controller_dr(uint8_t dr)
{
    uint8_t previous = (uint8_t)atomic_exchange(&s_dr, dr);
#if CONFIG_DESK_MXTARK_SOFT_I2C_MULTI_ADDRESS
    mxtark_soft_i2c_esp_set_dr(dr);
#endif
#if !CONFIG_DESK_MXTARK_SOFT_I2C_MULTI_ADDRESS
    /*
     * Idle polling queues 0x2E into a deep TX ring. Changing the key without
     * flushing that ring makes the control box keep reading "no press" for
     * seconds, so hold and preset both feel like a 5 s start delay.
     */
    if (s_ctx.handle && previous != dr) {
        (void)i2c_slave_reset_tx_fifo(s_ctx.handle);
        write_controller_dr(dr);
    }
#endif
    if (previous != dr) {
        ESP_LOGI(TAG, "DR=0x%02X", dr);
    }
}

/** Apply a gateway/safety command through original-panel arbitration. */
static esp_err_t set_dr(uint8_t dr)
{
#if CONFIG_DESK_MXTARK_PANEL_PROXY
    mxtark_panel_arbiter_result_t result;
    portENTER_CRITICAL(&s_panel_arbiter_mux);
    bool accepted = mxtark_panel_arbiter_gateway_request(
        &s_panel_arbiter, dr, &result);
    portEXIT_CRITICAL(&s_panel_arbiter_mux);
    if (!accepted) {
        return ESP_ERR_INVALID_STATE;
    }
    if (result.output_changed) {
        publish_controller_dr(result.output_dr);
    }
#else
    publish_controller_dr(dr);
#endif
    return ESP_OK;
}

/** Return the product height source without applying any calibration offset. */
static esp_err_t read_control_height_mm(int *out_mm)
{
    if (!out_mm) {
        return ESP_ERR_INVALID_ARG;
    }
#if CONFIG_DESK_TOF_ENABLE
    desk_tof_snapshot_t snapshot = desk_tof_snapshot();
    if (!snapshot.height_known) {
        /* NOT_SUPPORTED also prevents the obsolete boot-time digit probe. */
        return ESP_ERR_NOT_SUPPORTED;
    }
    *out_mm = snapshot.height_mm;
    return ESP_OK;
#elif MXTARK_HEIGHT_INPUT_ENABLED
    int height_mm = atomic_load(&s_height_mm);
    if (height_mm < 0) {
        return ESP_ERR_INVALID_STATE;
    }
    *out_mm = height_mm;
    return ESP_OK;
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static esp_err_t yd_init(void)
{
    atomic_store(&s_dr, DR_IDLE);
#if CONFIG_DESK_MXTARK_PANEL_PROXY
    mxtark_panel_arbiter_init(&s_panel_arbiter, DR_IDLE);
    atomic_store(&s_panel_active, false);
    atomic_store(&s_panel_connected, false);
#endif
#if MXTARK_CLOSED_LOOP_ENABLED
    atomic_store(&s_max_height_mm, CONFIG_DESK_MAX_HEIGHT_MM);
    atomic_store(&s_preset1_height_mm, DESK_PRESET1_HEIGHT_MM_DEFAULT);
    atomic_store(&s_preset4_height_mm, DESK_PRESET4_HEIGHT_MM_DEFAULT);
    cancel_preset_motion();
#endif
#if MXTARK_HEIGHT_INPUT_ENABLED
    atomic_store(&s_height_mm, -1);
    atomic_store(&s_motion_epoch, 1);
    atomic_store(&s_height_epoch, 0);
    atomic_store(&s_height_tick, 0);
#endif
#if !CONFIG_DESK_MXTARK_SOFT_I2C_MULTI_ADDRESS
    s_ctx.tx_q = xQueueCreate(1, sizeof(uint8_t));
    if (!s_ctx.tx_q) {
        return ESP_ERR_NO_MEM;
    }
#endif
#if MXTARK_HEIGHT_INPUT_ENABLED
    s_ctx.digit_q = xQueueCreate(32, sizeof(mxtark_soft_i2c_digit_event_t));
    if (!s_ctx.digit_q) {
        return ESP_ERR_NO_MEM;
    }
#endif

#if !CONFIG_DESK_MXTARK_SOFT_I2C_MULTI_ADDRESS
    i2c_slave_config_t cfg = {
        .i2c_port = CONFIG_DESK_I2C_PORT,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .scl_io_num = CONFIG_DESK_I2C_SCL_GPIO,
        .sda_io_num = CONFIG_DESK_I2C_SDA_GPIO,
        .slave_addr = ADDR_KEY_7BIT,
        /* One-byte key replies must not accumulate a multi-second idle queue. */
        .send_buf_depth = 8,
        .receive_buf_depth = 64,
        .addr_bit_len = I2C_ADDR_BIT_LEN_7,
    };
    esp_err_t err = i2c_new_slave_device(&cfg, &s_ctx.handle);
    if (err != ESP_OK) {
        return err;
    }
    i2c_slave_event_callbacks_t cbs = {
        .on_request = on_request_cb,
        .on_receive = on_receive_cb,
    };
    ESP_ERROR_CHECK(i2c_slave_register_event_callbacks(s_ctx.handle, &cbs, &s_ctx));
    if (xTaskCreatePinnedToCore(slave_tx_task, "yd_i2c_tx", 4096, &s_ctx,
                                configMAX_PRIORITIES - 2, NULL, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
#endif
#if MXTARK_HEIGHT_INPUT_ENABLED
    if (xTaskCreatePinnedToCore(height_decode_task, "yd_height", 4096, &s_ctx,
                                configMAX_PRIORITIES - 4, NULL, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
#endif
#if CONFIG_DESK_MXTARK_SOFT_I2C_MULTI_ADDRESS
    esp_err_t err = mxtark_soft_i2c_esp_init(
        s_ctx.digit_q, NULL, DR_IDLE);
    if (err != ESP_OK) {
        return err; /* This adapter owns both motion and height in this mode. */
    }
#elif CONFIG_DESK_MXTARK_HEIGHT_SNIFFER_EXPERIMENTAL
    err = start_digit_sniffer(&s_ctx);
    if (err != ESP_OK) {
        /* Height is optional; never sacrifice the already working motion path. */
        ESP_LOGE(TAG, "height sniffer unavailable: %s", esp_err_to_name(err));
    }
#else
    ESP_LOGI(TAG,
             "control-box height input disabled; waiting for external TOF source");
#endif
#if CONFIG_DESK_MXTARK_PANEL_PROXY
    err = mxtark_panel_proxy_init(panel_key_update, NULL);
    if (err != ESP_OK) {
        return err; /* An enabled active bridge must fail closed, not half-start. */
    }
#endif
#if CONFIG_DESK_TOF_ENABLE
    if (xTaskCreatePinnedToCore(tof_control_task, "yd_tof_guard", 3072, NULL,
                                configMAX_PRIORITIES - 4, NULL, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
#endif
#if MXTARK_HEIGHT_INPUT_ENABLED && CONFIG_DESK_MOTION_DIAGNOSTICS
    if (xTaskCreatePinnedToCore(motion_diagnostics_task, "yd_motion_diag", 3072,
                                NULL, configMAX_PRIORITIES - 3,
                                NULL, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
#endif
#if !CONFIG_DESK_MXTARK_SOFT_I2C_MULTI_ADDRESS
    ESP_LOGI(TAG, "I2C slave @0x%02X SCL=%d SDA=%d", ADDR_KEY_7BIT,
             CONFIG_DESK_I2C_SCL_GPIO, CONFIG_DESK_I2C_SDA_GPIO);
#endif
    return ESP_OK;
}

static esp_err_t yd_deinit(void)
{
    return ESP_OK;
}

static esp_err_t yd_stop(void)
{
#if MXTARK_CLOSED_LOOP_ENABLED
    cancel_preset_motion();
#endif
    return set_dr(DR_IDLE);
}

/** Start either manual direction through one shared driver path. */
static esp_err_t yd_hold_direction(uint8_t dr)
{
    if (dr != DR_UP && dr != DR_DOWN) {
        return ESP_ERR_INVALID_ARG;
    }
    if (panel_has_priority()) {
        return ESP_ERR_INVALID_STATE;
    }
#if CONFIG_DESK_TOF_ENABLE
    if (dr == DR_UP) {
        desk_tof_snapshot_t snapshot = desk_tof_snapshot();
        if (tof_upward_blocked(snapshot)) {
            log_tof_upward_block("command", snapshot);
            return ESP_ERR_INVALID_STATE;
        }
    }
#endif
#if MXTARK_CLOSED_LOOP_ENABLED
    cancel_preset_motion();
#endif
#if MXTARK_HEIGHT_INPUT_ENABLED
    if (dr == DR_UP) {
        int height_mm = atomic_load(&s_height_mm);
        if (height_mm < 0) {
            ESP_LOGI(TAG,
                     "manual up: height unknown, waiting for controller frame");
        }
    }
    begin_height_resync();
#endif
    return set_dr(dr);
}

static esp_err_t yd_hold_up(void)
{
    return yd_hold_direction(DR_UP);
}

static esp_err_t yd_hold_down(void)
{
    return yd_hold_direction(DR_DOWN);
}

/**
 * 使用产品 ToF 高度链路启动有界上升。
 *
 * 这里不能复用无 ToF 固件的实验高度输入：产品承诺要求 tof_control_task
 * 在命令返回后继续独立执行最高高度和右侧障碍保护。
 */
static esp_err_t yd_raise_to_max(void)
{
    if (panel_has_priority()) {
        return ESP_ERR_INVALID_STATE;
    }
#if CONFIG_DESK_TOF_ENABLE
    desk_tof_snapshot_t snapshot = desk_tof_snapshot();
    int max_height_mm = atomic_load(&s_max_height_mm);
    if (!snapshot.height_known) {
        log_tof_upward_block("raise_to_max", snapshot);
        return ESP_ERR_INVALID_STATE;
    }
    if (snapshot.height_mm >= max_height_mm) {
#if MXTARK_CLOSED_LOOP_ENABLED
        cancel_preset_motion();
#endif
        return set_dr(DR_IDLE);
    }
    if (tof_upward_blocked(snapshot)) {
        log_tof_upward_block("raise_to_max", snapshot);
        return ESP_ERR_INVALID_STATE;
    }
#if MXTARK_CLOSED_LOOP_ENABLED
    cancel_preset_motion();
#endif
    ESP_LOGI(TAG, "raise to max: current=%d mm max=%d mm",
             snapshot.height_mm, max_height_mm);
    return set_dr(DR_UP);
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

/**
 * 开始控制盒故障重置。
 *
 * 重置码来自 12 MHz 实机抓包；持续时间和最终空闲码由 desk_core 的
 * 单次定时器负责，确保浏览器断线也不会让 0x7F 永久保持。
 */
static esp_err_t yd_reset_controller(void)
{
    if (panel_has_priority()) {
        return ESP_ERR_INVALID_STATE;
    }
#if MXTARK_CLOSED_LOOP_ENABLED
    cancel_preset_motion();
#endif
    return set_dr(DR_RESET);
}

/**
 * 使用真实高度启动一个有界目标动作。
 *
 * 只有两个内置档位保留无 TOF 构建下的既有启动方向；任意自定义高度在
 * 当前高度未知时直接拒绝，避免猜错方向后持续移动。
 */
static esp_err_t yd_start_height_target(
    int target_mm, mxtark_preset_direction_t unknown_height_direction,
    const char *log_label)
{
    if (panel_has_priority()) {
        return ESP_ERR_INVALID_STATE;
    }
#if MXTARK_CLOSED_LOOP_ENABLED
    if (target_mm < DESK_HEIGHT_MM_MIN ||
        target_mm > DESK_HEIGHT_MM_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    int current_mm = -1;
#if CONFIG_DESK_TOF_ENABLE
    desk_tof_snapshot_t start_snapshot = desk_tof_snapshot();
    esp_err_t height_err = start_snapshot.height_known ? ESP_OK
                                                       : ESP_ERR_NOT_SUPPORTED;
    if (height_err == ESP_OK) {
        current_mm = start_snapshot.height_mm;
    }
#else
    esp_err_t height_err = read_control_height_mm(&current_mm);
#endif
    if (height_err != ESP_OK) {
#if CONFIG_DESK_TOF_ENABLE
        return ESP_ERR_INVALID_STATE;
#else
        if (unknown_height_direction == MXTARK_PRESET_STOP) {
            return ESP_ERR_INVALID_STATE;
        }
        atomic_store(&s_preset_direction, unknown_height_direction);
        atomic_store(&s_preset_target_mm, target_mm);
        begin_height_resync();
        ESP_LOGI(TAG, "%s bootstrap: height unknown, target=%d mm",
                 log_label, target_mm);
        esp_err_t err = set_dr(unknown_height_direction > 0 ? DR_UP
                                                            : DR_DOWN);
        if (err != ESP_OK) {
            cancel_preset_motion();
        }
        return err;
#endif
    }
    mxtark_preset_direction_t direction = mxtark_preset_direction(
        current_mm, target_mm,
#if CONFIG_DESK_TOF_ENABLE
        PRESET_START_MARGIN_MM
#else
        PRESET_STOP_MARGIN_MM
#endif
    );
    if (direction == MXTARK_PRESET_STOP) {
        cancel_preset_motion();
        return set_dr(DR_IDLE);
    }
#if CONFIG_DESK_TOF_ENABLE
    if (direction == MXTARK_PRESET_UP) {
        if (tof_upward_blocked(start_snapshot)) {
            log_tof_upward_block("preset", start_snapshot);
            return ESP_ERR_INVALID_STATE;
        }
    }
#endif

#if CONFIG_DESK_TOF_ENABLE
    start_preset_motion(target_mm, direction,
                        start_snapshot.height_sample_id);
#else
    atomic_store(&s_preset_direction, direction);
    atomic_store(&s_preset_target_mm, target_mm);
#endif
    ESP_LOGI(TAG, "%s: current=%d mm target=%d mm direction=%s",
             log_label, current_mm, target_mm,
             direction > 0 ? "up" : "down");
#if MXTARK_HEIGHT_INPUT_ENABLED
    begin_height_resync();
#endif
    esp_err_t err = set_dr(direction > 0 ? DR_UP : DR_DOWN);
    if (err != ESP_OK) {
        cancel_preset_motion();
    }
    return err;
#else
    (void)target_mm;
    (void)unknown_height_direction;
    (void)log_label;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static esp_err_t yd_goto_preset(uint8_t n)
{
    int target_mm = mxtark_preset_target_mm(
        n, atomic_load(&s_preset1_height_mm),
        atomic_load(&s_preset4_height_mm));
    if (target_mm < 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return yd_start_height_target(target_mm,
                                  mxtark_preset_bootstrap_direction(n),
                                  n == 1 ? "preset 1" : "preset 4");
}

static esp_err_t yd_goto_height_mm(int target_height_mm)
{
    return yd_start_height_target(target_height_mm, MXTARK_PRESET_STOP,
                                  "custom preset");
}

static esp_err_t yd_set_max_height_mm(int max_height_mm)
{
    if (max_height_mm < DESK_HEIGHT_MM_MIN ||
        max_height_mm > DESK_HEIGHT_MM_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
#if MXTARK_CLOSED_LOOP_ENABLED
    atomic_store(&s_max_height_mm, max_height_mm);
#else
    /*
     * desk_core 仍负责跨 Web/BLE 持久化该配置。没有真实高度源时驱动只接受
     * 配置但不执行限高，避免初始化失败或把模拟值用于安全控制。
     */
    (void)max_height_mm;
#endif
    return ESP_OK;
}

static esp_err_t yd_set_preset_heights_mm(int preset1_height_mm,
                                           int preset4_height_mm)
{
    if (preset1_height_mm < DESK_HEIGHT_MM_MIN ||
        preset1_height_mm >= preset4_height_mm ||
        preset4_height_mm > DESK_HEIGHT_MM_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
#if MXTARK_CLOSED_LOOP_ENABLED
    atomic_store(&s_preset1_height_mm, preset1_height_mm);
    atomic_store(&s_preset4_height_mm, preset4_height_mm);
#else
    (void)preset1_height_mm;
    (void)preset4_height_mm;
#endif
    return ESP_OK;
}

static esp_err_t yd_save_preset(uint8_t n)
{
#if MXTARK_HEIGHT_INPUT_ENABLED
    if (panel_has_priority()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (n == 1) {
        return set_dr(DR_P1_SAVE);
    }
    if (n == 4) {
        return set_dr(DR_P4_SAVE);
    }
    return ESP_ERR_NOT_SUPPORTED;
#else
    (void)n;
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static esp_err_t yd_get_height_mm(int *out_mm)
{
    return read_control_height_mm(out_mm);
}

static bool yd_is_upward_blocked(void)
{
#if CONFIG_DESK_TOF_ENABLE
    return tof_upward_blocked(desk_tof_snapshot());
#else
    return false;
#endif
}

static desk_status_t yd_get_status(void)
{
#if MXTARK_CLOSED_LOOP_ENABLED
    if (atomic_load(&s_preset_target_mm) >= 0) {
        return DESK_STATUS_GOTO_PRESET;
    }
#endif
    uint8_t dr = (uint8_t)atomic_load(&s_dr);
    switch (dr) {
    case DR_UP:
        return DESK_STATUS_MOVING_UP;
    case DR_DOWN:
        return DESK_STATUS_MOVING_DOWN;
    case DR_P1_GOTO:
    case DR_P4_GOTO:
    case DR_P1_SAVE:
    case DR_P4_SAVE:
        return DESK_STATUS_GOTO_PRESET;
    case DR_IDLE:
    default:
        return DESK_STATUS_IDLE;
    }
}

static desk_caps_t yd_get_caps(void)
{
    return (desk_caps_t){
        .hold_up_down = true,
#if CONFIG_DESK_TOF_ENABLE
        .raise_to_max = true,
        .preset_goto = true,
        .preset_save = false,
        .height = true,
        .preset_mask = (1u << 0) | (1u << 3), /* 1 and 4 */
#elif MXTARK_HEIGHT_INPUT_ENABLED
        .preset_goto = true,
        .preset_save = true,
        .height = true,
        .preset_mask = (1u << 0) | (1u << 3), /* 1 and 4 */
#else
        /* 没有真实高度时不能承诺档位闭环或安全限高。 */
        .preset_goto = false,
        .preset_save = false,
        .height = false,
        .preset_mask = 0,
#endif
    };
}

const desk_driver_t mxtark_driver = {
    .name = "mxtark",
    .init = yd_init,
    .deinit = yd_deinit,
    .stop = yd_stop,
    .hold_up = yd_hold_up,
    .hold_down = yd_hold_down,
    .raise_to_max = yd_raise_to_max,
    .reset_controller = yd_reset_controller,
    .goto_preset = yd_goto_preset,
    .goto_height_mm = yd_goto_height_mm,
    .save_preset = yd_save_preset,
    .get_height_mm = yd_get_height_mm,
    .set_max_height_mm = yd_set_max_height_mm,
    .set_preset_heights_mm = yd_set_preset_heights_mm,
    .is_upward_blocked = yd_is_upward_blocked,
    .set_panel_enabled = yd_set_panel_enabled,
    .get_status = yd_get_status,
    .get_caps = yd_get_caps,
};
