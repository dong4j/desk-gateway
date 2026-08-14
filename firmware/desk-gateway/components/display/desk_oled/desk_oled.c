/**
 * @file desk_oled.c
 * @brief SSD1306 128x32 I2C 驱动与 Desk Gateway 状态轮播任务。
 *
 * 驱动只实现本项目需要的 ASCII 子集，避免引入完整图形库。OLED 在启动后
 * 自动探测 0x3C/0x3D；缺失或运行期写入失败只记录一次，不影响共享总线。
 */
#include "desk_oled.h"

#include "desk_ble.h"
#include "desk_core.h"
#include "desk_oled_pages.h"
#include "desk_tof.h"
#include "desk_wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define OLED_WIDTH 128
#define OLED_HEIGHT 32
#define OLED_BUFFER_SIZE (OLED_WIDTH * OLED_HEIGHT / 8)
#define OLED_I2C_SPEED_HZ 400000
#define OLED_REFRESH_MS 250U
#define OLED_START_DELAY_MS 1200U

static const char *TAG = "desk_oled";
static i2c_master_bus_handle_t s_bus;
static bool s_started;

typedef struct {
    char character;
    uint8_t columns[5];
} glyph_t;

static const glyph_t GLYPHS[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
    {'/', {0x20, 0x10, 0x08, 0x04, 0x02}},
    {':', {0x00, 0x36, 0x36, 0x00, 0x00}},
    {'?', {0x02, 0x01, 0x51, 0x09, 0x06}},
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
    {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
    {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
    {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
    {'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'J', {0x20, 0x40, 0x41, 0x3F, 0x01}},
    {'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
    {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}},
    {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}},
    {'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
    {'Y', {0x07, 0x08, 0x70, 0x08, 0x07}},
    {'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
};

static const uint8_t *find_glyph(char character)
{
    char upper = (char)toupper((unsigned char)character);
    for (size_t i = 0; i < sizeof(GLYPHS) / sizeof(GLYPHS[0]); ++i) {
        if (GLYPHS[i].character == upper) {
            return GLYPHS[i].columns;
        }
    }
    return GLYPHS[5].columns; /* '?' */
}

static void set_pixel(uint8_t *buffer, int x, int y)
{
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) {
        return;
    }
    buffer[(y / 8) * OLED_WIDTH + x] |= (uint8_t)(1U << (y % 8));
}

static void draw_character(uint8_t *buffer, int x, int y, char character,
                           int scale)
{
    const uint8_t *columns = find_glyph(character);
    for (int column = 0; column < 5; ++column) {
        for (int row = 0; row < 7; ++row) {
            if ((columns[column] & (1U << row)) == 0) {
                continue;
            }
            for (int dx = 0; dx < scale; ++dx) {
                for (int dy = 0; dy < scale; ++dy) {
                    set_pixel(buffer, x + column * scale + dx,
                              y + row * scale + dy);
                }
            }
        }
    }
}

static void render_frame(const desk_oled_frame_t *frame, uint8_t *buffer)
{
    memset(buffer, 0, OLED_BUFFER_SIZE);
    int scale = frame->scale > 0 ? frame->scale : 1;
    int row_height = 8 * scale;
    int advance = 6 * scale;
    for (int row = 0; row < frame->row_count; ++row) {
        int x = 0;
        int y = row * row_height;
        for (size_t i = 0; frame->rows[row][i] != '\0'; ++i) {
            if (x + 5 * scale > OLED_WIDTH) {
                break;
            }
            draw_character(buffer, x, y, frame->rows[row][i], scale);
            x += advance;
        }
    }
}

static esp_err_t send_commands(i2c_master_dev_handle_t device,
                               const uint8_t *commands, size_t count)
{
    if (count > 31U) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint8_t data[32] = {0x00};
    memcpy(&data[1], commands, count);
    return i2c_master_transmit(device, data, count + 1U, 100);
}

static esp_err_t write_buffer(i2c_master_dev_handle_t device,
                              const uint8_t *buffer)
{
    static const uint8_t address_window[] = {0x21, 0x00, 0x7F,
                                              0x22, 0x00, 0x03};
    esp_err_t err = send_commands(device, address_window,
                                  sizeof(address_window));
    if (err != ESP_OK) {
        return err;
    }
    uint8_t transfer[OLED_BUFFER_SIZE + 1U];
    transfer[0] = 0x40;
    memcpy(&transfer[1], buffer, OLED_BUFFER_SIZE);
    return i2c_master_transmit(device, transfer, sizeof(transfer), 100);
}

static esp_err_t init_display(i2c_master_dev_handle_t device)
{
    static const uint8_t init_commands[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x1F, 0xD3, 0x00, 0x40,
        // Segment remap and COM scan direction must change together for a 180-degree rotation.
        0x8D, 0x14, 0x20, 0x00, 0xA0, 0xC0, 0xDA, 0x02,
        0x81, 0x8F, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6,
        0xAF,
    };
    return send_commands(device, init_commands, sizeof(init_commands));
}

static esp_err_t attach_display(i2c_master_dev_handle_t *out_device,
                                uint8_t *out_address)
{
    static const uint8_t addresses[] = {0x3C, 0x3D};
    for (size_t i = 0; i < sizeof(addresses); ++i) {
        if (i2c_master_probe(s_bus, addresses[i], 50) != ESP_OK) {
            continue;
        }
        const i2c_device_config_t config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = addresses[i],
            .scl_speed_hz = OLED_I2C_SPEED_HZ,
        };
        esp_err_t err = i2c_master_bus_add_device(s_bus, &config, out_device);
        if (err == ESP_OK) {
            *out_address = addresses[i];
        }
        return err;
    }
    return ESP_ERR_NOT_FOUND;
}

static const char *motion_name(desk_status_t status)
{
    switch (status) {
    case DESK_STATUS_MOVING_UP:
        return "UP";
    case DESK_STATUS_MOVING_DOWN:
        return "DOWN";
    case DESK_STATUS_GOTO_PRESET:
        return "PRESET";
    case DESK_STATUS_ERROR:
        return "ERROR";
    default:
        return "IDLE";
    }
}

static size_t connected_ble_count(const desk_ble_management_snapshot_t *ble)
{
    size_t count = 0;
    for (size_t i = 0; i < ble->device_count; ++i) {
        if (ble->devices[i].connected) {
            count++;
        }
    }
    return count;
}

static void desk_oled_task(void *argument)
{
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(OLED_START_DELAY_MS));

    i2c_master_dev_handle_t device = NULL;
    uint8_t address = 0;
    esp_err_t err = attach_display(&device, &address);
    if (err != ESP_OK || init_display(device) != ESP_OK) {
        ESP_LOGW(TAG, "SSD1306 not available at 0x3C/0x3D");
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "SSD1306 128x32 ready at 0x%02x", address);

    uint8_t previous[OLED_BUFFER_SIZE];
    memset(previous, 0xFF, sizeof(previous));
    bool write_error_logged = false;
    while (true) {
        desk_core_snapshot_t core = desk_core_snapshot();
        desk_tof_snapshot_t tof = desk_tof_snapshot();
        desk_ble_management_snapshot_t ble = {
            .capacity = DESK_BLE_MANAGEMENT_MAX_DEVICES,
        };
        (void)desk_ble_get_management_snapshot(&ble);

        char ip[16] = "NO IP";
        bool wifi_ap = desk_wifi_is_ap_active();
        bool wifi_connected = desk_wifi_is_connected();
        if (wifi_ap || wifi_connected) {
            (void)desk_wifi_get_ip(ip, sizeof(ip));
        }
        const desk_oled_page_data_t data = {
            .height_mm = tof.height_mm,
            .height_known = tof.height_known,
            .right_gap_mm = tof.right_gap_mm,
            .right_gap_known = tof.right_gap_known,
            .motion_name = motion_name(core.status),
            .moving = core.status == DESK_STATUS_MOVING_UP ||
                      core.status == DESK_STATUS_MOVING_DOWN ||
                      core.status == DESK_STATUS_GOTO_PRESET,
            .child_lock = core.child_lock,
            .wifi_connected = wifi_connected,
            .wifi_ap = wifi_ap,
            .ip_address = ip,
            .ble_connected = connected_ble_count(&ble),
            .ble_bonded = ble.device_count,
            .ble_capacity = ble.capacity,
        };
        bool sensor_offline = !tof.height_known || !tof.right_gap_known;
        desk_oled_page_t page = desk_oled_choose_page(
            esp_log_timestamp(), sensor_offline, data.moving);
        desk_oled_frame_t frame;
        uint8_t next[OLED_BUFFER_SIZE];
        desk_oled_build_frame(page, &data, &frame);
        render_frame(&frame, next);

        if (memcmp(previous, next, sizeof(next)) != 0) {
            err = write_buffer(device, next);
            if (err == ESP_OK) {
                memcpy(previous, next, sizeof(previous));
                write_error_logged = false;
            } else if (!write_error_logged) {
                ESP_LOGW(TAG, "display update failed: %s",
                         esp_err_to_name(err));
                write_error_logged = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(OLED_REFRESH_MS));
    }
}

esp_err_t desk_oled_start(i2c_master_bus_handle_t bus)
{
    if (!bus) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    s_bus = bus;
    BaseType_t created = xTaskCreate(desk_oled_task, "desk_oled", 4096, NULL,
                                     4, NULL);
    if (created != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    return ESP_OK;
}
