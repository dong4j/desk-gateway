/**
 * @file bond-management.js
 * @brief Web 配对设备区域的无 DOM 展示策略。
 */
(function exposeBondManagement(root, factory) {
  const api = factory();
  if (typeof module !== 'undefined' && module.exports) {
    module.exports = api;
  } else {
    root.DeskBondManagement = api;
  }
}(typeof globalThis !== 'undefined' ? globalThis : this, () => {
  'use strict';

  function statusText(device) {
    if (device.delete_state === 'pending') return '正在删除';
    if (device.delete_state === 'failed') return '删除失败';
    if (device.controlling) return '在线 · 控制中';
    return device.connected ? '在线' : '离线';
  }

  function shouldPollFrequently(snapshot) {
    return !!snapshot?.pairing_window?.open ||
      !!snapshot?.devices?.some((device) => device.delete_state === 'pending');
  }

  function hasDeleteConflict(snapshot) {
    return !!snapshot?.devices?.some((device) =>
      device.delete_state === 'pending' || device.delete_state === 'failed');
  }

  function normalizeAlias(value) {
    const alias = String(value ?? '').trim();
    if (/[\u0000-\u001f\u007f]/.test(alias)) {
      throw new Error('别名不能包含控制字符');
    }
    if (new TextEncoder().encode(alias).length > 48) {
      throw new Error('别名最多 48 个 UTF-8 字节');
    }
    return alias;
  }

  return { statusText, shouldPollFrequently, hasDeleteConflict,
    normalizeAlias };
}));
