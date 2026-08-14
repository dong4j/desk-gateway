/**
 * @file desk_web.c
 * @brief Bearer / X-Desk-Key 认证 + REST + 嵌入静态资源
 *
 * 状态由前端短轮询获取。ESP-IDF HTTP handler 在 server task 中执行，
 * 不在同步 handler 内维持长连接，避免状态流阻塞急停等控制请求。
 *
 * JSON 通过 Component Manager 依赖 espressif/cjson（IDF 6 已移出内置 json）。
 */
#include "desk_web.h"

#include "desk_ble.h"
#include "desk_audio.h"
#include "desk_core.h"
#include "desk_reminder.h"
#include "desk_tof.h"
#include "desk_web_ble_api.h"
#include "desk_wifi.h"

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "nvs.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "desk_web";
static const char *NVS_NS = "desk_web";
static const char *NVS_PASS = "password";

static httpd_handle_t s_server;
static char s_token[33];
static char s_password[64];
static bool s_restart_pending;

/* EMBED_FILES 用路径 www/xxx，但符号只取文件名：_binary_<name_with_underscores>_* */
extern const uint8_t www_login_html_start[] asm("_binary_login_html_start");
extern const uint8_t www_login_html_end[] asm("_binary_login_html_end");
extern const uint8_t www_index_html_start[] asm("_binary_index_html_start");
extern const uint8_t www_index_html_end[] asm("_binary_index_html_end");
extern const uint8_t www_hold_control_js_start[] asm("_binary_hold_control_js_start");
extern const uint8_t www_hold_control_js_end[] asm("_binary_hold_control_js_end");
extern const uint8_t www_bond_management_js_start[] asm("_binary_bond_management_js_start");
extern const uint8_t www_bond_management_js_end[] asm("_binary_bond_management_js_end");
extern const uint8_t www_height_presets_js_start[] asm("_binary_height_presets_js_start");
extern const uint8_t www_height_presets_js_end[] asm("_binary_height_presets_js_end");
extern const uint8_t www_reminder_control_js_start[] asm("_binary_reminder_control_js_start");
extern const uint8_t www_reminder_control_js_end[] asm("_binary_reminder_control_js_end");
extern const uint8_t www_app_js_start[] asm("_binary_app_js_start");
extern const uint8_t www_app_js_end[] asm("_binary_app_js_end");
extern const uint8_t www_style_css_start[] asm("_binary_style_css_start");
extern const uint8_t www_style_css_end[] asm("_binary_style_css_end");
extern const uint8_t www_favicon_png_start[] asm("_binary_favicon_png_start");
extern const uint8_t www_favicon_png_end[] asm("_binary_favicon_png_end");
extern const uint8_t www_desk_workstation_webp_start[] asm("_binary_desk_workstation_webp_start");
extern const uint8_t www_desk_workstation_webp_end[] asm("_binary_desk_workstation_webp_end");
extern const uint8_t www_setup_html_start[] asm("_binary_setup_html_start");
extern const uint8_t www_setup_html_end[] asm("_binary_setup_html_end");

static void load_password(void)
{
    nvs_handle_t h;
    size_t len = sizeof(s_password);
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_str(h, NVS_PASS, s_password, &len) != ESP_OK) {
            strncpy(s_password, CONFIG_DESK_WEB_DEFAULT_PASSWORD, sizeof(s_password) - 1);
        }
        nvs_close(h);
    } else {
        strncpy(s_password, CONFIG_DESK_WEB_DEFAULT_PASSWORD, sizeof(s_password) - 1);
    }
}

static esp_err_t save_password(const char *pass)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, NVS_PASS, pass);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err == ESP_OK) {
        strncpy(s_password, pass, sizeof(s_password) - 1);
        s_password[sizeof(s_password) - 1] = '\0';
    }
    return err;
}

static void mint_token(void)
{
    uint8_t raw[16];
    esp_fill_random(raw, sizeof(raw));
    for (int i = 0; i < 16; i++) {
        sprintf(&s_token[i * 2], "%02x", raw[i]);
    }
    s_token[32] = '\0';
}

static bool authed(httpd_req_t *req)
{
    /*
     * 本地自动化直接复用当前登录密码，避免每次档位切换先申请一个会刷新
     * Web 会话的 Bearer token。X-Desk-Key 与 Bearer 对全部已认证接口等价。
     */
    char desk_key[sizeof(s_password)] = {0};
    if (httpd_req_get_hdr_value_str(req, "X-Desk-Key", desk_key, sizeof(desk_key)) == ESP_OK &&
        strcmp(desk_key, s_password) == 0) {
        return true;
    }

    char hdr[96] = {0};
    if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) != ESP_OK) {
        return false;
    }
    const char *p = hdr;
    if (strncmp(p, "Bearer ", 7) == 0) {
        p += 7;
    }
    return s_token[0] && strcmp(p, s_token) == 0;
}

static esp_err_t send_cjson(httpd_req_t *req, int status, cJSON *obj)
{
    char *body = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!body) {
        return ESP_ERR_NO_MEM;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, status == 200 ? "200 OK" :
                                status == 202 ? "202 Accepted" :
                                status == 401 ? "401 Unauthorized" :
                                status == 403 ? "403 Forbidden" :
                                status == 404 ? "404 Not Found" :
                                status == 409 ? "409 Conflict" :
                                status == 500 ? "500 Internal Server Error" :
                                                "400 Bad Request");
    esp_err_t err = httpd_resp_sendstr(req, body);
    free(body);
    return err;
}

static esp_err_t send_unauthorized(httpd_req_t *req)
{
    cJSON *error = cJSON_CreateObject();
    cJSON_AddStringToObject(error, "error", "unauthorized");
    return send_cjson(req, 401, error);
}

static cJSON *ble_management_snapshot_json(
    const desk_ble_management_snapshot_t *snapshot)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *devices = cJSON_AddArrayToObject(root, "devices");
    for (size_t i = 0; i < snapshot->device_count; ++i) {
        const desk_ble_bond_view_t *view = &snapshot->devices[i];
        cJSON *device = cJSON_CreateObject();
        cJSON_AddStringToObject(device, "id", view->id);
        cJSON_AddStringToObject(device, "kind", view->kind);
        cJSON_AddStringToObject(device, "label", view->label);
        cJSON_AddStringToObject(device, "alias", view->alias);
        cJSON_AddBoolToObject(device, "connected", view->connected);
        cJSON_AddBoolToObject(device, "controlling", view->controlling);
        cJSON_AddStringToObject(
            device, "delete_state",
            desk_web_ble_delete_state_name(view->delete_state));
        if (view->delete_error[0]) {
            cJSON_AddStringToObject(device, "delete_error",
                                   view->delete_error);
        } else {
            cJSON_AddNullToObject(device, "delete_error");
        }
        cJSON_AddItemToArray(devices, device);
    }
    cJSON_AddNumberToObject(root, "capacity", snapshot->capacity);
    cJSON *pairing = cJSON_AddObjectToObject(root, "pairing_window");
    cJSON_AddBoolToObject(pairing, "open", snapshot->pairing_window_open);
    cJSON_AddNumberToObject(pairing, "remaining_seconds",
                            snapshot->pairing_window_remaining_seconds);
    return root;
}

static esp_err_t send_ble_management_result(
    httpd_req_t *req, desk_ble_management_result_t result)
{
    int status = desk_web_ble_result_status(result);
    cJSON *body = cJSON_CreateObject();
    cJSON_AddBoolToObject(body, "ok", status == 200 || status == 202);
    if (status == 404) {
        cJSON_AddStringToObject(body, "error", "bond_not_found");
    } else if (status == 409) {
        cJSON_AddStringToObject(body, "error", "delete_conflict");
    } else if (status == 400) {
        cJSON_AddStringToObject(body, "error", "invalid_alias");
    } else if (status == 500) {
        cJSON_AddStringToObject(body, "error", "internal_error");
    }
    return send_cjson(req, status, body);
}

static const char *status_str(desk_status_t st)
{
    switch (st) {
    case DESK_STATUS_MOVING_UP:
        return "moving_up";
    case DESK_STATUS_MOVING_DOWN:
        return "moving_down";
    case DESK_STATUS_GOTO_PRESET:
        return "goto_preset";
    case DESK_STATUS_ERROR:
        return "error";
    default:
        return "idle";
    }
}

static cJSON *snapshot_json(void)
{
    desk_core_snapshot_t s = desk_core_snapshot();
    desk_tof_snapshot_t tof = desk_tof_snapshot();
    const esp_app_desc_t *app = esp_app_get_description();
    char build_id[9] = {0};
    if (app) {
        /* 编译时间在增量构建时可能不变，短 ELF 哈希用于精确区分固件。 */
        snprintf(build_id, sizeof(build_id), "%02x%02x%02x%02x",
                 app->app_elf_sha256[0], app->app_elf_sha256[1],
                 app->app_elf_sha256[2], app->app_elf_sha256[3]);
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "status", status_str(s.status));
    if (s.height_known) {
        cJSON_AddNumberToObject(o, "height_mm", s.height_mm);
    } else {
        cJSON_AddNullToObject(o, "height_mm");
    }
    cJSON_AddBoolToObject(o, "height_known", s.height_known);
    cJSON_AddBoolToObject(o, "height_sim", s.height_sim);
    if (tof.height_known) {
        cJSON_AddNumberToObject(o, "tof_height_mm", tof.height_mm);
    } else {
        cJSON_AddNullToObject(o, "tof_height_mm");
    }
    cJSON_AddBoolToObject(o, "tof_height_known", tof.height_known);
    if (tof.right_gap_known) {
        cJSON_AddNumberToObject(o, "right_gap_mm", tof.right_gap_mm);
    } else {
        cJSON_AddNullToObject(o, "right_gap_mm");
    }
    cJSON_AddBoolToObject(o, "right_gap_known", tof.right_gap_known);
    cJSON_AddBoolToObject(o, "child_lock", s.child_lock);
    cJSON_AddBoolToObject(o, "upward_blocked", s.upward_blocked);
    cJSON_AddBoolToObject(o, "raise_to_max_supported",
                          s.raise_to_max_supported);
    cJSON_AddBoolToObject(o, "controller_reset_supported",
                          s.controller_reset_supported);
    cJSON_AddBoolToObject(o, "controller_reset_active",
                          s.controller_reset_active);
    cJSON_AddBoolToObject(o, "controller_reset_recommended",
                          s.controller_reset_recommended);
    cJSON_AddNumberToObject(o, "max_height_mm", s.max_height_mm);
    cJSON_AddNumberToObject(o, "preset1_height_mm", s.preset1_height_mm);
    cJSON_AddNumberToObject(o, "preset4_height_mm", s.preset4_height_mm);
    cJSON *sources = cJSON_AddObjectToObject(o, "control_sources");
    cJSON_AddBoolToObject(
        sources, "rest",
        (s.enabled_sources &
         DESK_CONTROL_SOURCE_BIT(DESK_CONTROL_SOURCE_REST)) != 0);
    cJSON_AddBoolToObject(
        sources, "bluetooth",
        (s.enabled_sources &
         DESK_CONTROL_SOURCE_BIT(DESK_CONTROL_SOURCE_BLUETOOTH)) != 0);
    cJSON_AddBoolToObject(
        sources, "panel",
        (s.enabled_sources &
         DESK_CONTROL_SOURCE_BIT(DESK_CONTROL_SOURCE_PANEL)) != 0);
    cJSON_AddStringToObject(o, "driver", s.driver ? s.driver : "none");
    /* 读取当前运行镜像的元数据，页面显示值可以直接用于确认烧录结果。 */
    cJSON_AddStringToObject(o, "build_date", app ? app->date : "");
    cJSON_AddStringToObject(o, "build_time", app ? app->time : "");
    cJSON_AddStringToObject(o, "build_id", build_id);
    /* ESP-IDF 从 Git 生成 app version，用于确认当前烧录对应的提交。 */
    cJSON_AddStringToObject(o, "git_version", app ? app->version : "");
    cJSON_AddNumberToObject(o, "ts_ms", (double)esp_log_timestamp());

    desk_reminder_snapshot_t reminder = desk_reminder_snapshot();
    cJSON *reminder_json = cJSON_AddObjectToObject(o, "reminder");
    cJSON_AddBoolToObject(reminder_json, "available", reminder.available);
    cJSON_AddStringToObject(reminder_json, "state",
                            desk_reminder_state_name(reminder.state));
    cJSON_AddStringToObject(reminder_json, "phase",
                            desk_reminder_phase_name(reminder.phase));
    cJSON_AddStringToObject(reminder_json, "alarm_reason",
                            desk_reminder_alarm_name(reminder.alarm_reason));
    cJSON_AddNumberToObject(reminder_json, "remaining_sec",
                            reminder.remaining_sec);
    cJSON_AddNumberToObject(reminder_json, "completed_focus_count",
                            reminder.completed_focus_count);
    cJSON_AddNumberToObject(reminder_json, "focus_minutes",
                            reminder.config.focus_minutes);
    cJSON_AddNumberToObject(reminder_json, "short_break_minutes",
                            reminder.config.short_break_minutes);
    cJSON_AddNumberToObject(reminder_json, "long_break_minutes",
                            reminder.config.long_break_minutes);
    cJSON_AddNumberToObject(reminder_json, "focuses_per_long_break",
                            reminder.config.focuses_per_long_break);
    if (reminder.last_error) {
        cJSON_AddStringToObject(reminder_json, "last_error", reminder.last_error);
    } else {
        cJSON_AddNullToObject(reminder_json, "last_error");
    }

    desk_audio_snapshot_t audio = desk_audio_snapshot();
    cJSON *audio_json = cJSON_AddObjectToObject(o, "audio");
    cJSON_AddBoolToObject(audio_json, "available", audio.available);
    cJSON_AddBoolToObject(audio_json, "enabled", audio.enabled);
    cJSON_AddBoolToObject(audio_json, "playing", audio.playing);
    cJSON_AddNumberToObject(audio_json, "volume_percent",
                            audio.volume_percent);
    cJSON_AddStringToObject(audio_json, "voice_pack", audio.voice_pack);
    if (audio.current_prompt) {
        cJSON_AddStringToObject(audio_json, "current_prompt",
                                audio.current_prompt);
    } else {
        cJSON_AddNullToObject(audio_json, "current_prompt");
    }
    if (audio.last_error) {
        cJSON_AddStringToObject(audio_json, "last_error", audio.last_error);
    } else {
        cJSON_AddNullToObject(audio_json, "last_error");
    }
    return o;
}

static esp_err_t read_body(httpd_req_t *req, char *buf, size_t len)
{
    int total = req->content_len;
    if (total <= 0 || (size_t)total >= len) {
        return ESP_ERR_INVALID_SIZE;
    }
    int r = httpd_req_recv(req, buf, total);
    if (r <= 0) {
        return ESP_FAIL;
    }
    buf[r] = '\0';
    return ESP_OK;
}

/** Parse only the user-configurable ingress names exposed by the REST API. */
static bool parse_control_source(const char *name,
                                 desk_control_source_t *out_source)
{
    if (!name || !out_source) {
        return false;
    }
    if (strcmp(name, "rest") == 0) {
        *out_source = DESK_CONTROL_SOURCE_REST;
        return true;
    }
    if (strcmp(name, "bluetooth") == 0) {
        *out_source = DESK_CONTROL_SOURCE_BLUETOOTH;
        return true;
    }
    if (strcmp(name, "panel") == 0) {
        *out_source = DESK_CONTROL_SOURCE_PANEL;
        return true;
    }
    return false;
}

static esp_err_t handler_login(httpd_req_t *req)
{
    char body[128];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "error", "bad body");
        return send_cjson(req, 400, e);
    }
    cJSON *root = cJSON_Parse(body);
    const cJSON *pw = root ? cJSON_GetObjectItem(root, "password") : NULL;
    bool ok = cJSON_IsString(pw) && pw->valuestring && strcmp(pw->valuestring, s_password) == 0;
    cJSON_Delete(root);
    if (!ok) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "error", "unauthorized");
        return send_cjson(req, 401, e);
    }
    mint_token();
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "token", s_token);
    return send_cjson(req, 200, o);
}

static esp_err_t handler_password(httpd_req_t *req)
{
    if (!authed(req)) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "error", "unauthorized");
        return send_cjson(req, 401, e);
    }
    char body[128];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddBoolToObject(e, "ok", false);
        return send_cjson(req, 400, e);
    }
    cJSON *root = cJSON_Parse(body);
    const cJSON *pw = root ? cJSON_GetObjectItem(root, "password") : NULL;
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (cJSON_IsString(pw) && pw->valuestring) {
        err = save_password(pw->valuestring);
    }
    cJSON_Delete(root);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", err == ESP_OK);
    return send_cjson(req, err == ESP_OK ? 200 : 400, o);
}

static esp_err_t handler_status(httpd_req_t *req)
{
    if (!authed(req)) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "error", "unauthorized");
        return send_cjson(req, 401, e);
    }
    return send_cjson(req, 200, snapshot_json());
}

static esp_err_t handler_bluetooth_bonds(httpd_req_t *req)
{
    if (!authed(req)) {
        return send_unauthorized(req);
    }
    desk_ble_management_snapshot_t snapshot;
    if (!desk_ble_get_management_snapshot(&snapshot)) {
        cJSON *error = cJSON_CreateObject();
        cJSON_AddStringToObject(error, "error", "bluetooth_not_ready");
        return send_cjson(req, 500, error);
    }
    return send_cjson(req, 200, ble_management_snapshot_json(&snapshot));
}

static esp_err_t handler_bluetooth_pairing_window(httpd_req_t *req)
{
    if (!authed(req)) {
        return send_unauthorized(req);
    }
    desk_ble_management_result_t result =
        req->method == HTTP_POST ? desk_ble_open_pairing_window()
                                 : desk_ble_close_pairing_window();
    return send_ble_management_result(req, result);
}

static esp_err_t handler_bluetooth_delete(httpd_req_t *req)
{
    if (!authed(req)) {
        return send_unauthorized(req);
    }
    desk_ble_management_result_t result;
    if (strcmp(req->uri, "/api/v1/bluetooth/bonds") == 0) {
        result = desk_ble_delete_all_bonds();
    } else {
        char bond_id[DESK_BLE_MANAGEMENT_ID_LENGTH];
        if (!desk_web_ble_extract_bond_id(req->uri, bond_id,
                                          sizeof(bond_id))) {
            result = DESK_BLE_MANAGEMENT_NOT_FOUND;
        } else {
            result = desk_ble_delete_bond(bond_id);
        }
    }
    return send_ble_management_result(req, result);
}

static esp_err_t handler_bluetooth_alias(httpd_req_t *req)
{
    if (!authed(req)) {
        return send_unauthorized(req);
    }
    char bond_id[DESK_BLE_MANAGEMENT_ID_LENGTH];
    if (!desk_web_ble_extract_alias_bond_id(req->uri, bond_id,
                                            sizeof(bond_id))) {
        return send_ble_management_result(
            req, DESK_BLE_MANAGEMENT_NOT_FOUND);
    }

    char body[160];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        return send_ble_management_result(
            req, DESK_BLE_MANAGEMENT_INVALID_ARGUMENT);
    }
    cJSON *root = cJSON_Parse(body);
    const cJSON *alias = root ? cJSON_GetObjectItem(root, "alias") : NULL;
    desk_ble_management_result_t result =
        cJSON_IsString(alias)
            ? desk_ble_set_bond_alias(bond_id, alias->valuestring)
            : DESK_BLE_MANAGEMENT_INVALID_ARGUMENT;
    cJSON_Delete(root);
    return send_ble_management_result(req, result);
}

static cJSON *height_presets_json(void)
{
    desk_core_height_preset_snapshot_t snapshot;
    desk_core_get_height_presets(&snapshot);
    cJSON *root = cJSON_CreateObject();
    cJSON *presets = cJSON_AddArrayToObject(root, "presets");
    size_t custom_count = 0;
    for (size_t i = 0; i < snapshot.count; ++i) {
        const desk_core_height_preset_t *preset = &snapshot.presets[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", preset->id);
        cJSON_AddStringToObject(item, "name", preset->name);
        cJSON_AddNumberToObject(item, "height_mm", preset->height_mm);
        cJSON_AddBoolToObject(item, "built_in", preset->built_in);
        cJSON_AddBoolToObject(item, "deletable", preset->deletable);
        cJSON_AddItemToArray(presets, item);
        if (!preset->built_in) {
            custom_count += 1;
        }
    }
    cJSON_AddNumberToObject(root, "custom_count", custom_count);
    cJSON_AddNumberToObject(root, "custom_capacity",
                            snapshot.custom_capacity);
    return root;
}

static esp_err_t send_height_preset_result(httpd_req_t *req, esp_err_t err)
{
    int status = err == ESP_OK ? 200 :
                 err == ESP_ERR_NOT_FOUND ? 404 :
                 err == ESP_ERR_NOT_ALLOWED ? 403 :
                 err == ESP_ERR_NO_MEM ? 409 : 400;
    cJSON *body = cJSON_CreateObject();
    cJSON_AddBoolToObject(body, "ok", err == ESP_OK);
    if (err == ESP_ERR_NOT_FOUND) {
        cJSON_AddStringToObject(body, "error", "preset_not_found");
    } else if (err == ESP_ERR_NOT_ALLOWED) {
        cJSON_AddStringToObject(body, "error", "preset_not_deletable");
    } else if (err == ESP_ERR_NO_MEM) {
        cJSON_AddStringToObject(body, "error", "preset_capacity_full");
    } else if (err != ESP_OK) {
        cJSON_AddStringToObject(body, "error", "invalid_preset");
    }
    return send_cjson(req, status, body);
}

/** 只接受一个档位 ID，防止 wildcard 路由把额外路径片段当作 ID。 */
static bool extract_height_preset_id(const char *uri, bool goto_action,
                                     char *out, size_t out_size)
{
    static const char prefix[] = "/api/v1/desk/height-presets/";
    static const char goto_suffix[] = "/goto";
    if (!uri || !out || out_size == 0 ||
        strncmp(uri, prefix, sizeof(prefix) - 1) != 0) {
        return false;
    }
    const char *start = uri + sizeof(prefix) - 1;
    size_t length = strlen(start);
    if (goto_action) {
        if (length <= sizeof(goto_suffix) - 1 ||
            strcmp(start + length - (sizeof(goto_suffix) - 1),
                   goto_suffix) != 0) {
            return false;
        }
        length -= sizeof(goto_suffix) - 1;
    }
    if (length == 0 || length >= out_size ||
        memchr(start, '/', length) != NULL) {
        return false;
    }
    memcpy(out, start, length);
    out[length] = '\0';
    return true;
}

static esp_err_t handler_height_presets_get(httpd_req_t *req)
{
    return authed(req) ? send_cjson(req, 200, height_presets_json())
                       : send_unauthorized(req);
}

static esp_err_t handler_height_presets_create(httpd_req_t *req)
{
    if (!authed(req)) {
        return send_unauthorized(req);
    }
    char body[192];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        return send_height_preset_result(req, ESP_ERR_INVALID_ARG);
    }
    cJSON *root = cJSON_Parse(body);
    const cJSON *name = root ? cJSON_GetObjectItem(root, "name") : NULL;
    const cJSON *height = root ? cJSON_GetObjectItem(root, "height_mm") : NULL;
    char id[DESK_HEIGHT_PRESET_ID_BUFFER_LENGTH] = {0};
    esp_err_t err = cJSON_IsString(name) && cJSON_IsNumber(height) &&
                            height->valuedouble == (double)height->valueint
                        ? desk_core_create_height_preset(
                              name->valuestring, height->valueint,
                              id, sizeof(id))
                        : ESP_ERR_INVALID_ARG;
    cJSON_Delete(root);
    if (err != ESP_OK) {
        return send_height_preset_result(req, err);
    }
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "ok", true);
    cJSON_AddStringToObject(response, "id", id);
    return send_cjson(req, 200, response);
}

static esp_err_t handler_height_preset_update_or_goto(httpd_req_t *req)
{
    if (!authed(req)) {
        return send_unauthorized(req);
    }
    bool goto_action = strstr(req->uri, "/goto") != NULL;
    char id[DESK_HEIGHT_PRESET_ID_BUFFER_LENGTH];
    if (!extract_height_preset_id(req->uri, goto_action, id, sizeof(id))) {
        return send_height_preset_result(req, ESP_ERR_NOT_FOUND);
    }
    if (goto_action) {
        return send_height_preset_result(
            req, desk_core_goto_height_preset(DESK_CONTROL_SOURCE_REST, id));
    }

    char body[192];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        return send_height_preset_result(req, ESP_ERR_INVALID_ARG);
    }
    cJSON *root = cJSON_Parse(body);
    const cJSON *name = root ? cJSON_GetObjectItem(root, "name") : NULL;
    const cJSON *height = root ? cJSON_GetObjectItem(root, "height_mm") : NULL;
    esp_err_t err = cJSON_IsNumber(height) &&
                            height->valuedouble == (double)height->valueint &&
                            (!name || cJSON_IsString(name))
                        ? desk_core_update_height_preset(
                              id, cJSON_IsString(name) ? name->valuestring
                                                      : NULL,
                              height->valueint)
                        : ESP_ERR_INVALID_ARG;
    cJSON_Delete(root);
    return send_height_preset_result(req, err);
}

static esp_err_t handler_height_preset_delete(httpd_req_t *req)
{
    if (!authed(req)) {
        return send_unauthorized(req);
    }
    char id[DESK_HEIGHT_PRESET_ID_BUFFER_LENGTH];
    if (!extract_height_preset_id(req->uri, false, id, sizeof(id))) {
        return send_height_preset_result(req, ESP_ERR_NOT_FOUND);
    }
    return send_height_preset_result(
        req, desk_core_delete_height_preset(id));
}

/** 给 HTTP 响应留出发送时间，再执行芯片软重启。 */
static void restart_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(800));
    ESP_LOGW(TAG, "restarting by authenticated Web request");
    esp_restart();
}

static esp_err_t handler_restart(httpd_req_t *req)
{
    if (!authed(req)) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "error", "unauthorized");
        return send_cjson(req, 401, e);
    }

    esp_err_t err = s_restart_pending ? ESP_ERR_INVALID_STATE : desk_core_stop();
    if (err == ESP_OK) {
        /* HTTP server 串行处理 handler，pending 可阻止倒计时内重复创建任务。 */
        s_restart_pending = true;
        if (xTaskCreate(restart_task, "web_restart", 2048, NULL,
                        tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
            s_restart_pending = false;
            err = ESP_ERR_NO_MEM;
        }
    }

    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", err == ESP_OK);
    cJSON_AddStringToObject(o, "err", esp_err_to_name(err));
    return send_cjson(req, err == ESP_OK ? 200 : 400, o);
}

static esp_err_t handler_cmd(httpd_req_t *req)
{
    if (!authed(req)) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "error", "unauthorized");
        return send_cjson(req, 401, e);
    }
    const char *uri = req->uri;
    esp_err_t err = ESP_ERR_NOT_FOUND;
    if (strcmp(uri, "/api/v1/desk/controller/reset") == 0) {
        err = desk_core_reset_controller(DESK_CONTROL_SOURCE_REST);
    } else if (strcmp(uri, "/api/v1/desk/raise-to-max") == 0) {
        err = desk_core_raise_to_max(DESK_CONTROL_SOURCE_REST);
    } else if (strcmp(uri, "/api/v1/desk/jog/up") == 0) {
        err = desk_core_jog_up(DESK_CONTROL_SOURCE_REST);
    } else if (strcmp(uri, "/api/v1/desk/jog/down") == 0) {
        err = desk_core_jog_down(DESK_CONTROL_SOURCE_REST);
    } else if (strstr(uri, "/desk/up")) {
        err = desk_core_hold_up(DESK_CONTROL_SOURCE_REST);
    } else if (strstr(uri, "/desk/down")) {
        err = desk_core_hold_down(DESK_CONTROL_SOURCE_REST);
    } else if (strstr(uri, "/desk/stop")) {
#if CONFIG_DESK_MOTION_DIAGNOSTICS
        ESP_LOGI(TAG, "motion ingress source=rest command=stop uri=%s", uri);
#endif
        err = desk_core_stop();
    } else if (strstr(uri, "/presets")) {
        char body[96];
        if (read_body(req, body, sizeof(body)) == ESP_OK) {
            cJSON *root = cJSON_Parse(body);
            const cJSON *preset1 = root ?
                cJSON_GetObjectItem(root, "preset1_height_mm") : NULL;
            const cJSON *preset4 = root ?
                cJSON_GetObjectItem(root, "preset4_height_mm") : NULL;
            if (cJSON_IsNumber(preset1) && cJSON_IsNumber(preset4) &&
                preset1->valuedouble == (double)preset1->valueint &&
                preset4->valuedouble == (double)preset4->valueint) {
                err = desk_core_set_preset_heights_mm(preset1->valueint,
                                                       preset4->valueint);
            } else {
                err = ESP_ERR_INVALID_ARG;
            }
            cJSON_Delete(root);
        } else {
            err = ESP_ERR_INVALID_ARG;
        }
    } else if (strstr(uri, "/max-height")) {
        char body[64];
        if (read_body(req, body, sizeof(body)) == ESP_OK) {
            cJSON *root = cJSON_Parse(body);
            const cJSON *max_height =
                root ? cJSON_GetObjectItem(root, "max_height_mm") : NULL;
            if (cJSON_IsNumber(max_height) &&
                max_height->valuedouble == (double)max_height->valueint) {
                err = desk_core_set_max_height_mm(max_height->valueint);
            } else {
                err = ESP_ERR_INVALID_ARG;
            }
            cJSON_Delete(root);
        } else {
            err = ESP_ERR_INVALID_ARG;
        }
    } else if (strstr(uri, "/child-lock")) {
        char body[64];
        if (read_body(req, body, sizeof(body)) == ESP_OK) {
            cJSON *root = cJSON_Parse(body);
            const cJSON *en = root ? cJSON_GetObjectItem(root, "enabled") : NULL;
            if (cJSON_IsBool(en)) {
                err = desk_core_set_child_lock(cJSON_IsTrue(en));
            } else {
                err = ESP_ERR_INVALID_ARG;
            }
            cJSON_Delete(root);
        } else {
            err = ESP_ERR_INVALID_ARG;
        }
    } else if (strstr(uri, "/access")) {
        char body[96];
        if (read_body(req, body, sizeof(body)) == ESP_OK) {
            cJSON *root = cJSON_Parse(body);
            const cJSON *source =
                root ? cJSON_GetObjectItem(root, "source") : NULL;
            const cJSON *en = root ? cJSON_GetObjectItem(root, "enabled") : NULL;
            desk_control_source_t parsed_source;
            if (cJSON_IsString(source) && cJSON_IsBool(en) &&
                parse_control_source(source->valuestring, &parsed_source)) {
                err = desk_core_set_source_enabled(parsed_source,
                                                   cJSON_IsTrue(en));
            } else {
                err = ESP_ERR_INVALID_ARG;
            }
            cJSON_Delete(root);
        } else {
            err = ESP_ERR_INVALID_ARG;
        }
    } else {
        int n = 0;
        if (sscanf(uri, "/api/v1/desk/preset/%d/goto", &n) == 1) {
            err = desk_core_goto_preset(DESK_CONTROL_SOURCE_REST, (uint8_t)n);
        } else if (sscanf(uri, "/api/v1/desk/preset/%d/save", &n) == 1) {
            err = desk_core_save_preset(DESK_CONTROL_SOURCE_REST, (uint8_t)n);
        }
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", err == ESP_OK);
    cJSON_AddStringToObject(o, "err", esp_err_to_name(err));
    if (err == ESP_ERR_NOT_ALLOWED) {
        desk_core_snapshot_t snapshot = desk_core_snapshot();
        cJSON_AddStringToObject(o, "reason",
                                snapshot.child_lock ? "child_lock" :
                                                      "source_disabled");
    }
    return send_cjson(req, err == ESP_OK ? 200 :
                           err == ESP_ERR_NOT_ALLOWED ? 403 : 400, o);
}

static bool json_integer_in_range(const cJSON *item, int minimum, int maximum,
                                  int *out_value)
{
    if (!cJSON_IsNumber(item) || item->valuedouble != (double)item->valueint ||
        item->valueint < minimum || item->valueint > maximum) {
        return false;
    }
    *out_value = item->valueint;
    return true;
}

/** 鉴权后的提醒动作入口；非法状态用 409 明确反馈，不静默改动作。 */
static esp_err_t handler_reminder_action(httpd_req_t *req)
{
    if (!authed(req)) return send_unauthorized(req);
    char body[96];
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (read_body(req, body, sizeof(body)) == ESP_OK) {
        cJSON *root = cJSON_Parse(body);
        const cJSON *action = root ? cJSON_GetObjectItem(root, "action") : NULL;
        desk_reminder_action_t parsed;
        if (cJSON_IsString(action) &&
            desk_reminder_action_from_name(action->valuestring, &parsed)) {
            err = desk_reminder_perform(parsed);
        }
        cJSON_Delete(root);
    }
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "ok", err == ESP_OK);
    cJSON_AddStringToObject(response, "err", esp_err_to_name(err));
    return send_cjson(req, err == ESP_OK ? 200 :
                           err == ESP_ERR_INVALID_STATE ? 409 : 400, response);
}

/** 试听只接受登记资源 ID，永远不把客户端字符串解释为文件路径。 */
static esp_err_t handler_audio_action(httpd_req_t *req)
{
    if (!authed(req)) return send_unauthorized(req);
    char body[128];
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (read_body(req, body, sizeof(body)) == ESP_OK) {
        cJSON *root = cJSON_Parse(body);
        const cJSON *action = root ? cJSON_GetObjectItem(root, "action") : NULL;
        if (cJSON_IsString(action) &&
            strcmp(action->valuestring, "stop_audio") == 0) {
            err = desk_audio_stop();
        } else if (cJSON_IsString(action) &&
                   strcmp(action->valuestring, "test_audio") == 0) {
            const cJSON *prompt = cJSON_GetObjectItem(root, "prompt_id");
            desk_audio_prompt_t parsed;
            if (cJSON_IsString(prompt) &&
                desk_audio_prompt_from_name(prompt->valuestring, &parsed)) {
                err = desk_audio_play(parsed, DESK_AUDIO_PRIORITY_PREVIEW);
            }
        }
        cJSON_Delete(root);
    }
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "ok", err == ESP_OK);
    cJSON_AddStringToObject(response, "err", esp_err_to_name(err));
    return send_cjson(req, err == ESP_OK ? 200 :
                           err == ESP_ERR_INVALID_STATE ? 409 : 400, response);
}

static esp_err_t handler_reminder_config(httpd_req_t *req)
{
    if (!authed(req)) return send_unauthorized(req);
    char body[320];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "error", "invalid_config");
        return send_cjson(req, 400, response);
    }
    cJSON *root = cJSON_Parse(body);
    if (!root) {
        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "error", "invalid_config");
        return send_cjson(req, 400, response);
    }

    desk_reminder_config_patch_t patch = {0};
    bool timing_changed = false;
    bool audio_changed = false;
    bool valid = true;
    int parsed = 0;
#define PARSE_CONFIG_INT(json_name, has_field, target_field, minimum, maximum) \
    do { \
        const cJSON *item = cJSON_GetObjectItem(root, json_name); \
        if (item) { \
            if (!json_integer_in_range(item, minimum, maximum, &parsed)) { \
                valid = false; \
            } else { \
                patch.has_field = true; \
                patch.target_field = parsed; \
                timing_changed = true; \
            } \
        } \
    } while (0)
    PARSE_CONFIG_INT("focus_minutes", has_focus_minutes, focus_minutes, 1, 180);
    PARSE_CONFIG_INT("short_break_minutes", has_short_break_minutes,
                     short_break_minutes, 1, 60);
    PARSE_CONFIG_INT("long_break_minutes", has_long_break_minutes,
                     long_break_minutes, 1, 120);
    PARSE_CONFIG_INT("focuses_per_long_break", has_focuses_per_long_break,
                     focuses_per_long_break, 1, 12);
#undef PARSE_CONFIG_INT

    desk_audio_snapshot_t old_audio = desk_audio_snapshot();
    bool audio_enabled = old_audio.enabled;
    uint8_t volume = old_audio.volume_percent;
    const cJSON *enabled_item = cJSON_GetObjectItem(root, "audio_enabled");
    if (enabled_item) {
        valid = valid && cJSON_IsBool(enabled_item);
        if (cJSON_IsBool(enabled_item)) audio_enabled = cJSON_IsTrue(enabled_item);
        audio_changed = true;
    }
    const cJSON *volume_item = cJSON_GetObjectItem(root, "volume_percent");
    if (volume_item) {
        if (!json_integer_in_range(volume_item, 0, 100, &parsed)) {
            valid = false;
        } else {
            volume = (uint8_t)parsed;
        }
        audio_changed = true;
    }
    const cJSON *voice_pack = cJSON_GetObjectItem(root, "voice_pack");
    if (voice_pack && (!cJSON_IsString(voice_pack) ||
                       strcmp(voice_pack->valuestring,
                              DESK_AUDIO_VOICE_PACK) != 0)) {
        valid = false;
    }
    valid = valid && (timing_changed || audio_changed || voice_pack);

    desk_reminder_snapshot_t old_reminder = desk_reminder_snapshot();
    esp_err_t err = valid ? ESP_OK : ESP_ERR_INVALID_ARG;
    if (err == ESP_OK && timing_changed) {
        err = desk_reminder_update_config(&patch);
    }
    if (err == ESP_OK && audio_changed) {
        err = desk_audio_set_config(audio_enabled, volume);
        if (err != ESP_OK && timing_changed) {
            /* 跨组件写入失败时尽力恢复旧时长，避免返回失败却留下半份配置。 */
            desk_reminder_config_patch_t rollback = {
                .has_focus_minutes = true,
                .has_short_break_minutes = true,
                .has_long_break_minutes = true,
                .has_focuses_per_long_break = true,
                .focus_minutes = old_reminder.config.focus_minutes,
                .short_break_minutes = old_reminder.config.short_break_minutes,
                .long_break_minutes = old_reminder.config.long_break_minutes,
                .focuses_per_long_break =
                    old_reminder.config.focuses_per_long_break,
            };
            (void)desk_reminder_update_config(&rollback);
        }
    }
    cJSON_Delete(root);
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "ok", err == ESP_OK);
    cJSON_AddStringToObject(response, "err", esp_err_to_name(err));
    return send_cjson(req, err == ESP_OK ? 200 : 400, response);
}

static esp_err_t send_embed(httpd_req_t *req, const char *type,
                            const uint8_t *start, const uint8_t *end)
{
    httpd_resp_set_type(req, type);
    return httpd_resp_send(req, (const char *)start, end - start);
}

static esp_err_t handler_root(httpd_req_t *req)
{
    /* SoftAP 配网期直接进 setup，避免卡在登录页 */
    if (desk_wifi_is_ap_active()) {
        return send_embed(req, "text/html", www_setup_html_start, www_setup_html_end);
    }
    return send_embed(req, "text/html", www_index_html_start, www_index_html_end);
}
static esp_err_t handler_login_page(httpd_req_t *req)
{
    return send_embed(req, "text/html", www_login_html_start, www_login_html_end);
}
static esp_err_t handler_setup_page(httpd_req_t *req)
{
    return send_embed(req, "text/html", www_setup_html_start, www_setup_html_end);
}
static esp_err_t handler_app_js(httpd_req_t *req)
{
    return send_embed(req, "application/javascript", www_app_js_start, www_app_js_end);
}
static esp_err_t handler_hold_control_js(httpd_req_t *req)
{
    return send_embed(req, "application/javascript", www_hold_control_js_start,
                      www_hold_control_js_end);
}
static esp_err_t handler_bond_management_js(httpd_req_t *req)
{
    return send_embed(req, "application/javascript",
                      www_bond_management_js_start,
                      www_bond_management_js_end);
}
static esp_err_t handler_height_presets_js(httpd_req_t *req)
{
    return send_embed(req, "application/javascript",
                      www_height_presets_js_start,
                      www_height_presets_js_end);
}
static esp_err_t handler_reminder_control_js(httpd_req_t *req)
{
    return send_embed(req, "application/javascript",
                      www_reminder_control_js_start,
                      www_reminder_control_js_end);
}
static esp_err_t handler_css(httpd_req_t *req)
{
    return send_embed(req, "text/css", www_style_css_start, www_style_css_end);
}
static esp_err_t handler_favicon(httpd_req_t *req)
{
    /* 浏览器会在登录和配网页面之前请求图标，因此保持此静态资源免鉴权。 */
    return send_embed(req, "image/png", www_favicon_png_start,
                      www_favicon_png_end);
}
static esp_err_t handler_desk_workstation(httpd_req_t *req)
{
    return send_embed(req, "image/webp", www_desk_workstation_webp_start,
                      www_desk_workstation_webp_end);
}

/** SoftAP 下免登录写 WiFi；已入网后拒绝（防局域网未授权改密） */
static esp_err_t handler_setup_wifi(httpd_req_t *req)
{
    if (!desk_wifi_is_ap_active()) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "error", "not in setup mode");
        return send_cjson(req, 403, e);
    }
    char body[192];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddBoolToObject(e, "ok", false);
        return send_cjson(req, 400, e);
    }
    cJSON *root = cJSON_Parse(body);
    const cJSON *ssid = root ? cJSON_GetObjectItem(root, "ssid") : NULL;
    const cJSON *password = root ? cJSON_GetObjectItem(root, "password") : NULL;
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (cJSON_IsString(ssid) && ssid->valuestring && ssid->valuestring[0]) {
        const char *pass = (cJSON_IsString(password) && password->valuestring) ? password->valuestring : "";
        err = desk_wifi_set_sta(ssid->valuestring, pass);
    }
    cJSON_Delete(root);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", err == ESP_OK);
    cJSON_AddStringToObject(o, "err", esp_err_to_name(err));
    return send_cjson(req, err == ESP_OK ? 200 : 400, o);
}

esp_err_t desk_web_start(void)
{
    /* 幂等：STA 可能多次 GOT_IP（重连），只启动一次 */
    if (s_server) {
        return ESP_OK;
    }

    load_password();
    mint_token();

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.max_uri_handlers = 32;
    cfg.max_open_sockets = 7;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 8192;

    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        s_server = NULL;
        return err;
    }

    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = handler_root},
        {.uri = "/setup.html", .method = HTTP_GET, .handler = handler_setup_page},
        {.uri = "/login.html", .method = HTTP_GET, .handler = handler_login_page},
        {.uri = "/hold-control.js", .method = HTTP_GET, .handler = handler_hold_control_js},
        {.uri = "/bond-management.js", .method = HTTP_GET, .handler = handler_bond_management_js},
        {.uri = "/height-presets.js", .method = HTTP_GET, .handler = handler_height_presets_js},
        {.uri = "/reminder-control.js", .method = HTTP_GET, .handler = handler_reminder_control_js},
        {.uri = "/app.js", .method = HTTP_GET, .handler = handler_app_js},
        {.uri = "/style.css", .method = HTTP_GET, .handler = handler_css},
        {.uri = "/favicon.png", .method = HTTP_GET, .handler = handler_favicon},
        {.uri = "/desk-workstation.webp", .method = HTTP_GET, .handler = handler_desk_workstation},
        {.uri = "/api/v1/setup/wifi", .method = HTTP_POST, .handler = handler_setup_wifi},
        {.uri = "/api/v1/auth/login", .method = HTTP_POST, .handler = handler_login},
        {.uri = "/api/v1/auth/password", .method = HTTP_POST, .handler = handler_password},
        {.uri = "/api/v1/desk/status", .method = HTTP_GET, .handler = handler_status},
        {.uri = "/api/v1/desk/height-presets", .method = HTTP_GET, .handler = handler_height_presets_get},
        {.uri = "/api/v1/desk/height-presets", .method = HTTP_POST, .handler = handler_height_presets_create},
        {.uri = "/api/v1/desk/height-presets/*", .method = HTTP_POST, .handler = handler_height_preset_update_or_goto},
        {.uri = "/api/v1/desk/height-presets/*", .method = HTTP_DELETE, .handler = handler_height_preset_delete},
        {.uri = "/api/v1/bluetooth/bonds", .method = HTTP_GET, .handler = handler_bluetooth_bonds},
        {.uri = "/api/v1/bluetooth/pairing-window", .method = HTTP_POST, .handler = handler_bluetooth_pairing_window},
        {.uri = "/api/v1/bluetooth/pairing-window", .method = HTTP_DELETE, .handler = handler_bluetooth_pairing_window},
        {.uri = "/api/v1/bluetooth/bonds", .method = HTTP_DELETE, .handler = handler_bluetooth_delete},
        {.uri = "/api/v1/bluetooth/bonds/*", .method = HTTP_DELETE, .handler = handler_bluetooth_delete},
        {.uri = "/api/v1/bluetooth/bonds/*", .method = HTTP_POST, .handler = handler_bluetooth_alias},
        {.uri = "/api/v1/system/restart", .method = HTTP_POST, .handler = handler_restart},
        {.uri = "/api/v1/reminder/action", .method = HTTP_POST, .handler = handler_reminder_action},
        {.uri = "/api/v1/reminder/config", .method = HTTP_POST, .handler = handler_reminder_config},
        {.uri = "/api/v1/audio/action", .method = HTTP_POST, .handler = handler_audio_action},
        {.uri = "/api/v1/desk/*", .method = HTTP_POST, .handler = handler_cmd},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(s_server, &routes[i]);
    }
    ESP_LOGI(TAG, "HTTP on :80");
    return ESP_OK;
}
