/**
 * @file height-presets.js
 * @brief Web 多高度档位输入校验与容量策略。
 */
(function exposeHeightPresets(root, factory) {
  const api = factory();
  if (typeof module !== 'undefined' && module.exports) {
    module.exports = api;
  } else {
    root.DeskHeightPresets = api;
  }
}(typeof globalThis !== 'undefined' ? globalThis : this, () => {
  'use strict';

  function normalizeName(value) {
    const name = String(value ?? '').trim();
    if (!name) throw new Error('请输入档位名称');
    if (/[\u0000-\u001f\u007f]/.test(name)) {
      throw new Error('档位名称不能包含控制字符');
    }
    if (new TextEncoder().encode(name).length > 48) {
      throw new Error('档位名称最多 48 个 UTF-8 字节');
    }
    return name;
  }

  function heightMmFromCm(value, minimumHeightMm = 550) {
    const heightMm = Math.round(Number(value) * 10);
    if (!Number.isInteger(heightMm) || heightMm < minimumHeightMm ||
        heightMm > 940) {
      throw new Error(
        `档位高度范围为 ${(minimumHeightMm / 10).toFixed(1)}–94.0 cm`);
    }
    return heightMm;
  }

  function canCreate(snapshot) {
    return !!snapshot && snapshot.custom_count < snapshot.custom_capacity;
  }

  return { normalizeName, heightMmFromCm, canCreate };
}));
