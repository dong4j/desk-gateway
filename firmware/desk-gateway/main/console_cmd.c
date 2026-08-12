/**
 * @file console_cmd.c
 * @brief 串口 REPL → desk_core / wifi
 *
 * 不用 fgets：USB Serial/JTAG + 部分监视器/输入法会把「单字符」当成一行，
 * 导致 unknown 'w'。改为 getchar 组行，仅在收到 CR/LF 时执行。
 */
#include "console_cmd.h"

#include "desk_core.h"
#include "desk_wifi.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "console";

static void print_help(void)
{
    printf("\nDesk Gateway — yourdesk_v1 @0x24\n"
           "  help | status | stop | idle | up | down\n"
           "  p1 | p4 | save1 | save4\n"
           "  lock | unlock\n"
           "  wifi <ssid> <pass> | wifi status\n"
           "LAN Web: http://<ip>/  default password from Kconfig\n"
           "(type full line, then Enter; use English IME)\n\n");
}

static void trim_inplace(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) {
        s[--n] = '\0';
    }
    char *p = s;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (p != s) {
        memmove(s, p, strlen(p) + 1);
    }
}

static void handle_line(char *line)
{
    trim_inplace(line);
    if (!line[0]) {
        return;
    }
    if (!strcmp(line, "help") || !strcmp(line, "?")) {
        print_help();
        return;
    }
    if (!strcmp(line, "status")) {
        desk_core_snapshot_t s = desk_core_snapshot();
        printf("status=%d child_lock=%d driver=%s height_known=%d\n",
               (int)s.status, (int)s.child_lock, s.driver, (int)s.height_known);
        return;
    }
    if (!strcmp(line, "stop") || !strcmp(line, "idle")) {
        desk_core_stop();
        return;
    }
    if (!strcmp(line, "up")) {
        desk_core_hold_up(DESK_CONTROL_SOURCE_CONSOLE);
        return;
    }
    if (!strcmp(line, "down")) {
        desk_core_hold_down(DESK_CONTROL_SOURCE_CONSOLE);
        return;
    }
    if (!strcmp(line, "p1")) {
        desk_core_goto_preset(DESK_CONTROL_SOURCE_CONSOLE, 1);
        return;
    }
    if (!strcmp(line, "p4")) {
        desk_core_goto_preset(DESK_CONTROL_SOURCE_CONSOLE, 4);
        return;
    }
    if (!strcmp(line, "save1")) {
        desk_core_save_preset(DESK_CONTROL_SOURCE_CONSOLE, 1);
        return;
    }
    if (!strcmp(line, "save4")) {
        desk_core_save_preset(DESK_CONTROL_SOURCE_CONSOLE, 4);
        return;
    }
    if (!strcmp(line, "lock")) {
        desk_core_set_child_lock(true);
        return;
    }
    if (!strcmp(line, "unlock")) {
        desk_core_set_child_lock(false);
        return;
    }
    if (!strcmp(line, "wifi status")) {
        char ip[24] = "-";
        (void)desk_wifi_get_ip(ip, sizeof(ip));
        printf("wifi connected=%d ip=%s\n", (int)desk_wifi_is_connected(), ip);
        return;
    }
    if (!strncmp(line, "wifi ", 5)) {
        char ssid[33] = {0};
        char pass[65] = {0};
        if (sscanf(line + 5, "%32s %64s", ssid, pass) >= 1) {
            esp_err_t err = desk_wifi_set_sta(ssid, pass);
            printf("wifi set: %s\n", esp_err_to_name(err));
        } else {
            printf("usage: wifi <ssid> <pass>\n");
        }
        return;
    }
    printf("unknown '%s'\n", line);
    ESP_LOGW(TAG, "unknown: %s", line);
}

/**
 * 阻塞读一行：回显可打印字符，支持退格；仅 CR/LF 结束。
 * @return 字符数（不含结尾 NUL）；空行也可为 0
 */
static int read_line(char *buf, size_t buflen)
{
    size_t i = 0;
    while (i + 1 < buflen) {
        int c = getchar();
        if (c == EOF) {
            clearerr(stdin);
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        /* 忽略单独的提示符回环 / 控制噪声 */
        if (c == '\r' || c == '\n') {
            putchar('\n');
            fflush(stdout);
            break;
        }
        if (c == '\b' || c == 0x7f) {
            if (i > 0) {
                i--;
                printf("\b \b");
                fflush(stdout);
            }
            continue;
        }
        if (c < 32 || c > 126) {
            continue;
        }
        buf[i++] = (char)c;
        putchar(c);
        fflush(stdout);
    }
    buf[i] = '\0';
    return (int)i;
}

void console_cmd_task(void *arg)
{
    (void)arg;
    char line[96];
    print_help();
    for (;;) {
        printf("> ");
        fflush(stdout);
        (void)read_line(line, sizeof(line));
        handle_line(line);
    }
}
