/**
 * @file desk_core.c
 * @brief 运动超时、童锁、可选 SIM 高度
 *
 * 超时放在 core：驱动只负责协议字节，避免各入口漏 stop。
 * 童锁 Phase1 仅持久化状态；真屏蔽面板在 Phase2 MITM。
 * SIM 高度：驱动无 digit 时本地推算，仅演示，禁止当真实高度。
 */
#include "desk_core.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "desk_core";
static const char *NVS_NS = "desk_core";
static const char *NVS_KEY_LOCK = "child_lock";

static esp_timer_handle_t s_hold_timer;
static bool s_child_lock;

#if CONFIG_DESK_SIM_HEIGHT
static int s_sim_mm;
static int64_t s_sim_last_us;
static int s_sim_preset1_mm;
static int s_sim_preset4_mm;

static void sim_init(void)
{
    s_sim_mm = (CONFIG_DESK_SIM_HEIGHT_MM_MIN + CONFIG_DESK_SIM_HEIGHT_MM_MAX) / 2;
    s_sim_preset1_mm = CONFIG_DESK_SIM_HEIGHT_MM_MIN + 50;
    s_sim_preset4_mm = CONFIG_DESK_SIM_HEIGHT_MM_MAX - 50;
    s_sim_last_us = esp_timer_get_time();
}

static int sim_clamp(int mm)
{
    if (mm < CONFIG_DESK_SIM_HEIGHT_MM_MIN) {
        return CONFIG_DESK_SIM_HEIGHT_MM_MIN;
    }
    if (mm > CONFIG_DESK_SIM_HEIGHT_MM_MAX) {
        return CONFIG_DESK_SIM_HEIGHT_MM_MAX;
    }
    return mm;
}

/** 按当前运动状态推进模拟高度（轮询 snapshot 时调用） */
static void sim_advance(desk_status_t st)
{
    int64_t now = esp_timer_get_time();
    int dt_ms = (int)((now - s_sim_last_us) / 1000);
    if (dt_ms < 0) {
        dt_ms = 0;
    }
    if (dt_ms > 500) {
        dt_ms = 500; /* 避免长时间未轮询一次跳太多 */
    }
    s_sim_last_us = now;
    int delta = (CONFIG_DESK_SIM_HEIGHT_SPEED_MM_S * dt_ms) / 1000;
    if (delta <= 0 && (st == DESK_STATUS_MOVING_UP || st == DESK_STATUS_MOVING_DOWN)) {
        /* 低速时至少保证偶发 +1，避免 UI 完全不动 */
        static int acc;
        acc += dt_ms;
        if (acc >= 40) {
            delta = 1;
            acc = 0;
        }
    }
    if (st == DESK_STATUS_MOVING_UP) {
        s_sim_mm = sim_clamp(s_sim_mm + delta);
    } else if (st == DESK_STATUS_MOVING_DOWN) {
        s_sim_mm = sim_clamp(s_sim_mm - delta);
    }
}
#endif /* CONFIG_DESK_SIM_HEIGHT */

static void cancel_hold_timer(void)
{
    if (s_hold_timer && esp_timer_is_active(s_hold_timer)) {
        esp_timer_stop(s_hold_timer);
    }
}

static void hold_timer_cb(void *arg)
{
    (void)arg;
    const desk_driver_t *drv = desk_driver_get_active();
    if (drv && drv->stop) {
        (void)drv->stop();
    }
    ESP_LOGW(TAG, "hold timeout -> stop");
}

static void arm_hold_ms(uint32_t ms)
{
    cancel_hold_timer();
    if (ms > 0) {
        ESP_ERROR_CHECK(esp_timer_start_once(s_hold_timer, (uint64_t)ms * 1000ULL));
    }
}

static esp_err_t load_child_lock(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_child_lock = false;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    uint8_t v = 0;
    err = nvs_get_u8(h, NVS_KEY_LOCK, &v);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_child_lock = false;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    s_child_lock = (v != 0);
    return ESP_OK;
}

static esp_err_t save_child_lock(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(h, NVS_KEY_LOCK, s_child_lock ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

esp_err_t desk_core_init(const desk_driver_t *drv)
{
    const esp_timer_create_args_t args = {
        .callback = &hold_timer_cb,
        .name = "desk_hold",
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &s_hold_timer));
    (void)load_child_lock();
#if CONFIG_DESK_SIM_HEIGHT
    sim_init();
    ESP_LOGW(TAG, "SIM height ON (demo only; not real desk digit)");
#endif

    esp_err_t err = desk_driver_register(drv);
    if (err != ESP_OK) {
        return err;
    }
    (void)desk_core_stop();
    ESP_LOGI(TAG, "init ok; child_lock=%d; motion_timeout=%d ms",
             (int)s_child_lock, CONFIG_DESK_MOTION_TIMEOUT_MS);
    return ESP_OK;
}

esp_err_t desk_core_stop(void)
{
    cancel_hold_timer();
    const desk_driver_t *drv = desk_driver_get_active();
    if (!drv || !drv->stop) {
        return ESP_ERR_INVALID_STATE;
    }
    return drv->stop();
}

esp_err_t desk_core_hold_up(void)
{
    const desk_driver_t *drv = desk_driver_get_active();
    if (!drv || !drv->hold_up) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    esp_err_t err = drv->hold_up();
    if (err == ESP_OK) {
        arm_hold_ms(CONFIG_DESK_MOTION_TIMEOUT_MS);
#if CONFIG_DESK_SIM_HEIGHT
        s_sim_last_us = esp_timer_get_time();
#endif
    }
    return err;
}

esp_err_t desk_core_hold_down(void)
{
    const desk_driver_t *drv = desk_driver_get_active();
    if (!drv || !drv->hold_down) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    esp_err_t err = drv->hold_down();
    if (err == ESP_OK) {
        arm_hold_ms(CONFIG_DESK_MOTION_TIMEOUT_MS);
#if CONFIG_DESK_SIM_HEIGHT
        s_sim_last_us = esp_timer_get_time();
#endif
    }
    return err;
}

esp_err_t desk_core_goto_preset(uint8_t n)
{
    const desk_driver_t *drv = desk_driver_get_active();
    if (!drv || !drv->goto_preset) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    esp_err_t err = drv->goto_preset(n);
    if (err == ESP_OK) {
        arm_hold_ms(CONFIG_DESK_GOTO_HOLD_MS);
#if CONFIG_DESK_SIM_HEIGHT
        if (n == 1) {
            s_sim_mm = s_sim_preset1_mm;
        } else if (n == 4) {
            s_sim_mm = s_sim_preset4_mm;
        }
#endif
    }
    return err;
}

esp_err_t desk_core_save_preset(uint8_t n)
{
    const desk_driver_t *drv = desk_driver_get_active();
    if (!drv || !drv->save_preset) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    esp_err_t err = drv->save_preset(n);
    if (err == ESP_OK) {
        arm_hold_ms(CONFIG_DESK_SAVE_HOLD_MS);
#if CONFIG_DESK_SIM_HEIGHT
        if (n == 1) {
            s_sim_preset1_mm = s_sim_mm;
        } else if (n == 4) {
            s_sim_preset4_mm = s_sim_mm;
        }
#endif
    }
    return err;
}

esp_err_t desk_core_set_child_lock(bool enabled)
{
    s_child_lock = enabled;
    esp_err_t err = save_child_lock();
    ESP_LOGI(TAG, "child_lock -> %d", (int)enabled);
    return err;
}

bool desk_core_get_child_lock(void)
{
    return s_child_lock;
}

desk_core_snapshot_t desk_core_snapshot(void)
{
    desk_core_snapshot_t s = {
        .status = DESK_STATUS_IDLE,
        .height_mm = -1,
        .height_known = false,
        .height_sim = false,
        .child_lock = s_child_lock,
        .driver = "none",
    };
    const desk_driver_t *drv = desk_driver_get_active();
    if (!drv) {
        return s;
    }
    s.driver = drv->name;
    if (drv->get_status) {
        s.status = drv->get_status();
    }
    if (drv->get_height_mm) {
        int mm = 0;
        if (drv->get_height_mm(&mm) == ESP_OK) {
            s.height_mm = mm;
            s.height_known = true;
            s.height_sim = false;
            return s;
        }
    }
#if CONFIG_DESK_SIM_HEIGHT
    sim_advance(s.status);
    s.height_mm = s_sim_mm;
    s.height_known = true;
    s.height_sim = true;
#endif
    return s;
}
