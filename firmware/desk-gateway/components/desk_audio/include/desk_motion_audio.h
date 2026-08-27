/**
 * @file desk_motion_audio.h
 * @brief 将统一控桌事件转换成本地语音和接近上限的变频提示音。
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 注册非阻塞 desk_core 观察者并启动低优先级提示任务。
 *
 * 播放失败属于辅助能力降级，调用方不得因此阻止桌控入口启动。
 */
esp_err_t desk_motion_audio_start(void);

#ifdef __cplusplus
}
#endif
