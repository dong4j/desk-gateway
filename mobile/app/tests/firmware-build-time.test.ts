/** 固件构建时间展示格式测试。 */

import assert from 'node:assert/strict';
import test from 'node:test';

import { formatFirmwareBuildTime } from '../src/desk/formatFirmwareBuildTime';

test('formats ESP-IDF firmware revision to minute precision', () => {
  assert.equal(
    formatFirmwareBuildTime('Aug 11 2026 19:52:22 # 1b5e1e21'),
    '2026.08.11 19:52',
  );
});

test('keeps the Git-derived firmware version visible', () => {
  assert.equal(
    formatFirmwareBuildTime('Aug 11 2026 19:52:22 @ fc310ab-dirty'),
    '2026.08.11 19:52 · fc310ab-dirty',
  );
});

test('keeps unknown firmware revision formats intact', () => {
  assert.equal(formatFirmwareBuildTime('development-build'), 'development-build');
  assert.equal(formatFirmwareBuildTime(null), null);
});
