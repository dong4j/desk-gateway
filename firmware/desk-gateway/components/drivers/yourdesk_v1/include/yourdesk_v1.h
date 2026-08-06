/**
 * @file yourdesk_v1.h
 * @brief 用户桌协议驱动：I²C Slave 模拟 TM1650 键通道 @0x24
 */
#pragma once

#include "desk_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 可注册到 desk_core 的驱动实例 */
extern const desk_driver_t yourdesk_v1_driver;

#ifdef __cplusplus
}
#endif
