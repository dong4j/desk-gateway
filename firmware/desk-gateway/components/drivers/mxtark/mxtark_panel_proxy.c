/**
 * @file mxtark_panel_proxy.c
 * @brief GPIO6/7 上的原厂 TM1650 面板事务代理。
 *
 * 控制盒侧继续使用稳定的 ESP32-S3 硬件 I2C Slave @0x24。面板侧使用
 * 开漏 GPIO 软件 Master，避免占用双 ToF/OLED 所在的 I2C1。任务只缓存按键
 * 并把 ToF 实测高度写回原厂数码管，任何 NACK 或总线超时都会立即发布空闲。
 */
#include "mxtark_panel_proxy.h"

#include "desk_tof.h"
#include "mxtark_panel_arbiter.h"
#include "mxtark_panel_display.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <stddef.h>

#if CONFIG_DESK_MXTARK_PANEL_PROXY

static const char *TAG = "mxtark_panel";

#define PANEL_KEY_ADDR_7BIT       0x24u
#define PANEL_DIG1_ADDR_7BIT      0x34u
#define PANEL_DIG2_ADDR_7BIT      0x35u
#define PANEL_DIG3_ADDR_7BIT      0x36u
#define PANEL_DIG4_ADDR_7BIT      0x37u
#define PANEL_IDLE_DR             0x2Eu
#define PANEL_CONTROL_DW          0x01u
#define PANEL_HALF_PERIOD_US      52u
#define PANEL_WRITE_READ_GAP_US   30u
#define PANEL_LINE_TIMEOUT_US     1000u
#define PANEL_POLL_INTERVAL_MS    20u
#define PANEL_DISPLAY_REFRESH_MS  100u
#define PANEL_UNKNOWN_LOG_MS      2000u

typedef struct {
    mxtark_panel_key_callback_t key_callback;
    void *callback_ctx;
} panel_proxy_ctx_t;

static panel_proxy_ctx_t s_panel;

/** Open-drain 高电平表示释放线路，真正高电平由原厂面板上拉产生。 */
static inline void release_line(gpio_num_t gpio)
{
    (void)gpio_set_level(gpio, 1);
}

/** 软件 Master 只主动拉低线路，绝不主动输出高电平。 */
static inline void drive_line_low(gpio_num_t gpio)
{
    (void)gpio_set_level(gpio, 0);
}

/** 等待释放后的线路变高，并为短路、断电或异常占线提供有界超时。 */
static esp_err_t wait_line_high(gpio_num_t gpio)
{
    for (uint32_t waited_us = 0; waited_us < PANEL_LINE_TIMEOUT_US;
         waited_us += 2u) {
        if (gpio_get_level(gpio)) {
            return ESP_OK;
        }
        esp_rom_delay_us(2);
    }
    return ESP_ERR_TIMEOUT;
}

/** 产生 START；进入事务前要求两根线路都已释放。 */
static esp_err_t panel_start(void)
{
    esp_err_t err;
    release_line((gpio_num_t)CONFIG_DESK_PANEL_I2C_SDA_GPIO);
    release_line((gpio_num_t)CONFIG_DESK_PANEL_I2C_SCL_GPIO);
    err = wait_line_high((gpio_num_t)CONFIG_DESK_PANEL_I2C_SCL_GPIO);
    if (err != ESP_OK) {
        return err;
    }
    err = wait_line_high((gpio_num_t)CONFIG_DESK_PANEL_I2C_SDA_GPIO);
    if (err != ESP_OK) {
        return err;
    }
    esp_rom_delay_us(PANEL_HALF_PERIOD_US);
    drive_line_low((gpio_num_t)CONFIG_DESK_PANEL_I2C_SDA_GPIO);
    esp_rom_delay_us(PANEL_HALF_PERIOD_US);
    drive_line_low((gpio_num_t)CONFIG_DESK_PANEL_I2C_SCL_GPIO);
    return ESP_OK;
}

/** 无论前序事务是否 NACK，都尽力释放总线并产生 STOP。 */
static esp_err_t panel_stop(void)
{
    drive_line_low((gpio_num_t)CONFIG_DESK_PANEL_I2C_SDA_GPIO);
    esp_rom_delay_us(PANEL_HALF_PERIOD_US);
    release_line((gpio_num_t)CONFIG_DESK_PANEL_I2C_SCL_GPIO);
    esp_err_t err =
        wait_line_high((gpio_num_t)CONFIG_DESK_PANEL_I2C_SCL_GPIO);
    esp_rom_delay_us(PANEL_HALF_PERIOD_US);
    release_line((gpio_num_t)CONFIG_DESK_PANEL_I2C_SDA_GPIO);
    esp_rom_delay_us(PANEL_HALF_PERIOD_US);
    return err;
}

/** 在 SCL 低电平阶段设置一位，再按实测约 9.6 kHz 时钟发送。 */
static esp_err_t panel_write_bit(bool high)
{
    if (high) {
        release_line((gpio_num_t)CONFIG_DESK_PANEL_I2C_SDA_GPIO);
    } else {
        drive_line_low((gpio_num_t)CONFIG_DESK_PANEL_I2C_SDA_GPIO);
    }
    esp_rom_delay_us(PANEL_HALF_PERIOD_US);
    release_line((gpio_num_t)CONFIG_DESK_PANEL_I2C_SCL_GPIO);
    esp_err_t err =
        wait_line_high((gpio_num_t)CONFIG_DESK_PANEL_I2C_SCL_GPIO);
    if (err != ESP_OK) {
        return err;
    }
    esp_rom_delay_us(PANEL_HALF_PERIOD_US);
    drive_line_low((gpio_num_t)CONFIG_DESK_PANEL_I2C_SCL_GPIO);
    return ESP_OK;
}

/** 释放 SDA 后在 SCL 高电平中部采样一位。 */
static esp_err_t panel_read_bit(bool *out_high)
{
    if (!out_high) {
        return ESP_ERR_INVALID_ARG;
    }
    release_line((gpio_num_t)CONFIG_DESK_PANEL_I2C_SDA_GPIO);
    esp_rom_delay_us(PANEL_HALF_PERIOD_US);
    release_line((gpio_num_t)CONFIG_DESK_PANEL_I2C_SCL_GPIO);
    esp_err_t err =
        wait_line_high((gpio_num_t)CONFIG_DESK_PANEL_I2C_SCL_GPIO);
    if (err != ESP_OK) {
        return err;
    }
    esp_rom_delay_us(PANEL_HALF_PERIOD_US);
    *out_high = gpio_get_level(CONFIG_DESK_PANEL_I2C_SDA_GPIO) != 0;
    drive_line_low((gpio_num_t)CONFIG_DESK_PANEL_I2C_SCL_GPIO);
    return ESP_OK;
}

/** 发送一个字节并要求 TM1650 在第九个时钟返回 ACK。 */
static esp_err_t panel_write_byte(uint8_t byte)
{
    for (uint8_t mask = 0x80u; mask != 0; mask >>= 1) {
        esp_err_t err = panel_write_bit((byte & mask) != 0);
        if (err != ESP_OK) {
            return err;
        }
    }
    bool nack = true;
    esp_err_t err = panel_read_bit(&nack);
    if (err != ESP_OK) {
        return err;
    }
    return nack ? ESP_ERR_NOT_FOUND : ESP_OK;
}

/** 读取一个字节并按原始抓包发送 ACK，STOP 由调用方随后产生。 */
static esp_err_t panel_read_byte(uint8_t *out_byte)
{
    if (!out_byte) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t byte = 0;
    for (int bit = 0; bit < 8; ++bit) {
        bool high = false;
        esp_err_t err = panel_read_bit(&high);
        if (err != ESP_OK) {
            return err;
        }
        byte = (uint8_t)((byte << 1) | (high ? 1u : 0u));
    }
    esp_err_t err = panel_write_bit(false);
    if (err != ESP_OK) {
        return err;
    }
    *out_byte = byte;
    return ESP_OK;
}

/** 发送一笔完整的 address+data+STOP 写事务。 */
static esp_err_t write_panel_register(uint8_t address, uint8_t data)
{
    esp_err_t err = panel_start();
    if (err == ESP_OK) {
        err = panel_write_byte((uint8_t)(address << 1));
    }
    if (err == ESP_OK) {
        err = panel_write_byte(data);
    }
    esp_err_t stop_err = panel_stop();
    return err != ESP_OK ? err : stop_err;
}

/** 发送一笔完整的 address+read+ACK+STOP 读事务。 */
static esp_err_t read_panel_register(uint8_t address, uint8_t *out_data)
{
    esp_err_t err = panel_start();
    if (err == ESP_OK) {
        err = panel_write_byte((uint8_t)((address << 1) | 1u));
    }
    if (err == ESP_OK) {
        err = panel_read_byte(out_data);
    }
    esp_err_t stop_err = panel_stop();
    return err != ESP_OK ? err : stop_err;
}

/** 执行抓包中确认的 STOP 分隔按键轮询。 */
static esp_err_t poll_panel_key(uint8_t *dr)
{
    if (!dr) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err =
        write_panel_register(PANEL_KEY_ADDR_7BIT, PANEL_CONTROL_DW);
    if (err != ESP_OK) {
        return err;
    }
    esp_rom_delay_us(PANEL_WRITE_READ_GAP_US);
    return read_panel_register(PANEL_KEY_ADDR_7BIT, dr);
}

/** 用 TOF400C 的实测高度维持原厂三位数码管显示。 */
static esp_err_t refresh_panel_height(int previous_height_cm,
                                      int *out_height_cm)
{
    if (!out_height_cm) {
        return ESP_ERR_INVALID_ARG;
    }
    desk_tof_snapshot_t tof = desk_tof_snapshot();
    if (!tof.height_known) {
        return ESP_ERR_INVALID_STATE;
    }

    mxtark_panel_display_frame_t frame;
    if (!mxtark_panel_display_encode_height(tof.height_mm, &frame)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (frame.height_cm == previous_height_cm) {
        *out_height_cm = frame.height_cm;
        return ESP_OK;
    }

    /* 原厂控制盒的干净刷新顺序为 DIG3 -> DIG2 -> DIG1 -> DIG4。 */
    esp_err_t err =
        write_panel_register(PANEL_DIG3_ADDR_7BIT, frame.digits[2]);
    if (err == ESP_OK) {
        err = write_panel_register(PANEL_DIG2_ADDR_7BIT, frame.digits[1]);
    }
    if (err == ESP_OK) {
        err = write_panel_register(PANEL_DIG1_ADDR_7BIT, frame.digits[0]);
    }
    if (err == ESP_OK) {
        err = write_panel_register(PANEL_DIG4_ADDR_7BIT, frame.digits[3]);
    }
    if (err != ESP_OK) {
        return err;
    }
    *out_height_cm = frame.height_cm;
    return ESP_OK;
}

/**
 * 周期轮询面板按键；断线和 NACK 在一个轮询周期内退回空闲。
 *
 * 20 ms 周期把最坏停止延迟保持在人体按键可感知范围内，同时避免 9.6 kHz
 * 软件时钟持续占满 CPU。数码管只在厘米值变化或面板重连后刷新。
 */
static void panel_proxy_task(void *arg)
{
    (void)arg;
    bool connected = false;
    uint8_t published_dr = PANEL_IDLE_DR;
    int displayed_height_cm = -1;
    TickType_t last_display_tick = 0;
    TickType_t last_unknown_log_tick = 0;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        uint8_t dr = PANEL_IDLE_DR;
        esp_err_t err = poll_panel_key(&dr);
        bool next_connected = err == ESP_OK;
        if (!next_connected) {
            dr = PANEL_IDLE_DR;
        }

        TickType_t now = xTaskGetTickCount();
        uint8_t normalized_dr = mxtark_panel_arbiter_normalize_dr(
            dr, PANEL_IDLE_DR);
        bool unknown_dr = next_connected && dr != normalized_dr;
        bool connection_changed = next_connected != connected;
        bool dr_changed = dr != published_dr;
        if (connection_changed || dr_changed) {
            if (connection_changed) {
                if (next_connected) {
                    ESP_LOGI(TAG, "original panel connected raw DR=0x%02X",
                             dr);
                    displayed_height_cm = -1;
                } else {
                    ESP_LOGI(TAG, "original panel disconnected");
                }
            } else if (next_connected && !unknown_dr) {
                ESP_LOGI(TAG, "panel raw DR=0x%02X", dr);
            }
            if (unknown_dr &&
                (last_unknown_log_tick == 0 ||
                 now - last_unknown_log_tick >=
                     pdMS_TO_TICKS(PANEL_UNKNOWN_LOG_MS))) {
                ESP_LOGW(TAG,
                         "ignore unknown panel DR=0x%02X and release priority",
                         dr);
                last_unknown_log_tick = now;
            }
            connected = next_connected;
            published_dr = dr;
            s_panel.key_callback(connected, published_dr,
                                 s_panel.callback_ctx);
        }

        if (connected &&
            (last_display_tick == 0 ||
             now - last_display_tick >=
                 pdMS_TO_TICKS(PANEL_DISPLAY_REFRESH_MS))) {
            int height_cm = -1;
            esp_err_t display_err =
                refresh_panel_height(displayed_height_cm, &height_cm);
            if (display_err == ESP_OK) {
                if (height_cm != displayed_height_cm) {
                    ESP_LOGD(TAG, "panel height=%d cm", height_cm);
                }
                displayed_height_cm = height_cm;
            }
            last_display_tick = now;
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(PANEL_POLL_INTERVAL_MS));
    }
}

esp_err_t mxtark_panel_proxy_init(
    mxtark_panel_key_callback_t key_callback, void *callback_ctx)
{
    ESP_RETURN_ON_FALSE(key_callback, ESP_ERR_INVALID_ARG, TAG,
                        "key callback is required");
    ESP_RETURN_ON_FALSE(CONFIG_DESK_PANEL_I2C_SCL_GPIO !=
                            CONFIG_DESK_PANEL_I2C_SDA_GPIO,
                        ESP_ERR_INVALID_ARG, TAG,
                        "panel CLK and DAT GPIO must differ");
    ESP_RETURN_ON_FALSE(CONFIG_DESK_PANEL_I2C_SCL_GPIO !=
                                CONFIG_DESK_I2C_SCL_GPIO &&
                            CONFIG_DESK_PANEL_I2C_SCL_GPIO !=
                                CONFIG_DESK_I2C_SDA_GPIO &&
                            CONFIG_DESK_PANEL_I2C_SDA_GPIO !=
                                CONFIG_DESK_I2C_SCL_GPIO &&
                            CONFIG_DESK_PANEL_I2C_SDA_GPIO !=
                                CONFIG_DESK_I2C_SDA_GPIO,
                        ESP_ERR_INVALID_ARG, TAG,
                        "panel and control-box buses must be isolated");
#if CONFIG_DESK_TOF_ENABLE
    ESP_RETURN_ON_FALSE(CONFIG_DESK_PANEL_I2C_SCL_GPIO !=
                                CONFIG_DESK_TOF_I2C_SCL_GPIO &&
                            CONFIG_DESK_PANEL_I2C_SCL_GPIO !=
                                CONFIG_DESK_TOF_I2C_SDA_GPIO &&
                            CONFIG_DESK_PANEL_I2C_SDA_GPIO !=
                                CONFIG_DESK_TOF_I2C_SCL_GPIO &&
                            CONFIG_DESK_PANEL_I2C_SDA_GPIO !=
                                CONFIG_DESK_TOF_I2C_SDA_GPIO,
                        ESP_ERR_INVALID_ARG, TAG,
                        "panel and ToF/OLED GPIO must differ");
#endif

    gpio_config_t bus_config = {
        .pin_bit_mask =
            (1ULL << CONFIG_DESK_PANEL_I2C_SCL_GPIO) |
            (1ULL << CONFIG_DESK_PANEL_I2C_SDA_GPIO),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&bus_config), TAG,
                        "configure software panel bus");
    release_line((gpio_num_t)CONFIG_DESK_PANEL_I2C_SCL_GPIO);
    release_line((gpio_num_t)CONFIG_DESK_PANEL_I2C_SDA_GPIO);

    s_panel = (panel_proxy_ctx_t){
        .key_callback = key_callback,
        .callback_ctx = callback_ctx,
    };
    if (xTaskCreatePinnedToCore(panel_proxy_task, "yd_panel", 4096, NULL,
                                configMAX_PRIORITIES - 5, NULL, 1) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG,
             "software panel proxy SCL=%d SDA=%d 9.6kHz split-STOP ACK+STOP",
             CONFIG_DESK_PANEL_I2C_SCL_GPIO,
             CONFIG_DESK_PANEL_I2C_SDA_GPIO);
    return ESP_OK;
}

#else

esp_err_t mxtark_panel_proxy_init(
    mxtark_panel_key_callback_t key_callback, void *callback_ctx)
{
    (void)key_callback;
    (void)callback_ctx;
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
