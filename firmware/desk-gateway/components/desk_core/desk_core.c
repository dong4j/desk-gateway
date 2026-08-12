/**
 * @file desk_core.c
 * @brief 方向相关运动保护、统一入口权限、童锁和可选 SIM 高度
 *
 * 下降超时放在 core；上升依靠松手/显式 STOP 与最高安全高度停止。
 * 驱动只负责协议字节，避免各入口绕过统一保护。
 * 童锁优先于所有入口开关；所有运动命令必须携带来源并在此授权。
 * SIM 高度：驱动无 digit 时本地推算，仅演示，禁止当真实高度。
 */
#include "desk_core.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "desk_core";
static const char *NVS_NS = "desk_core";
static const char *NVS_KEY_LOCK = "child_lock";
static const char *NVS_KEY_MAX_HEIGHT = "max_height_mm";
static const char *NVS_KEY_SOURCES = "ctrl_sources";
static const char *NVS_KEY_PRESET1_HEIGHT = "preset1_mm";
static const char *NVS_KEY_PRESET4_HEIGHT = "preset4_mm";

#define DESK_PRESET1_HEIGHT_MM_DEFAULT 640
#define DESK_PRESET4_HEIGHT_MM_DEFAULT 1020

/*
 * 上升不使用通用运动超时：手动上升由松手/显式 STOP 结束，档位 4
 * 由目标高度结束；两者始终受驱动层最高安全高度保护。传入 0 会先
 * 取消可能遗留的 core 定时器，但不会启动新的定时器。
 */
#define DESK_UPWARD_MOTION_TIMEOUT_MS 0U

/*
 * 当前 YourDesk 控制盒实测：DOWN 保持 130 ms 后立即 STOP，不产生可见位移，
 * 但会让控制盒发送当前高度显示帧。该时长是本机硬件经验值，不是协议常量。
 */
#define DESK_STARTUP_HEIGHT_PROBE_MS 130U

/*
 * 单次旋转只进入待命；700 ms 窗口内第二个同方向事件才启动。
 * 每个物理刻度刷新 500 ms 运动租约，减少停转后的额外行程；
 * 最后一个刻度后不再收到 REST 请求时自动停止。
 */
#define DESK_JOG_START_WINDOW_MS 700U
#define DESK_JOG_LEASE_MS 500U

typedef enum {
    DESK_JOG_NONE = 0,
    DESK_JOG_UP,
    DESK_JOG_DOWN,
} desk_jog_direction_t;

static esp_timer_handle_t s_hold_timer;
static desk_control_policy_t s_control_policy = {
    .child_lock = false,
    .enabled_sources = DESK_CONTROL_SOURCE_DEFAULT_MASK,
};
static int s_max_height_mm = CONFIG_DESK_MAX_HEIGHT_MM;
static int s_preset1_height_mm = DESK_PRESET1_HEIGHT_MM_DEFAULT;
static int s_preset4_height_mm = DESK_PRESET4_HEIGHT_MM_DEFAULT;
static desk_jog_direction_t s_jog_pending_direction;
static uint32_t s_jog_last_event_ms;

#if CONFIG_DESK_MOTION_DIAGNOSTICS
/** Keep UP and DOWN diagnostics structurally identical for direct comparison. */
static const char *motion_status_name(desk_status_t status)
{
    switch (status) {
    case DESK_STATUS_IDLE:
        return "idle";
    case DESK_STATUS_MOVING_UP:
        return "moving_up";
    case DESK_STATUS_MOVING_DOWN:
        return "moving_down";
    case DESK_STATUS_GOTO_PRESET:
        return "goto_preset";
    case DESK_STATUS_ERROR:
        return "error";
    default:
        return "unknown";
    }
}

/** Log a manual hold without calling any extra driver getter with side effects. */
static void log_hold_request(const desk_driver_t *drv,
                             desk_control_source_t source, bool upward,
                             uint32_t lease_ms)
{
    desk_status_t status = drv && drv->get_status ? drv->get_status()
                                                   : DESK_STATUS_ERROR;
    desk_status_t expected = upward ? DESK_STATUS_MOVING_UP
                                    : DESK_STATUS_MOVING_DOWN;
    ESP_LOGI(TAG,
             "motion request source=%s mode=hold dir=%s action=%s status=%s lease=%lu ms",
             desk_control_source_name(source), upward ? "up" : "down",
             status == expected ? "renew" : "start",
             motion_status_name(status), (unsigned long)lease_ms);
}

static void log_hold_result(desk_control_source_t source, bool upward,
                            esp_err_t err)
{
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "motion request source=%s mode=hold dir=%s rejected=%s",
                 desk_control_source_name(source), upward ? "up" : "down",
                 esp_err_to_name(err));
    }
}
#endif

#if CONFIG_DESK_SIM_HEIGHT
static int s_sim_mm;
static int64_t s_sim_last_us;

static void sim_init(void)
{
    s_sim_mm = (CONFIG_DESK_SIM_HEIGHT_MM_MIN + CONFIG_DESK_SIM_HEIGHT_MM_MAX) / 2;
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
    bool was_jog = s_jog_pending_direction != DESK_JOG_NONE;
    s_jog_pending_direction = DESK_JOG_NONE;
    const desk_driver_t *drv = desk_driver_get_active();
    if (drv && drv->stop) {
        (void)drv->stop();
    }
    ESP_LOGW(TAG, "motion stop source=core reason=%s",
             was_jog ? "jog_event_gap" : "hold_timeout");
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
        s_control_policy.child_lock = false;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    uint8_t v = 0;
    err = nvs_get_u8(h, NVS_KEY_LOCK, &v);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_control_policy.child_lock = false;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    s_control_policy.child_lock = (v != 0);
    return ESP_OK;
}

static esp_err_t save_child_lock(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(h, NVS_KEY_LOCK,
                     s_control_policy.child_lock ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

/** Load per-source switches; unknown future bits are ignored until supported. */
static esp_err_t load_control_sources(void)
{
    s_control_policy.enabled_sources = DESK_CONTROL_SOURCE_DEFAULT_MASK;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    uint32_t value = 0;
    err = nvs_get_u32(h, NVS_KEY_SOURCES, &value);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    const uint32_t known_mask =
        (DESK_CONTROL_SOURCE_BIT(DESK_CONTROL_SOURCE_COUNT) - UINT32_C(1));
    s_control_policy.enabled_sources = value & known_mask;
    /* Console is a local diagnostic path, but still remains below child lock. */
    s_control_policy.enabled_sources |=
        DESK_CONTROL_SOURCE_BIT(DESK_CONTROL_SOURCE_CONSOLE);
    return ESP_OK;
}

/** Persist all known source bits as one extensible NVS mask. */
static esp_err_t save_control_sources(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u32(h, NVS_KEY_SOURCES,
                      s_control_policy.enabled_sources);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

/** Load the persisted ceiling, falling back to the compile-time safe default. */
static esp_err_t load_max_height(void)
{
    s_max_height_mm = CONFIG_DESK_MAX_HEIGHT_MM;
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    int32_t value = 0;
    err = nvs_get_i32(h, NVS_KEY_MAX_HEIGHT, &value);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    if (value < DESK_MAX_HEIGHT_MM_MIN || value > DESK_MAX_HEIGHT_MM_MAX) {
        ESP_LOGW(TAG, "ignore invalid stored max height: %ld mm", (long)value);
        return ESP_OK;
    }
    s_max_height_mm = (int)value;
    return ESP_OK;
}

/** Persist only a range-checked ceiling; runtime enforcement is configured first. */
static esp_err_t save_max_height(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_i32(h, NVS_KEY_MAX_HEIGHT, (int32_t)s_max_height_mm);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

static bool preset_heights_valid(int preset1_height_mm,
                                 int preset4_height_mm,
                                 int max_height_mm)
{
    return preset1_height_mm >= DESK_MAX_HEIGHT_MM_MIN &&
           preset1_height_mm < preset4_height_mm &&
           preset4_height_mm <= max_height_mm &&
           preset4_height_mm <= DESK_MAX_HEIGHT_MM_MAX;
}

/** Load both gateway-owned preset targets as one validated configuration. */
static esp_err_t load_preset_heights(void)
{
    s_preset1_height_mm = DESK_PRESET1_HEIGHT_MM_DEFAULT;
    s_preset4_height_mm = DESK_PRESET4_HEIGHT_MM_DEFAULT;
    if (s_preset4_height_mm > s_max_height_mm) {
        s_preset4_height_mm = s_max_height_mm;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    int32_t preset1 = 0;
    int32_t preset4 = 0;
    esp_err_t preset1_err = nvs_get_i32(h, NVS_KEY_PRESET1_HEIGHT, &preset1);
    esp_err_t preset4_err = nvs_get_i32(h, NVS_KEY_PRESET4_HEIGHT, &preset4);
    nvs_close(h);
    if (preset1_err == ESP_ERR_NVS_NOT_FOUND ||
        preset4_err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (preset1_err != ESP_OK) {
        return preset1_err;
    }
    if (preset4_err != ESP_OK) {
        return preset4_err;
    }
    if (!preset_heights_valid((int)preset1, (int)preset4,
                              s_max_height_mm)) {
        ESP_LOGW(TAG, "ignore invalid stored presets: p1=%ld p4=%ld max=%d",
                 (long)preset1, (long)preset4, s_max_height_mm);
        return ESP_OK;
    }
    s_preset1_height_mm = (int)preset1;
    s_preset4_height_mm = (int)preset4;
    return ESP_OK;
}

/** Persist both targets in one NVS commit so power loss cannot split the pair. */
static esp_err_t save_preset_heights(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_i32(h, NVS_KEY_PRESET1_HEIGHT,
                      (int32_t)s_preset1_height_mm);
    if (err == ESP_OK) {
        err = nvs_set_i32(h, NVS_KEY_PRESET4_HEIGHT,
                          (int32_t)s_preset4_height_mm);
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}

/**
 * 启动时用一次极短 DOWN 脉冲唤醒控制盒高度显示帧。
 *
 * 该探测发生在 BLE/Wi-Fi/Web 启动之前，不会打断外部控制；童锁仍具有最高
 * 优先级。仅在驱动明确表示高度尚未知时执行一次，避免重复探测造成位移。
 */
static void probe_startup_height_if_unknown(const desk_driver_t *drv)
{
    if (!drv || !drv->get_height_mm) {
        return;
    }

    int height_mm = 0;
    esp_err_t height_err = drv->get_height_mm(&height_mm);
    if (height_err == ESP_OK) {
        ESP_LOGI(TAG, "startup height already known: %d mm", height_mm);
        return;
    }
    if (height_err == ESP_ERR_NOT_SUPPORTED) {
        return;
    }
    if (height_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "startup height check failed: %s",
                 esp_err_to_name(height_err));
        return;
    }
    if (s_control_policy.child_lock) {
        ESP_LOGI(TAG, "startup height probe skipped: child lock enabled");
        return;
    }

    ESP_LOGI(TAG, "startup height unknown; probing DOWN for %u ms",
             (unsigned)DESK_STARTUP_HEIGHT_PROBE_MS);
    esp_err_t err = desk_core_hold_down(DESK_CONTROL_SOURCE_CONSOLE);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "startup height probe rejected: %s",
                 esp_err_to_name(err));
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(DESK_STARTUP_HEIGHT_PROBE_MS));
    err = desk_core_stop();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "startup height probe stop failed: %s",
                 esp_err_to_name(err));
    }
}

esp_err_t desk_core_init(const desk_driver_t *drv)
{
    const esp_timer_create_args_t args = {
        .callback = &hold_timer_cb,
        .name = "desk_hold",
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &s_hold_timer));
    (void)load_child_lock();
    (void)load_control_sources();
    (void)load_max_height();
    (void)load_preset_heights();
#if CONFIG_DESK_SIM_HEIGHT
    sim_init();
    ESP_LOGI(TAG, "SIM height fallback compiled; height-capable drivers bypass it");
#endif

    esp_err_t err = desk_driver_register(drv);
    if (err != ESP_OK) {
        return err;
    }
    if (drv->set_max_height_mm) {
        err = drv->set_max_height_mm(s_max_height_mm);
        if (err != ESP_OK) {
            return err;
        }
    }
    if (drv->set_preset_heights_mm) {
        err = drv->set_preset_heights_mm(s_preset1_height_mm,
                                          s_preset4_height_mm);
        if (err != ESP_OK) {
            return err;
        }
    }
    (void)desk_core_stop();
    if (drv->set_panel_enabled) {
        bool panel_enabled = desk_control_policy_allows(
            &s_control_policy, DESK_CONTROL_SOURCE_PANEL);
        err = drv->set_panel_enabled(panel_enabled);
        if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED) {
            return err;
        }
    }
    probe_startup_height_if_unknown(drv);
    ESP_LOGI(TAG,
             "init ok; child_lock=%d; sources=0x%08lx; max_height=%d mm; "
             "preset1=%d mm; preset4=%d mm; motion_timeout=%d ms",
             (int)s_control_policy.child_lock,
             (unsigned long)s_control_policy.enabled_sources,
             s_max_height_mm, s_preset1_height_mm, s_preset4_height_mm,
             CONFIG_DESK_MOTION_TIMEOUT_MS);
    return ESP_OK;
}

esp_err_t desk_core_stop(void)
{
    cancel_hold_timer();
    s_jog_pending_direction = DESK_JOG_NONE;
    const desk_driver_t *drv = desk_driver_get_active();
    if (!drv || !drv->stop) {
        return ESP_ERR_INVALID_STATE;
    }
#if CONFIG_DESK_MOTION_DIAGNOSTICS
    desk_status_t status = drv->get_status ? drv->get_status()
                                           : DESK_STATUS_ERROR;
    ESP_LOGI(TAG, "motion stop source=core reason=explicit status=%s",
             motion_status_name(status));
#endif
    return drv->stop();
}

/** Reject every motion source below child lock and its own persisted switch. */
static esp_err_t authorize_source(desk_control_source_t source)
{
    if (!desk_control_source_is_valid(source)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!desk_control_policy_allows(&s_control_policy, source)) {
        ESP_LOGW(TAG, "reject source=%s child_lock=%d enabled=%d",
                 desk_control_source_name(source),
                 (int)s_control_policy.child_lock,
                 (int)desk_control_policy_source_enabled(&s_control_policy,
                                                         source));
        return ESP_ERR_NOT_ALLOWED;
    }
    return ESP_OK;
}

/**
 * Start or renew one manual direction through the same motion path.
 *
 * UP adds only the configured ceiling pre-check. Once a direction is already
 * active, both UP and DOWN merely renew the lease and leave height decoding
 * untouched.
 */
static esp_err_t hold_direction_for_ms(bool upward, uint32_t timeout_ms)
{
    const desk_driver_t *drv = desk_driver_get_active();
    esp_err_t (*hold)(void) =
        drv ? (upward ? drv->hold_up : drv->hold_down) : NULL;
    if (!drv || !hold) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (upward) {
        if (!drv->get_height_mm) {
            return ESP_ERR_NOT_SUPPORTED;
        }
        int current_mm = -1;
        esp_err_t height_err = drv->get_height_mm(&current_mm);
        if (height_err != ESP_OK && height_err != ESP_ERR_INVALID_STATE) {
            if (drv->stop) {
                (void)drv->stop();
            }
            return height_err;
        }
        if (height_err == ESP_OK &&
            current_mm >=
                s_max_height_mm - DESK_MAX_HEIGHT_STOP_MARGIN_MM) {
            if (drv->stop) {
                (void)drv->stop();
            }
            return ESP_ERR_INVALID_STATE;
        }
    }

    desk_status_t moving_status =
        upward ? DESK_STATUS_MOVING_UP : DESK_STATUS_MOVING_DOWN;
    if (drv->get_status && drv->get_status() == moving_status) {
        /* 续租不能重新发送方向命令，也不能重置高度同步状态。 */
        arm_hold_ms(timeout_ms);
        return ESP_OK;
    }

    /*
     * An unknown height is expected after boot because the controller may not
     * refresh its display while idle. Motion starts normally; the independent
     * upward observer takes over as soon as the first real frame arrives.
     */
    esp_err_t err = hold();
    if (err == ESP_OK) {
        arm_hold_ms(timeout_ms);
#if CONFIG_DESK_SIM_HEIGHT
        s_sim_last_us = esp_timer_get_time();
#endif
    }
    return err;
}

static esp_err_t hold_up_for_ms(uint32_t timeout_ms)
{
    return hold_direction_for_ms(true, timeout_ms);
}

static esp_err_t hold_down_for_ms(uint32_t timeout_ms)
{
    return hold_direction_for_ms(false, timeout_ms);
}

esp_err_t desk_core_hold_up(desk_control_source_t source)
{
    esp_err_t err = authorize_source(source);
    if (err != ESP_OK) {
        return err;
    }
    s_jog_pending_direction = DESK_JOG_NONE;
#if CONFIG_DESK_MOTION_DIAGNOSTICS
    const desk_driver_t *drv = desk_driver_get_active();
    log_hold_request(drv, source, true, DESK_UPWARD_MOTION_TIMEOUT_MS);
#endif
    err = hold_up_for_ms(DESK_UPWARD_MOTION_TIMEOUT_MS);
#if CONFIG_DESK_MOTION_DIAGNOSTICS
    log_hold_result(source, true, err);
#endif
    return err;
}

esp_err_t desk_core_hold_down(desk_control_source_t source)
{
    esp_err_t err = authorize_source(source);
    if (err != ESP_OK) {
        return err;
    }
    s_jog_pending_direction = DESK_JOG_NONE;
#if CONFIG_DESK_MOTION_DIAGNOSTICS
    const desk_driver_t *drv = desk_driver_get_active();
    log_hold_request(drv, source, false, CONFIG_DESK_MOTION_TIMEOUT_MS);
#endif
    err = hold_down_for_ms(CONFIG_DESK_MOTION_TIMEOUT_MS);
#if CONFIG_DESK_MOTION_DIAGNOSTICS
    log_hold_result(source, false, err);
#endif
    return err;
}

/**
 * 将离散旋钮事件转换成类似长按的运动：首次事件只待命，连续事件启动并续租。
 */
static esp_err_t jog_event(desk_jog_direction_t direction,
                           desk_control_source_t source)
{
#if !CONFIG_DESK_MOTION_DIAGNOSTICS
    (void)source;
#endif
    const desk_driver_t *drv = desk_driver_get_active();
    if (!drv || !drv->get_status) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    desk_status_t status = drv->get_status();
    desk_status_t expected_status = direction == DESK_JOG_UP
                                        ? DESK_STATUS_MOVING_UP
                                        : DESK_STATUS_MOVING_DOWN;

    if (status == expected_status) {
#if CONFIG_DESK_MOTION_DIAGNOSTICS
        ESP_LOGI(TAG,
                 "motion request source=%s mode=jog dir=%s action=renew status=%s lease=%u ms",
                 desk_control_source_name(source),
                 direction == DESK_JOG_UP ? "up" : "down",
                 motion_status_name(status), DESK_JOG_LEASE_MS);
#endif
        s_jog_pending_direction = direction;
        s_jog_last_event_ms = now_ms;
        return direction == DESK_JOG_UP
                   ? hold_up_for_ms(DESK_JOG_LEASE_MS)
                   : hold_down_for_ms(DESK_JOG_LEASE_MS);
    }

    if (status == DESK_STATUS_MOVING_UP || status == DESK_STATUS_MOVING_DOWN ||
        status == DESK_STATUS_GOTO_PRESET) {
        /* 反向旋转先立即停下；新方向仍需第二个事件才能启动。 */
        esp_err_t stop_err = desk_core_stop();
        if (stop_err != ESP_OK) {
            return stop_err;
        }
    }

    uint32_t elapsed_ms = now_ms - s_jog_last_event_ms;
    bool should_start = s_jog_pending_direction == direction &&
                        elapsed_ms <= DESK_JOG_START_WINDOW_MS;
    s_jog_pending_direction = direction;
    s_jog_last_event_ms = now_ms;

    if (!should_start) {
#if CONFIG_DESK_MOTION_DIAGNOSTICS
        ESP_LOGI(TAG,
                 "motion request source=%s mode=jog dir=%s action=armed status=%s window=%u ms",
                 desk_control_source_name(source),
                 direction == DESK_JOG_UP ? "up" : "down",
                 motion_status_name(status), DESK_JOG_START_WINDOW_MS);
#endif
        return ESP_OK;
    }
#if CONFIG_DESK_MOTION_DIAGNOSTICS
    ESP_LOGI(TAG,
             "motion request source=%s mode=jog dir=%s action=start status=%s lease=%u ms",
             desk_control_source_name(source),
             direction == DESK_JOG_UP ? "up" : "down",
             motion_status_name(status), DESK_JOG_LEASE_MS);
#endif
    return direction == DESK_JOG_UP
               ? hold_up_for_ms(DESK_JOG_LEASE_MS)
               : hold_down_for_ms(DESK_JOG_LEASE_MS);
}

esp_err_t desk_core_jog_up(desk_control_source_t source)
{
    esp_err_t err = authorize_source(source);
    if (err != ESP_OK) {
        return err;
    }
    return jog_event(DESK_JOG_UP, source);
}

esp_err_t desk_core_jog_down(desk_control_source_t source)
{
    esp_err_t err = authorize_source(source);
    if (err != ESP_OK) {
        return err;
    }
    return jog_event(DESK_JOG_DOWN, source);
}

esp_err_t desk_core_goto_preset(desk_control_source_t source, uint8_t n)
{
    esp_err_t auth_err = authorize_source(source);
    if (auth_err != ESP_OK) {
        return auth_err;
    }
    s_jog_pending_direction = DESK_JOG_NONE;
    const desk_driver_t *drv = desk_driver_get_active();
    if (!drv || !drv->goto_preset) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    /*
     * 驱动执行档位期间统一返回 GOTO_PRESET，无法从 status 区分方向。
     * 因此在启动前用当前高度和最终目标判断本次是否真正向上；高度
     * 未知时保持原有超时，避免改变档位 1 的安全下降行为。
     */
    bool preset_moves_up = false;
    if (drv->get_height_mm && (n == 1 || n == 4)) {
        int current_mm = -1;
        if (drv->get_height_mm(&current_mm) == ESP_OK) {
            int target_mm =
                n == 1 ? s_preset1_height_mm : s_preset4_height_mm;
            if (target_mm > s_max_height_mm) {
                target_mm = s_max_height_mm;
            }
            preset_moves_up = current_mm < target_mm;
        }
    }

    esp_err_t err = drv->goto_preset(n);
    if (err == ESP_OK) {
        /*
         * 只按实际运动方向决定是否启用超时：上升到目标或安全上限后
         * 由驱动停止；下降保持现有超时行为不变。
         */
        arm_hold_ms(preset_moves_up ? DESK_UPWARD_MOTION_TIMEOUT_MS
                                    : CONFIG_DESK_MOTION_TIMEOUT_MS);
#if CONFIG_DESK_SIM_HEIGHT
        if (n == 1) {
            s_sim_mm = s_preset1_height_mm;
        } else if (n == 4) {
            s_sim_mm = s_preset4_height_mm;
        }
#endif
    }
    return err;
}

esp_err_t desk_core_save_preset(desk_control_source_t source, uint8_t n)
{
    esp_err_t auth_err = authorize_source(source);
    if (auth_err != ESP_OK) {
        return auth_err;
    }
    const desk_driver_t *drv = desk_driver_get_active();
    if (!drv || !drv->save_preset) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    esp_err_t err = drv->save_preset(n);
    if (err == ESP_OK) {
        arm_hold_ms(CONFIG_DESK_SAVE_HOLD_MS);
#if CONFIG_DESK_SIM_HEIGHT
        if (n == 1) {
            (void)desk_core_set_preset_heights_mm(
                s_sim_mm, s_preset4_height_mm);
        } else if (n == 4) {
            (void)desk_core_set_preset_heights_mm(
                s_preset1_height_mm, s_sim_mm);
        }
#endif
    }
    return err;
}

esp_err_t desk_core_set_child_lock(bool enabled)
{
    if (enabled == s_control_policy.child_lock) {
        return ESP_OK;
    }

    const desk_driver_t *drv = desk_driver_get_active();
    if (enabled) {
        /* Set the runtime lock first so no concurrent source can start again. */
        s_control_policy.child_lock = true;
        esp_err_t stop_err = desk_core_stop();
        if (drv && drv->set_panel_enabled) {
            (void)drv->set_panel_enabled(false);
        }
        esp_err_t persist_err = save_child_lock();
        ESP_LOGI(TAG, "child_lock -> 1");
        return persist_err != ESP_OK ? persist_err : stop_err;
    }

    /* Do not reopen any source until the unlocked state is durable. */
    s_control_policy.child_lock = false;
    esp_err_t err = save_child_lock();
    if (err != ESP_OK) {
        s_control_policy.child_lock = true;
        return err;
    }
    if (drv && drv->set_panel_enabled) {
        bool panel_enabled = desk_control_policy_source_enabled(
            &s_control_policy, DESK_CONTROL_SOURCE_PANEL);
        err = drv->set_panel_enabled(panel_enabled);
        if (err == ESP_ERR_NOT_SUPPORTED) {
            err = ESP_OK;
        }
    }
    ESP_LOGI(TAG, "child_lock -> 0");
    return err;
}

bool desk_core_get_child_lock(void)
{
    return s_control_policy.child_lock;
}

esp_err_t desk_core_set_source_enabled(desk_control_source_t source,
                                       bool enabled)
{
    if (!desk_control_source_is_valid(source) ||
        (DESK_CONTROL_SOURCE_BIT(source) &
         DESK_CONTROL_SOURCE_CONFIGURABLE_MASK) == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    bool previous = desk_control_policy_source_enabled(&s_control_policy,
                                                       source);
    if (previous == enabled) {
        return ESP_OK;
    }

    uint32_t bit = DESK_CONTROL_SOURCE_BIT(source);
    if (enabled) {
        s_control_policy.enabled_sources |= bit;
    } else {
        s_control_policy.enabled_sources &= ~bit;
        /* Administrative revocation is fail-safe: cancel any in-flight motion. */
        (void)desk_core_stop();
    }

    const desk_driver_t *drv = desk_driver_get_active();
    if (source == DESK_CONTROL_SOURCE_PANEL && drv &&
        drv->set_panel_enabled) {
        bool effective = enabled && !s_control_policy.child_lock;
        (void)drv->set_panel_enabled(effective);
    }

    esp_err_t err = save_control_sources();
    if (err != ESP_OK) {
        if (previous) {
            s_control_policy.enabled_sources |= bit;
        } else {
            s_control_policy.enabled_sources &= ~bit;
        }
        if (source == DESK_CONTROL_SOURCE_PANEL && drv &&
            drv->set_panel_enabled) {
            bool effective = previous && !s_control_policy.child_lock;
            (void)drv->set_panel_enabled(effective);
        }
        return err;
    }
    ESP_LOGI(TAG, "source %s -> %d", desk_control_source_name(source),
             (int)enabled);
    return ESP_OK;
}

bool desk_core_get_source_enabled(desk_control_source_t source)
{
    return desk_control_policy_source_enabled(&s_control_policy, source);
}

esp_err_t desk_core_set_max_height_mm(int max_height_mm)
{
    if (max_height_mm < DESK_MAX_HEIGHT_MM_MIN ||
        max_height_mm > DESK_MAX_HEIGHT_MM_MAX ||
        max_height_mm < s_preset4_height_mm) {
        return ESP_ERR_INVALID_ARG;
    }
    const desk_driver_t *drv = desk_driver_get_active();
    if (!drv || !drv->set_max_height_mm) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    esp_err_t err = drv->set_max_height_mm(max_height_mm);
    if (err != ESP_OK) {
        return err;
    }
    s_max_height_mm = max_height_mm;
    err = save_max_height();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "max safe height=%d mm (stop margin=%d mm)",
                 s_max_height_mm, DESK_MAX_HEIGHT_STOP_MARGIN_MM);
    }
    return err;
}

int desk_core_get_max_height_mm(void)
{
    return s_max_height_mm;
}

esp_err_t desk_core_set_preset_heights_mm(int preset1_height_mm,
                                          int preset4_height_mm)
{
    if (!preset_heights_valid(preset1_height_mm, preset4_height_mm,
                              s_max_height_mm)) {
        return ESP_ERR_INVALID_ARG;
    }
    const desk_driver_t *drv = desk_driver_get_active();
    if (!drv || !drv->set_preset_heights_mm) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    int previous_preset1 = s_preset1_height_mm;
    int previous_preset4 = s_preset4_height_mm;
    esp_err_t err = drv->set_preset_heights_mm(preset1_height_mm,
                                                preset4_height_mm);
    if (err != ESP_OK) {
        return err;
    }
    s_preset1_height_mm = preset1_height_mm;
    s_preset4_height_mm = preset4_height_mm;
    err = save_preset_heights();
    if (err != ESP_OK) {
        s_preset1_height_mm = previous_preset1;
        s_preset4_height_mm = previous_preset4;
        (void)drv->set_preset_heights_mm(previous_preset1,
                                          previous_preset4);
        return err;
    }
    ESP_LOGI(TAG, "preset heights: p1=%d mm p4=%d mm",
             s_preset1_height_mm, s_preset4_height_mm);
    return ESP_OK;
}

desk_core_snapshot_t desk_core_snapshot(void)
{
    desk_core_snapshot_t s = {
        .status = DESK_STATUS_IDLE,
        .height_mm = -1,
        .height_known = false,
        .height_sim = false,
        .child_lock = s_control_policy.child_lock,
        .upward_blocked = false,
        .max_height_mm = s_max_height_mm,
        .preset1_height_mm = s_preset1_height_mm,
        .preset4_height_mm = s_preset4_height_mm,
        .enabled_sources = s_control_policy.enabled_sources,
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
    if (drv->is_upward_blocked) {
        s.upward_blocked = drv->is_upward_blocked();
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
    bool driver_has_height = drv->get_caps && drv->get_caps().height;
    /* A height-capable driver must report unknown rather than fabricate a fallback. */
    if (!driver_has_height) {
        sim_advance(s.status);
        s.height_mm = s_sim_mm;
        s.height_known = true;
        s.height_sim = true;
    }
#endif
    return s;
}
