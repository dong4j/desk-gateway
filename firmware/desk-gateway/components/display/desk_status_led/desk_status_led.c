/**
 * @file desk_status_led.c
 * @brief 把已冻结的红黄蓝语义写到 GPIO1/2/8。
 *
 * 不占用 desk_core 的单一 event listener（BLE 已经在用）。250 ms 轮询
 * 快照即可；GPIO 配置失败只让灯不可用。
 */
#include "desk_status_led.h"

#include "desk_core.h"
#include "desk_status_led_logic.h"
#include "desk_wifi.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if CONFIG_DESK_STATUS_LED_ENABLE

#define LED_REFRESH_MS 250U

static const char *TAG = "desk_status_led";
static bool s_started;

static bool pins_are_unique(void)
{
    return CONFIG_DESK_STATUS_LED_RED_GPIO !=
               CONFIG_DESK_STATUS_LED_YELLOW_GPIO &&
           CONFIG_DESK_STATUS_LED_RED_GPIO !=
               CONFIG_DESK_STATUS_LED_BLUE_GPIO &&
           CONFIG_DESK_STATUS_LED_YELLOW_GPIO !=
               CONFIG_DESK_STATUS_LED_BLUE_GPIO;
}

static void apply_output(const desk_status_led_output_t *out)
{
    gpio_set_level((gpio_num_t)CONFIG_DESK_STATUS_LED_RED_GPIO,
                   out->red ? 1 : 0);
    gpio_set_level((gpio_num_t)CONFIG_DESK_STATUS_LED_YELLOW_GPIO,
                   out->yellow ? 1 : 0);
    gpio_set_level((gpio_num_t)CONFIG_DESK_STATUS_LED_BLUE_GPIO,
                   out->blue ? 1 : 0);
}

static void desk_status_led_task(void *argument)
{
    (void)argument;
    desk_status_led_output_t previous = {0};
    while (true) {
        desk_core_snapshot_t core = desk_core_snapshot();
        desk_status_led_input_t in = {
            .moving = core.status == DESK_STATUS_MOVING_UP ||
                      core.status == DESK_STATUS_MOVING_DOWN ||
                      core.status == DESK_STATUS_GOTO_PRESET,
            .fault = core.status == DESK_STATUS_ERROR,
            .child_lock = core.child_lock,
            .upward_blocked = core.upward_blocked,
            .wifi_ap = desk_wifi_is_ap_active(),
            .wifi_connected = desk_wifi_is_connected(),
        };
        desk_status_led_output_t out;
        desk_status_led_evaluate(&in, &out);
        if (out.red != previous.red || out.yellow != previous.yellow ||
            out.blue != previous.blue) {
            apply_output(&out);
            previous = out;
        }
        vTaskDelay(pdMS_TO_TICKS(LED_REFRESH_MS));
    }
}

esp_err_t desk_status_led_start(void)
{
    if (s_started) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!pins_are_unique()) {
        ESP_LOGE(TAG, "status LED GPIOs must differ");
        return ESP_ERR_INVALID_ARG;
    }

    const gpio_config_t led_config = {
        .pin_bit_mask = (1ULL << CONFIG_DESK_STATUS_LED_RED_GPIO) |
                        (1ULL << CONFIG_DESK_STATUS_LED_YELLOW_GPIO) |
                        (1ULL << CONFIG_DESK_STATUS_LED_BLUE_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&led_config);
    if (err != ESP_OK) {
        return err;
    }
    /* 先灭灯，避免上电浮空微亮一直持续到第一次轮询。 */
    desk_status_led_output_t off = {0};
    apply_output(&off);

    BaseType_t created = xTaskCreate(desk_status_led_task, "desk_status_led",
                                     3072, NULL, 4, NULL);
    if (created != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    ESP_LOGI(TAG, "status LEDs red=%d yellow=%d blue=%d",
             CONFIG_DESK_STATUS_LED_RED_GPIO,
             CONFIG_DESK_STATUS_LED_YELLOW_GPIO,
             CONFIG_DESK_STATUS_LED_BLUE_GPIO);
    return ESP_OK;
}

#else

esp_err_t desk_status_led_start(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

#endif
