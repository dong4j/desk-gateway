/**
 * @file console_cmd.h
 * @brief UART 行命令：切换 DR（Phase 1 主控手段）
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** 阻塞读 stdin 行并执行；在独立任务中调用 */
void console_cmd_task(void *arg);

#ifdef __cplusplus
}
#endif
