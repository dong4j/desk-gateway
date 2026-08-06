/**
 * @file desk_dr.h
 * @brief 升降桌键态 DR 契约（与 docs/3 §18 对齐）
 *
 * 控制盒是 I²C Master，每 ~3.7ms 写 DW=0x01 再读 1 字节 DR。
 * 控桌 = 改 Slave 回的 DR，不是主动发「升桌帧」。
 * Preset2/3 未抓包验证，禁止伪造键码。
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 7-bit 键通道地址（PulseView）；8-bit 写=0x48 / 读=0x49 */
#define DESK_I2C_ADDR_KEY_7BIT   0x24u

/** Master 写控制字节；Phase 1 可忽略位域，但必须 ACK 整次写 */
#define DESK_DW_POLL             0x01u

/** 停止 / 空闲（松开升、降后必须立刻回到此值） */
#define DESK_DR_IDLE             0x2Eu
/** 升高：等同按住上升键，维持多久桌子就升多久 */
#define DESK_DR_UP               0x47u
/** 降低：等同按住下降键 */
#define DESK_DR_DOWN             0x4Fu
/** 前往档位 1（短码）；不要用长码 0x57 */
#define DESK_DR_PRESET1_GOTO     0x17u
/** 保存当前高度到档位 1（长码 = 短码 | 0x40），需维持约 ≥4s */
#define DESK_DR_PRESET1_SAVE     0x57u
/** 前往档位 4（短码） */
#define DESK_DR_PRESET4_GOTO     0x2Fu
/** 保存档位 4（长码） */
#define DESK_DR_PRESET4_SAVE     0x6Fu

/**
 * @brief DR 状态机初始化：上电强制 idle，启动运动超时看门狗。
 *
 * 约束：任何异常路径都应能回到 DESK_DR_IDLE，避免桌子失控。
 */
void desk_dr_init(void);

/** 当前应答字节（I²C 读周期直接交卷给 Master） */
uint8_t desk_dr_get(void);

/**
 * @brief 立刻切换 DR（升/降会启动最长按住超时）。
 * @param dr 仅允许已验证键码；未知值会被拒绝并保持原状。
 * @return true 已接受；false 拒绝（未验证码）
 */
bool desk_dr_set(uint8_t dr);

/** 强制停止：DR=idle，取消定时回 idle 的 one-shot */
void desk_dr_stop(void);

/**
 * @brief 短暂维持某 DR，到期自动 idle（用于 goto/save）。
 * @param dr   短码或长码
 * @param hold_ms 维持毫秒；0 表示按 Kconfig 默认（goto/save 各一套）
 */
bool desk_dr_pulse(uint8_t dr, uint32_t hold_ms);

/** 人类可读名，供串口 status */
const char *desk_dr_name(uint8_t dr);

#ifdef __cplusplus
}
#endif
