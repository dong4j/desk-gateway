/** App 多高度档位输入校验测试。 */

import assert from 'node:assert/strict';
import test from 'node:test';

import {
  heightPresetErrorMessage,
  heightPresetMmFromCm,
  normalizeHeightPresetName,
} from '../src/desk/HeightPresets';

test('normalizes custom height preset names and centimetres', () => {
  assert.equal(normalizeHeightPresetName('  午休  '), '午休');
  assert.equal(heightPresetMmFromCm('55'), 550);
  assert.throws(() => heightPresetMmFromCm('54.9'), /55.0/);
  assert.equal(heightPresetMmFromCm('72.5'), 725);
  assert.throws(() => normalizeHeightPresetName('   '), /名称/);
  assert.throws(() => normalizeHeightPresetName('坏\n名称'), /控制字符/);
  assert.throws(() => heightPresetMmFromCm('95'), /55.0/);
  assert.throws(() => heightPresetMmFromCm('59.9', 600), /60.0/);
});

test('maps stable height preset API errors', () => {
  assert.equal(heightPresetErrorMessage(new Error('preset_capacity_full')),
    '自定义档位已达到上限');
  assert.equal(heightPresetErrorMessage(new Error('preset_not_deletable')),
    '内置档位不能删除');
});
