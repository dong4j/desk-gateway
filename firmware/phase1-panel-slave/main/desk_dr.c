/**
 * @file desk_dr.c
 * @brief DR 状态与安全超时
 *
 * 为什么单独成模块：I²C 回调只读当前 DR；串口/后续 WiFi 只写 DR。
 * 升/降超时集中在这里，避免各入口漏写 stop。
 */
#include "desk_dr.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include <stdatomic.h>

static const char *TAG = "desk_dr";

static atomic_uint_fast8_t s_dr;
static esp_timer_handle_t s_hold_timer;

/** 仅允许抓包验证过的键码，防止串口误输入把桌子带到未知状态 */
static bool desk_dr_is_known(uint8_t dr)
{
    switch (dr) {
    case DESK_DR_IDLE:
    case DESK_DR_UP:
    case DESK_DR_DOWN:
    case DESK_DR_PRESET1_GOTO:
    case DESK_DR_PRESET1_SAVE:
    case DESK_DR_PRESET4_GOTO:
    case DESK_DR_PRESET4_SAVE:
        return true;
    default:
        return false;
    }
}

static void hold_timer_cb(void *arg)
{
    (void)arg;
    /* 超时 / 脉冲结束：停止优先 */
    atomic_store(&s_dr, DESK_DR_IDLE);
    ESP_LOGW(TAG, "hold timeout -> DR=0x%02X (idle)", DESK_DR_IDLE);
}

static void cancel_hold_timer(void)
{
    if (s_hold_timer && esp_timer_is_active(s_hold_timer)) {
        esp_timer_stop(s_hold_timer);
    }
}

/**
 * 升/降是「按住」语义：最长连续运动上限。
 * 经 `dr XX` 直接写入的 goto/save 也要自动收回 idle，避免一直卡在短/长码。
 */
static void arm_hold_for_dr(uint8_t dr)
{
    cancel_hold_timer();
    uint32_t ms = 0;
    if (dr == DESK_DR_UP || dr == DESK_DR_DOWN) {
        ms = CONFIG_DESK_MOTION_TIMEOUT_MS;
    } else if (dr == DESK_DR_PRESET1_GOTO || dr == DESK_DR_PRESET4_GOTO) {
        ms = CONFIG_DESK_GOTO_HOLD_MS;
    } else if (dr == DESK_DR_PRESET1_SAVE || dr == DESK_DR_PRESET4_SAVE) {
        ms = CONFIG_DESK_SAVE_HOLD_MS;
    }
    if (ms > 0) {
        ESP_ERROR_CHECK(esp_timer_start_once(s_hold_timer, (uint64_t)ms * 1000ULL));
    }
}

void desk_dr_init(void)
{
    atomic_store(&s_dr, DESK_DR_IDLE);

    const esp_timer_create_args_t args = {
        .callback = &hold_timer_cb,
        .name = "desk_hold",
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &s_hold_timer));
    ESP_LOGI(TAG, "init DR=0x%02X idle; motion timeout %d ms",
             DESK_DR_IDLE, CONFIG_DESK_MOTION_TIMEOUT_MS);
}

uint8_t desk_dr_get(void)
{
    return (uint8_t)atomic_load(&s_dr);
}

bool desk_dr_set(uint8_t dr)
{
    if (!desk_dr_is_known(dr)) {
        ESP_LOGW(TAG, "reject unknown DR=0x%02X (preset2/3 not verified)", dr);
        return false;
    }
    atomic_store(&s_dr, dr);
    if (dr == DESK_DR_IDLE) {
        cancel_hold_timer();
    } else {
        arm_hold_for_dr(dr);
    }
    ESP_LOGI(TAG, "DR -> 0x%02X (%s)", dr, desk_dr_name(dr));
    return true;
}

void desk_dr_stop(void)
{
    cancel_hold_timer();
    atomic_store(&s_dr, DESK_DR_IDLE);
    ESP_LOGI(TAG, "STOP -> DR=0x%02X", DESK_DR_IDLE);
}

bool desk_dr_pulse(uint8_t dr, uint32_t hold_ms)
{
    if (!desk_dr_is_known(dr) || dr == DESK_DR_IDLE) {
        return desk_dr_set(dr);
    }
    if (hold_ms == 0) {
        if (dr == DESK_DR_PRESET1_SAVE || dr == DESK_DR_PRESET4_SAVE) {
            hold_ms = CONFIG_DESK_SAVE_HOLD_MS;
        } else if (dr == DESK_DR_PRESET1_GOTO || dr == DESK_DR_PRESET4_GOTO) {
            hold_ms = CONFIG_DESK_GOTO_HOLD_MS;
        } else {
            /* 升/降不应走 pulse；交给 set + 运动超时 */
            return desk_dr_set(dr);
        }
    }

    cancel_hold_timer();
    atomic_store(&s_dr, dr);
    ESP_ERROR_CHECK(esp_timer_start_once(s_hold_timer, (uint64_t)hold_ms * 1000ULL));
    ESP_LOGI(TAG, "pulse DR=0x%02X (%s) for %u ms", dr, desk_dr_name(dr), (unsigned)hold_ms);
    return true;
}

const char *desk_dr_name(uint8_t dr)
{
    switch (dr) {
    case DESK_DR_IDLE:         return "idle/stop";
    case DESK_DR_UP:           return "up";
    case DESK_DR_DOWN:         return "down";
    case DESK_DR_PRESET1_GOTO: return "preset1-goto";
    case DESK_DR_PRESET1_SAVE: return "preset1-save";
    case DESK_DR_PRESET4_GOTO: return "preset4-goto";
    case DESK_DR_PRESET4_SAVE: return "preset4-save";
    default:                   return "unknown";
    }
}
