/** 首页高度标尺和安全状态文案必须使用当前 TOF 产品口径。 */

import assert from 'node:assert/strict';
import test from 'node:test';

import {
  describeDeskStatus,
  normalizeDeskHeight,
} from '../src/desk/heightPresentation';

test('maps the fixed 550–940 mm product range', () => {
  assert.equal(normalizeDeskHeight(550), 0);
  assert.equal(normalizeDeskHeight(745), 0.5);
  assert.equal(normalizeDeskHeight(940), 1);
  assert.equal(normalizeDeskHeight(1_200), 1);
});

test('explains maximum height and other upward safety blocks', () => {
  const common = {
    connected: true,
    childLock: false,
    activeSourceAllowed: true,
    controllerResetActive: false,
    heightKnown: true,
    maxHeightMm: 900,
    upwardBlocked: true,
    motion: 'idle' as const,
  };

  assert.match(describeDeskStatus({ ...common, heightMm: 900 }), /最高安全高度/);
  assert.match(describeDeskStatus({ ...common, heightMm: 700 }), /右侧有障碍/);
  assert.match(describeDeskStatus({
    ...common,
    heightKnown: false,
    heightMm: null,
  }), /高度传感器暂不可用/);
});
