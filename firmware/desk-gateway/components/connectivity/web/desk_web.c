/**
 * @file desk_web.c
 * @brief Bearer 认证 + REST + 嵌入静态资源
 *
 * 状态由前端短轮询获取。ESP-IDF HTTP handler 在 server task 中执行，
 * 不在同步 handler 内维持长连接，避免状态流阻塞急停等控制请求。
 *
 * JSON 通过 Component Manager 依赖 espressif/cjson（IDF 6 已移出内置 json）。
 */
#include "desk_web.h"

#include "desk_core.h"
#include "desk_wifi.h"

#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
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

/* EMBED_FILES 用路径 www/xxx，但符号只取文件名：_binary_<name_with_underscores>_* */
extern const uint8_t www_login_html_start[] asm("_binary_login_html_start");
extern const uint8_t www_login_html_end[] asm("_binary_login_html_end");
extern const uint8_t www_index_html_start[] asm("_binary_index_html_start");
extern const uint8_t www_index_html_end[] asm("_binary_index_html_end");
extern const uint8_t www_app_js_start[] asm("_binary_app_js_start");
extern const uint8_t www_app_js_end[] asm("_binary_app_js_end");
extern const uint8_t www_style_css_start[] asm("_binary_style_css_start");
extern const uint8_t www_style_css_end[] asm("_binary_style_css_end");
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
                                status == 401 ? "401 Unauthorized" : "400 Bad Request");
    esp_err_t err = httpd_resp_sendstr(req, body);
    free(body);
    return err;
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
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "status", status_str(s.status));
    if (s.height_known) {
        cJSON_AddNumberToObject(o, "height_mm", s.height_mm);
    } else {
        cJSON_AddNullToObject(o, "height_mm");
    }
    cJSON_AddBoolToObject(o, "height_known", s.height_known);
    cJSON_AddBoolToObject(o, "height_sim", s.height_sim);
    cJSON_AddBoolToObject(o, "child_lock", s.child_lock);
    cJSON_AddBoolToObject(o, "upward_blocked", s.upward_blocked);
    cJSON_AddNumberToObject(o, "max_height_mm", s.max_height_mm);
    cJSON_AddStringToObject(o, "driver", s.driver ? s.driver : "none");
    cJSON_AddNumberToObject(o, "ts_ms", (double)esp_log_timestamp());
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

static esp_err_t handler_cmd(httpd_req_t *req)
{
    if (!authed(req)) {
        cJSON *e = cJSON_CreateObject();
        cJSON_AddStringToObject(e, "error", "unauthorized");
        return send_cjson(req, 401, e);
    }
    const char *uri = req->uri;
    esp_err_t err = ESP_ERR_NOT_FOUND;
    if (strstr(uri, "/desk/up")) {
        err = desk_core_hold_up();
    } else if (strstr(uri, "/desk/down")) {
        err = desk_core_hold_down();
    } else if (strstr(uri, "/desk/stop")) {
        err = desk_core_stop();
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
    } else {
        int n = 0;
        if (sscanf(uri, "/api/v1/desk/preset/%d/goto", &n) == 1) {
            err = desk_core_goto_preset((uint8_t)n);
        } else if (sscanf(uri, "/api/v1/desk/preset/%d/save", &n) == 1) {
            err = desk_core_save_preset((uint8_t)n);
        }
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "ok", err == ESP_OK);
    cJSON_AddStringToObject(o, "err", esp_err_to_name(err));
    return send_cjson(req, err == ESP_OK ? 200 : 400, o);
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
static esp_err_t handler_css(httpd_req_t *req)
{
    return send_embed(req, "text/css", www_style_css_start, www_style_css_end);
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
    cfg.max_uri_handlers = 20;
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
        {.uri = "/app.js", .method = HTTP_GET, .handler = handler_app_js},
        {.uri = "/style.css", .method = HTTP_GET, .handler = handler_css},
        {.uri = "/api/v1/setup/wifi", .method = HTTP_POST, .handler = handler_setup_wifi},
        {.uri = "/api/v1/auth/login", .method = HTTP_POST, .handler = handler_login},
        {.uri = "/api/v1/auth/password", .method = HTTP_POST, .handler = handler_password},
        {.uri = "/api/v1/desk/status", .method = HTTP_GET, .handler = handler_status},
        {.uri = "/api/v1/desk/*", .method = HTTP_POST, .handler = handler_cmd},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(s_server, &routes[i]);
    }
    ESP_LOGI(TAG, "HTTP on :80");
    return ESP_OK;
}
