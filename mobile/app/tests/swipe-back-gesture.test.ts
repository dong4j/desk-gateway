/** 二级页面边缘右滑返回的纯手势判定测试。 */

import assert from 'node:assert/strict';
import test from 'node:test';

import {
  shouldActivateSwipeBack,
  shouldCompleteSwipeBack,
} from '../src/ui/swipeBackGesture';

test('only activates for a right swipe that starts at the left edge', () => {
  assert.equal(shouldActivateSwipeBack({
    startX: 18,
    dx: 24,
    dy: 4,
    vx: 0.2,
  }), true);
  assert.equal(shouldActivateSwipeBack({
    startX: 60,
    dx: 80,
    dy: 2,
    vx: 0.5,
  }), false);
  assert.equal(shouldActivateSwipeBack({
    startX: 18,
    dx: 12,
    dy: 30,
    vx: 0.2,
  }), false);
});

test('completes for enough distance or a deliberate fast swipe', () => {
  assert.equal(shouldCompleteSwipeBack({
    startX: 12,
    dx: 100,
    dy: 5,
    vx: 0.1,
  }), true);
  assert.equal(shouldCompleteSwipeBack({
    startX: 12,
    dx: 52,
    dy: 5,
    vx: 0.7,
  }), true);
  assert.equal(shouldCompleteSwipeBack({
    startX: 12,
    dx: 52,
    dy: 5,
    vx: 0.2,
  }), false);
});
