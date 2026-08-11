/**
 * 固件构建时间的移动端显示格式化。
 *
 * ESP-IDF 通过 Device Information Service 返回英文编译时间和 Git 派生版本。本模块
 * 只改变 UI 展示，并兼容仍携带 ELF 短标识的旧固件。
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

/** 将固件原始版本转换为精确到分钟、附带 Git 版本的移动端文案。 */
export function formatFirmwareBuildTime(
  revision: string | null,
): string | null {
  if (revision === null) {
    return null;
  }

  const match = revision.match(
    /^([A-Z][a-z]{2})\s+(\d{1,2})\s+(\d{4})\s+(\d{2}):(\d{2}):\d{2}(?:\s+@\s+(\S+)|\s+#.*)?$/,
  );
  if (!match) {
    return revision;
  }

  const [, monthName, day, year, hour, minute, gitVersion] = match;
  const month = MONTHS[monthName];
  if (!month) {
    return revision;
  }
  const buildTime = `${year}.${month}.${day.padStart(2, '0')} ${hour}:${minute}`;
  return gitVersion ? `${buildTime} · ${gitVersion}` : buildTime;
}
