/**
 * @file desk_web.h
 * @brief 局域网 HTTP：认证 + REST + SSE + 静态页
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t desk_web_start(void);

#ifdef __cplusplus
}
#endif
