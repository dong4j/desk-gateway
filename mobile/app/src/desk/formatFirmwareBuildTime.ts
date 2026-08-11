/**
 * 固件构建时间的移动端显示格式化。
 *
 * ESP-IDF 当前通过 Device Information Service 返回英文编译时间和短哈希。本模块只
 * 改变 UI 展示，不改变 BLE 协议中的原始值，因而仍兼容旧固件和其他调试客户端。
 */

const MONTHS: Record<string, string> = {
  Jan: '01',
  Feb: '02',
  Mar: '03',
  Apr: '04',
  May: '05',
  Jun: '06',
  Jul: '07',
  Aug: '08',
  Sep: '09',
  Oct: '10',
  Nov: '11',
  Dec: '12',
};

/** 将 `Aug 11 2026 19:52:22 # hash` 转为 `2026.08.11 19:52`。 */
export function formatFirmwareBuildTime(
  revision: string | null,
): string | null {
  if (revision === null) {
    return null;
  }

  const match = revision.match(
    /^([A-Z][a-z]{2})\s+(\d{1,2})\s+(\d{4})\s+(\d{2}):(\d{2}):\d{2}(?:\s+#.*)?$/,
  );
  if (!match) {
    return revision;
  }

  const [, monthName, day, year, hour, minute] = match;
  const month = MONTHS[monthName];
  if (!month) {
    return revision;
  }
  return `${year}.${month}.${day.padStart(2, '0')} ${hour}:${minute}`;
}
