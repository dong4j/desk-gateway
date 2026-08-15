/**
 * 升降桌高度展示口径。
 *
 * 当前产品直接使用 TOF400C 的处理后距离，固定有效范围为 560–940 mm。用户设置的
 * 最高安全高度只是运动限制，不能改变首页标尺的物理量程。
 */

import type { DeskMotion } from './types';

export const DESK_MIN_HEIGHT_MM = 560;
export const DESK_MAX_HEIGHT_MM = 940;
export const DESK_DEFAULT_SIT_HEIGHT_MM = 560;
export const DESK_DEFAULT_STAND_HEIGHT_MM = 870;
export const DESK_RULER_LABELS_CM = [94, 85, 75, 65, 56] as const;

/** 将设备高度映射到固定产品量程；安全上限变化不得改变这个结果。 */
export function normalizeDeskHeight(heightMm: number | null): number {
  if (heightMm === null) {
    return 0.5;
  }
  return Math.max(
    0,
    Math.min(
      1,
      (heightMm - DESK_MIN_HEIGHT_MM) /
        (DESK_MAX_HEIGHT_MM - DESK_MIN_HEIGHT_MM),
    ),
  );
}

interface DeskStatusDescriptionInput {
  connected: boolean;
  childLock: boolean;
  activeSourceAllowed: boolean;
  controllerResetActive: boolean;
  heightKnown: boolean;
  heightMm: number | null;
  maxHeightMm: number;
  upwardBlocked: boolean;
  motion: DeskMotion | null;
}

/**
 * 解释当前控制状态，尤其说明上升为何被禁用，避免只留下一个没有原因的灰色按钮。
 */
export function describeDeskStatus({
  connected,
  childLock,
  activeSourceAllowed,
  controllerResetActive,
  heightKnown,
  heightMm,
  maxHeightMm,
  upwardBlocked,
  motion,
}: DeskStatusDescriptionInput): string {
  if (!connected) return '设备未连接';
  if (childLock) return '童锁已开启，所有控制入口均已锁定';
  if (!activeSourceAllowed) return '当前连接入口已被设备设置禁用';
  if (controllerResetActive) return '控制盒正在重置，请等待桌子恢复';
  if (!heightKnown || heightMm === null) {
    return '高度传感器暂不可用，已禁止上升';
  }
  if (upwardBlocked) {
    return heightMm >= maxHeightMm
      ? '已到最高安全高度，继续上升已被阻止'
      : '右侧有障碍或安全传感器不可用，继续上升已被阻止';
  }
  switch (motion) {
    case 'moving_up': return '正在上升，松手即停';
    case 'moving_down': return '正在下降，松手即停';
    case 'goto_preset': return '正在前往目标档位';
    case 'error': return '控制盒报告异常，请停止操作并检查设备';
    default: return '按住按钮升降，松手即停';
  }
}
