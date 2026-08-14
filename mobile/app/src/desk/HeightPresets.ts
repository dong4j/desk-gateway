/** App 多高度档位的输入校验与错误文案，保持为纯函数便于主机测试。 */

export function normalizeHeightPresetName(value: string): string {
  const name = value.trim();
  if (!name) {
    throw new Error('请输入档位名称');
  }
  if (/[\u0000-\u001f\u007f]/.test(name)) {
    throw new Error('档位名称不能包含控制字符');
  }
  let bytes = 0;
  for (const character of name) {
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
    throw new Error('档位名称最多 48 个 UTF-8 字节');
  }
  return name;
}

export function heightPresetMmFromCm(value: string): number {
  const heightMm = Math.round(Number(value) * 10);
  if (!Number.isInteger(heightMm) || heightMm < 560 || heightMm > 940) {
    throw new Error('档位高度范围为 56.0–94.0 cm');
  }
  return heightMm;
}

export function heightPresetErrorMessage(error: unknown): string {
  const detail = error instanceof Error ? error.message : String(error);
  if (detail.includes('preset_capacity_full')) return '自定义档位已达到上限';
  if (detail.includes('preset_not_deletable')) return '内置档位不能删除';
  if (detail.includes('preset_not_found')) return '档位已不存在，列表已刷新';
  if (detail.includes('invalid_preset')) return '档位名称或高度无效';
  return `操作失败：${detail}`;
}
