/** 手机设置页的 Bond 状态文案与轮询策略，保持为纯函数便于主机单测。 */

import type { DeskBondDevice, DeskBondSnapshot } from './DeskRestClient';

export function isBondManagementConfigured(host: string, restKey: string): boolean {
  return host.trim() !== '' && restKey !== '';
}

export function bondStatusText(device: DeskBondDevice): string {
  if (device.delete_state === 'pending') {
    return '正在删除';
  }
  if (device.delete_state === 'failed') {
    return '删除失败';
  }
  if (device.controlling) {
    return '在线 · 控制中';
  }
  return device.connected ? '在线' : '离线';
}

export function bondPollIntervalMs(snapshot: DeskBondSnapshot): number {
  const deleting = snapshot.devices.some(
    (device) => device.delete_state === 'pending',
  );
  return snapshot.pairing_window.open || deleting ? 1_000 : 5_000;
}

export function bondErrorMessage(error: unknown): string {
  const detail = error instanceof Error ? error.message : String(error);
  if (detail.includes('delete_conflict')) {
    return '存在删除失败或进行中的设备，请先逐台处理';
  }
  if (detail.includes('bond_not_found')) {
    return '设备已被删除，列表已刷新';
  }
  if (detail.includes('unauthorized')) {
    return 'REST 认证失效，请重新保存局域网管理密码';
  }
  return `操作失败：${detail}`;
}
