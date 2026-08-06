/**
 * @file i2c_panel_slave.h
 * @brief 模拟 TM1650 键通道：I²C Slave @ 0x24
 *
 * Phase 1 只应答键读；不写 digit、不旁路听高度（后续再加）。
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 安装 I²C Slave 并启动应答任务。
 *
 * 接线约束：USB 给 ESP32 供电；与控制盒共 GND；不要接桌子 3.3V 作主供电。
 * 原厂面板 Phase 1 可拔掉，只留主机 ↔ ESP32 一根线。
 */
esp_err_t i2c_panel_slave_start(void);

#ifdef __cplusplus
}
#endif
