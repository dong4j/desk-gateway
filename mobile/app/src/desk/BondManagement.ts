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

/** pending/failed 都要求用户逐台处理，不能再发起批量删除。 */
export function hasBondDeleteConflict(
  snapshot: DeskBondSnapshot | null,
): boolean {
  return snapshot?.devices.some(
    (device) => device.delete_state !== 'idle',
  ) ?? false;
}

/** 容量已满时只允许关闭已打开的窗口，不能再次开放新配对。 */
export function isBondPairingCapacityBlocked(
  snapshot: DeskBondSnapshot | null,
): boolean {
  return snapshot !== null &&
    !snapshot.pairing_window.open &&
    snapshot.devices.length >= snapshot.capacity;
}

export function bondErrorMessage(error: unknown): string {
  const detail = error instanceof Error ? error.message : String(error);
  if (detail.includes('delete_conflict')) {
    return '存在删除失败或进行中的设备，请先逐台处理';
  }
  if (detail.includes('bond_not_found')) {
    return '设备已被删除，列表已刷新';
  }
  if (detail.includes('invalid_alias')) {
    return '设备别名无效，请修改后重试';
  }
  if (detail.includes('unauthorized')) {
    return 'REST 认证失效，请重新保存局域网管理密码';
  }
  return `操作失败：${detail}`;
}

/** 固件按 UTF-8 字节持久化别名；这里提前给出与设备一致的错误。 */
export function normalizeBondAlias(value: string): string {
  const alias = value.trim();
  if (/[\u0000-\u001f\u007f]/.test(alias)) {
    throw new Error('别名不能包含控制字符');
  }
  let bytes = 0;
  for (const character of alias) {
    const codePoint = character.codePointAt(0)!;
    bytes += codePoint <= 0x7f
      ? 1
      : codePoint <= 0x7ff
        ? 2
        : codePoint <= 0xffff
          ? 3
          : 4;
  }
  if (bytes > 48) {
    throw new Error('别名最多 48 个 UTF-8 字节');
  }
  return alias;
}

/**
 * 连接恢复先清除网关端旧 Bond，再开放配对窗口。只有删除成功后才允许
 * 新配对，避免旧密钥仍在时继续引导用户重连。
 */
export async function recoverBluetoothConnection(
  deleteBond: () => Promise<void>,
  openPairingWindow: () => Promise<void>,
): Promise<void> {
  await deleteBond();
  await openPairingWindow();
}
