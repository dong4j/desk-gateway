/**
 * @file desk_tof.c
 * @brief TOF050C/VL6180X 与 TOF400C/VL53L1X 共享 I2C 总线的测距服务。
 *
 * 两颗芯片上电默认地址都是 0x29，因此必须先同时拉低 SHUT，再单独唤醒
 * VL6180X 并改到 0x30，最后唤醒仍使用 0x29 的 VL53L1X。地址不掉电保存，
 * 这段顺序必须在每次启动时执行。
 */
#include "desk_tof.h"

#include "desk_tof_filter.h"
#include "desk_tof_snapshot_logic.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "vl53l1x.h"

#include <stdatomic.h>
#include <stdint.h>

#define TOF_DEFAULT_ADDR 0x29
#define VL6180X_RUNTIME_ADDR 0x30
#define TOF_I2C_SPEED_HZ 400000
#define TOF_STALE_MS 1000U

#define VL6180X_MODEL_ID 0x000
#define VL6180X_SYSTEM_INTERRUPT_CONFIG_GPIO 0x014
#define VL6180X_SYSTEM_INTERRUPT_CLEAR 0x015
#define VL6180X_SYSTEM_FRESH_OUT_OF_RESET 0x016
#define VL6180X_SYSRANGE_START 0x018
#define VL6180X_SYSRANGE_INTERMEASUREMENT_PERIOD 0x01B
#define VL6180X_SYSRANGE_MAX_CONVERGENCE_TIME 0x01C
#define VL6180X_SYSRANGE_CROSSTALK_VALID_HEIGHT 0x021
#define VL6180X_SYSRANGE_PART_TO_PART_RANGE_OFFSET 0x024
#define VL6180X_SYSRANGE_RANGE_CHECK_ENABLES 0x02D
#define VL6180X_SYSRANGE_VHV_RECALIBRATE 0x02E
#define VL6180X_SYSRANGE_VHV_REPEAT_RATE 0x031
#define VL6180X_SYSALS_INTERMEASUREMENT_PERIOD 0x03E
#define VL6180X_SYSALS_ANALOGUE_GAIN 0x03F
#define VL6180X_SYSALS_INTEGRATION_PERIOD 0x040
#define VL6180X_RESULT_RANGE_STATUS 0x04D
#define VL6180X_RESULT_INTERRUPT_STATUS_GPIO 0x04F
#define VL6180X_RESULT_RANGE_VAL 0x062
#define VL6180X_RANGE_SCALER 0x096
#define VL6180X_READOUT_AVERAGING_SAMPLE_PERIOD 0x10A
#define VL6180X_I2C_SLAVE_DEVICE_ADDRESS 0x212
#define VL6180X_INTERLEAVED_MODE_ENABLE 0x2A3

static const char *TAG = "desk_tof";
static atomic_int s_height_mm = ATOMIC_VAR_INIT(-1);
static atomic_int s_raw_height_mm = ATOMIC_VAR_INIT(-1);
static atomic_int s_control_height_mm = ATOMIC_VAR_INIT(-1);
/* 偶数表示一组完整样本；写入期间使用相邻奇数，避免读到混合快照。 */
static atomic_uint_fast32_t s_height_publish_seq = ATOMIC_VAR_INIT(0);
static atomic_bool s_height_known = ATOMIC_VAR_INIT(false);
static atomic_int s_right_gap_mm = ATOMIC_VAR_INIT(-1);
static atomic_bool s_right_gap_known = ATOMIC_VAR_INIT(false);
static desk_tof_height_view_t s_last_good_height;
static bool s_started;
static i2c_master_bus_handle_t s_bus;

typedef struct {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t vl6180x;
    vl53l1x_t vl53l1x;
    bool wall_ready;
    bool height_ready;
    TickType_t wall_last_valid;
    TickType_t height_last_valid;
    desk_tof_stable_filter_t wall_filter;
    desk_tof_stable_filter_t height_filter;
    desk_tof_control_filter_t height_control_filter;
} desk_tof_context_t;

typedef struct {
    uint16_t reg;
    uint8_t value;
} vl6180x_reg8_t;

static esp_err_t vl6180x_write8(i2c_master_dev_handle_t dev,
                                uint16_t reg, uint8_t value)
{
    const uint8_t data[] = {
        (uint8_t)(reg >> 8),
        (uint8_t)reg,
        value,
    };
    return i2c_master_transmit(dev, data, sizeof(data), 100);
}

static esp_err_t vl6180x_write16(i2c_master_dev_handle_t dev,
                                 uint16_t reg, uint16_t value)
{
    const uint8_t data[] = {
        (uint8_t)(reg >> 8),
        (uint8_t)reg,
        (uint8_t)(value >> 8),
        (uint8_t)value,
    };
    return i2c_master_transmit(dev, data, sizeof(data), 100);
}

static esp_err_t vl6180x_read8(i2c_master_dev_handle_t dev,
                               uint16_t reg, uint8_t *out)
{
    const uint8_t address[] = {(uint8_t)(reg >> 8), (uint8_t)reg};
    return i2c_master_transmit_receive(dev, address, sizeof(address), out, 1,
                                       100);
}

static esp_err_t vl6180x_add_device(i2c_master_bus_handle_t bus,
                                    uint8_t address,
                                    i2c_master_dev_handle_t *out_dev)
{
    const i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = TOF_I2C_SPEED_HZ,
    };
    return i2c_master_bus_add_device(bus, &config, out_dev);
}

/** Apply ST AN4545 mandatory private-register settings after reset. */
static esp_err_t vl6180x_apply_private_settings(i2c_master_dev_handle_t dev)
{
    static const vl6180x_reg8_t settings[] = {
        {0x207, 0x01}, {0x208, 0x01}, {0x096, 0x00}, {0x097, 0xFD},
        {0x0E3, 0x01}, {0x0E4, 0x03}, {0x0E5, 0x02}, {0x0E6, 0x01},
        {0x0E7, 0x03}, {0x0F5, 0x02}, {0x0D9, 0x05}, {0x0DB, 0xCE},
        {0x0DC, 0x03}, {0x0DD, 0xF8}, {0x09F, 0x00}, {0x0A3, 0x3C},
        {0x0B7, 0x00}, {0x0BB, 0x3C}, {0x0B2, 0x09}, {0x0CA, 0x09},
        {0x198, 0x01}, {0x1B0, 0x17}, {0x1AD, 0x00}, {0x0FF, 0x05},
        {0x100, 0x05}, {0x199, 0x05}, {0x1A6, 0x1B}, {0x1AC, 0x3E},
        {0x1A7, 0x1F}, {0x030, 0x00},
    };
    for (size_t i = 0; i < sizeof(settings) / sizeof(settings[0]); ++i) {
        esp_err_t err = vl6180x_write8(dev, settings[i].reg,
                                       settings[i].value);
        if (err != ESP_OK) {
            return err;
        }
    }
    return vl6180x_write8(dev, VL6180X_SYSTEM_FRESH_OUT_OF_RESET, 0);
}

/** Configure 1x millimetre ranging at 10 Hz; ALS and GPIO interrupt are unused. */
static esp_err_t vl6180x_configure(i2c_master_dev_handle_t dev)
{
    uint8_t fresh = 0;
    uint8_t offset = 0;
    uint8_t range_checks = 0;
    esp_err_t err = vl6180x_read8(dev, VL6180X_SYSTEM_FRESH_OUT_OF_RESET,
                                  &fresh);
    if (err != ESP_OK) {
        return err;
    }
    err = vl6180x_read8(dev, VL6180X_SYSRANGE_PART_TO_PART_RANGE_OFFSET,
                        &offset);
    if (err != ESP_OK) {
        return err;
    }
    if (fresh == 1) {
        err = vl6180x_apply_private_settings(dev);
        if (err != ESP_OK) {
            return err;
        }
    }

    const vl6180x_reg8_t settings[] = {
        {VL6180X_READOUT_AVERAGING_SAMPLE_PERIOD, 0x30},
        {VL6180X_SYSALS_ANALOGUE_GAIN, 0x46},
        {VL6180X_SYSRANGE_VHV_REPEAT_RATE, 0xFF},
        {VL6180X_SYSRANGE_VHV_RECALIBRATE, 0x01},
        {VL6180X_SYSRANGE_INTERMEASUREMENT_PERIOD, 0x09},
        {VL6180X_SYSALS_INTERMEASUREMENT_PERIOD, 0x31},
        {VL6180X_SYSTEM_INTERRUPT_CONFIG_GPIO, 0x04},
        {VL6180X_SYSRANGE_MAX_CONVERGENCE_TIME, 0x31},
        {VL6180X_INTERLEAVED_MODE_ENABLE, 0x00},
        {VL6180X_SYSRANGE_PART_TO_PART_RANGE_OFFSET, offset},
        {VL6180X_SYSRANGE_CROSSTALK_VALID_HEIGHT, 20},
    };
    err = vl6180x_write16(dev, VL6180X_SYSALS_INTEGRATION_PERIOD, 0x0063);
    if (err != ESP_OK) {
        return err;
    }
    err = vl6180x_write16(dev, VL6180X_RANGE_SCALER, 253);
    if (err != ESP_OK) {
        return err;
    }
    for (size_t i = 0; i < sizeof(settings) / sizeof(settings[0]); ++i) {
        err = vl6180x_write8(dev, settings[i].reg, settings[i].value);
        if (err != ESP_OK) {
            return err;
        }
    }
    err = vl6180x_read8(dev, VL6180X_SYSRANGE_RANGE_CHECK_ENABLES,
                        &range_checks);
    if (err != ESP_OK) {
        return err;
    }
    return vl6180x_write8(dev, VL6180X_SYSRANGE_RANGE_CHECK_ENABLES,
                          range_checks | 0x01);
}

static esp_err_t init_wall_sensor(desk_tof_context_t *ctx)
{
    gpio_set_level(CONFIG_DESK_TOF_RIGHT_SHUT_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(5));

    i2c_master_dev_handle_t default_dev = NULL;
    esp_err_t err = vl6180x_add_device(ctx->bus, TOF_DEFAULT_ADDR,
                                       &default_dev);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t model_id = 0;
    err = vl6180x_read8(default_dev, VL6180X_MODEL_ID, &model_id);
    if (err == ESP_OK && model_id != 0xB4) {
        ESP_LOGW(TAG, "TOF050C unexpected model id: 0x%02x", model_id);
        err = ESP_ERR_NOT_FOUND;
    }
    if (err == ESP_OK) {
        err = vl6180x_configure(default_dev);
    }
    if (err == ESP_OK) {
        err = vl6180x_write8(default_dev,
                             VL6180X_I2C_SLAVE_DEVICE_ADDRESS,
                             VL6180X_RUNTIME_ADDR);
    }
    i2c_master_bus_rm_device(default_dev);
    if (err != ESP_OK) {
        return err;
    }
    err = vl6180x_add_device(ctx->bus, VL6180X_RUNTIME_ADDR, &ctx->vl6180x);
    if (err != ESP_OK) {
        return err;
    }
    err = vl6180x_write8(ctx->vl6180x, VL6180X_SYSRANGE_START, 0x03);
    if (err == ESP_OK) {
        ctx->wall_ready = true;
        ESP_LOGI(TAG, "TOF050C ready at 0x%02x", VL6180X_RUNTIME_ADDR);
    }
    return err;
}

static esp_err_t init_height_sensor(desk_tof_context_t *ctx)
{
    gpio_set_level(CONFIG_DESK_TOF_HEIGHT_SHUT_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    esp_err_t err = vl53l1x_init(&ctx->vl53l1x, ctx->bus, TOF_DEFAULT_ADDR);
    uint16_t model_id = 0;
    if (err == ESP_OK) {
        err = vl53l1x_get_sensor_id(&ctx->vl53l1x, &model_id);
    }
    if (err == ESP_OK && model_id != 0xEACC) {
        ESP_LOGW(TAG, "TOF400C unexpected model id: 0x%04x", model_id);
        err = ESP_ERR_NOT_FOUND;
    }
    if (err == ESP_OK) {
        err = vl53l1x_sensor_init(&ctx->vl53l1x);
    }
    if (err == ESP_OK) {
        /* 100 ms + full 16x16 ROI prioritises floor ranging stability. */
        err = vl53l1x_config_long_100ms(&ctx->vl53l1x);
    }
    if (err == ESP_OK) {
        err = vl53l1x_start(&ctx->vl53l1x);
    }
    if (err == ESP_OK) {
        ctx->height_ready = true;
        ESP_LOGI(TAG, "TOF400C ready at 0x%02x", TOF_DEFAULT_ADDR);
    }
    return err;
}

static bool read_wall_mm(desk_tof_context_t *ctx, int *out_mm)
{
    uint8_t interrupt_status = 0;
    if (vl6180x_read8(ctx->vl6180x,
                      VL6180X_RESULT_INTERRUPT_STATUS_GPIO,
                      &interrupt_status) != ESP_OK ||
        (interrupt_status & 0x04) == 0) {
        return false;
    }
    uint8_t range_status = 0;
    uint8_t range_mm = 0;
    esp_err_t status_err = vl6180x_read8(ctx->vl6180x,
                                         VL6180X_RESULT_RANGE_STATUS,
                                         &range_status);
    esp_err_t range_err = vl6180x_read8(ctx->vl6180x,
                                        VL6180X_RESULT_RANGE_VAL,
                                        &range_mm);
    (void)vl6180x_write8(ctx->vl6180x, VL6180X_SYSTEM_INTERRUPT_CLEAR, 0x01);
    if (status_err != ESP_OK || range_err != ESP_OK ||
        (range_status & 0xF0) != 0) {
        return false;
    }
    *out_mm = (int)range_mm + CONFIG_DESK_TOF_RIGHT_OFFSET_MM;
    return *out_mm >= 0;
}

static void publish_wall(desk_tof_context_t *ctx, int raw_mm)
{
    int filtered = desk_tof_stable_filter_push(&ctx->wall_filter, raw_mm);
    atomic_store(&s_right_gap_mm, filtered);
    atomic_store(&s_right_gap_known, true);
    ctx->wall_last_valid = xTaskGetTickCount();
}

static void publish_height(desk_tof_context_t *ctx, int raw_mm)
{
    if (raw_mm < 0) {
        return;
    }
    /* 产品高度就是 TOF400C 原始距离，不做物理桌高换算。 */
    int stable = desk_tof_stable_filter_push(&ctx->height_filter, raw_mm);
    int control = desk_tof_control_filter_push(&ctx->height_control_filter,
                                                raw_mm);
    /* 奇数窗口必须极短：读端只空转几次，这里禁止 delay / I2C / 日志。 */
    atomic_fetch_add(&s_height_publish_seq, 1U);
    atomic_store(&s_raw_height_mm, raw_mm);
    atomic_store(&s_control_height_mm, control);
    atomic_store(&s_height_mm, stable);
    atomic_store(&s_height_known, true);
    atomic_fetch_add(&s_height_publish_seq, 1U);
    ctx->height_last_valid = xTaskGetTickCount();
}

static bool sample_stale(TickType_t now, TickType_t last_valid)
{
    if (last_valid == 0) {
        return true;
    }
    return (now - last_valid) > pdMS_TO_TICKS(TOF_STALE_MS);
}

static void desk_tof_task(void *argument)
{
    (void)argument;
    desk_tof_context_t ctx = {.bus = s_bus};

    const gpio_config_t shut_config = {
        .pin_bit_mask = (1ULL << CONFIG_DESK_TOF_RIGHT_SHUT_GPIO) |
                        (1ULL << CONFIG_DESK_TOF_HEIGHT_SHUT_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&shut_config));
    gpio_set_level(CONFIG_DESK_TOF_RIGHT_SHUT_GPIO, 0);
    gpio_set_level(CONFIG_DESK_TOF_HEIGHT_SHUT_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));

    esp_err_t err = init_wall_sensor(&ctx);
    if (err != ESP_OK) {
        /* Keep a failed 0x29 device shut down so TOF400C can still start. */
        gpio_set_level(CONFIG_DESK_TOF_RIGHT_SHUT_GPIO, 0);
        ESP_LOGW(TAG, "TOF050C unavailable: %s", esp_err_to_name(err));
    }
    err = init_height_sensor(&ctx);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "TOF400C unavailable: %s", esp_err_to_name(err));
    }

    while (ctx.wall_ready || ctx.height_ready) {
        int wall_mm = 0;
        if (ctx.wall_ready && read_wall_mm(&ctx, &wall_mm)) {
            publish_wall(&ctx, wall_mm);
        }

        if (ctx.height_ready) {
            vl53l1x_result_t result = {0};
            err = vl53l1x_read(&ctx.vl53l1x, &result, 200);
            if (err == ESP_OK && result.status == 0) {
                publish_height(&ctx, result.distance_mm);
            }
        }

        TickType_t now = xTaskGetTickCount();
        if (sample_stale(now, ctx.wall_last_valid)) {
            atomic_store(&s_right_gap_known, false);
        }
        if (sample_stale(now, ctx.height_last_valid)) {
            atomic_store(&s_height_known, false);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGW(TAG, "no ToF sensor available; telemetry remains unknown");
    vTaskDelete(NULL);
}

esp_err_t desk_tof_start(i2c_master_bus_handle_t bus)
{
    if (!bus) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    s_bus = bus;
    BaseType_t created = xTaskCreate(desk_tof_task, "desk_tof", 6144, NULL, 5,
                                     NULL);
    if (created != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    return ESP_OK;
}

desk_tof_snapshot_t desk_tof_snapshot(void)
{
    desk_tof_snapshot_t snapshot = {0};
    unsigned failed_attempts = 0;
    bool height_copied = false;
    desk_tof_height_view_t fresh = {0};

    /*
     * 高优先级读端不能在奇数序号上死循环：写端 desk_tof 优先级更低，
     * 忙等会把它和 IDLE 一起饿死。vTaskDelay(1) 是一拍，不能改成
     * pdMS_TO_TICKS(1)（1 ms 在 10 ms tick 下会变成 0，等于没让出）。
     */
    for (;;) {
        uint_fast32_t begin_seq = atomic_load(&s_height_publish_seq);
        if (desk_tof_snapshot_seq_readable((uint32_t)begin_seq)) {
            fresh.height_mm = atomic_load(&s_height_mm);
            fresh.raw_height_mm = atomic_load(&s_raw_height_mm);
            fresh.control_height_mm = atomic_load(&s_control_height_mm);
            fresh.height_known = atomic_load(&s_height_known);
            uint_fast32_t end_seq = atomic_load(&s_height_publish_seq);
            if (desk_tof_snapshot_seq_consistent((uint32_t)begin_seq,
                                                 (uint32_t)end_seq)) {
                fresh.height_sample_id = (uint32_t)(end_seq / 2U);
                height_copied = true;
                break;
            }
        }

        desk_tof_snapshot_retry_t action =
            desk_tof_snapshot_retry_action(failed_attempts++);
        if (action == DESK_TOF_SNAPSHOT_RETRY_ABANDON) {
            break;
        }
        if (action == DESK_TOF_SNAPSHOT_RETRY_YIELD) {
            vTaskDelay(1);
        }
    }

    desk_tof_height_view_t resolved = {0};
    desk_tof_snapshot_resolve_height(height_copied, &fresh, &s_last_good_height,
                                     &resolved);
    snapshot.height_known = resolved.height_known;
    snapshot.height_mm = resolved.height_mm;
    snapshot.raw_height_mm = resolved.raw_height_mm;
    snapshot.control_height_mm = resolved.control_height_mm;
    snapshot.height_sample_id = resolved.height_sample_id;

    snapshot.right_gap_mm = atomic_load(&s_right_gap_mm);
    snapshot.right_gap_known = atomic_load(&s_right_gap_known);
    if (!snapshot.right_gap_known) {
        snapshot.right_gap_mm = -1;
    }
    return snapshot;
}
