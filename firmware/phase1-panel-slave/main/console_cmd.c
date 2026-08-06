/**
 * @file console_cmd.c
 * @brief 串口 REPL：idle/up/down/stop + 已验证档位
 *
 * 用简单 fgets 而非 esp_console，减少依赖；115200 8N1 接 CH343。
 */
#include "console_cmd.h"

#include "desk_dr.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "console";

static void print_help(void)
{
    printf("\nDesk Gateway Phase 1 — panel slave @0x24\n"
           "Commands (type + Enter):\n"
           "  help              show this help\n"
           "  status            print current DR\n"
           "  idle | stop       DR=0x2E (safe default)\n"
           "  up                DR=0x47 hold (auto-stop by motion timeout)\n"
           "  down              DR=0x4F hold\n"
           "  p1 | preset1      goto preset1 (0x17, short pulse)\n"
           "  save1             save preset1 (0x57, >=4s pulse)\n"
           "  p4 | preset4      goto preset4 (0x2F)\n"
           "  save4             save preset4 (0x6F)\n"
           "  dr <hex>          set verified DR only (e.g. dr 2e)\n"
           "Note: preset2/3 codes unknown — rejected.\n"
           "Safety: watch the desk; USB power ESP32; share GND with host.\n\n");
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
    if (line[0] == '\0') {
        return;
    }

    if (strcmp(line, "help") == 0 || strcmp(line, "?") == 0) {
        print_help();
        return;
    }
    if (strcmp(line, "status") == 0) {
        uint8_t dr = desk_dr_get();
        printf("DR=0x%02X (%s)\n", dr, desk_dr_name(dr));
        return;
    }
    if (strcmp(line, "idle") == 0 || strcmp(line, "stop") == 0) {
        desk_dr_stop();
        return;
    }
    if (strcmp(line, "up") == 0) {
        desk_dr_set(DESK_DR_UP);
        return;
    }
    if (strcmp(line, "down") == 0) {
        desk_dr_set(DESK_DR_DOWN);
        return;
    }
    if (strcmp(line, "p1") == 0 || strcmp(line, "preset1") == 0) {
        desk_dr_pulse(DESK_DR_PRESET1_GOTO, 0);
        return;
    }
    if (strcmp(line, "save1") == 0) {
        desk_dr_pulse(DESK_DR_PRESET1_SAVE, 0);
        return;
    }
    if (strcmp(line, "p4") == 0 || strcmp(line, "preset4") == 0) {
        desk_dr_pulse(DESK_DR_PRESET4_GOTO, 0);
        return;
    }
    if (strcmp(line, "save4") == 0) {
        desk_dr_pulse(DESK_DR_PRESET4_SAVE, 0);
        return;
    }
    if (strncmp(line, "dr ", 3) == 0) {
        unsigned v = 0;
        if (sscanf(line + 3, "%x", &v) == 1 && v <= 0xFF) {
            if (!desk_dr_set((uint8_t)v)) {
                printf("rejected 0x%02X\n", v);
            }
        } else {
            printf("usage: dr <hex>\n");
        }
        return;
    }

    printf("unknown '%s' — type help\n", line);
    ESP_LOGW(TAG, "unknown cmd: %s", line);
}

void console_cmd_task(void *arg)
{
    (void)arg;
    char line[64];
    print_help();
    for (;;) {
        printf("> ");
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) == NULL) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        handle_line(line);
    }
}
