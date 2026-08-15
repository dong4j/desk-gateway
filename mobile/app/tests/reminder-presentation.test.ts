/** 番茄页面动作和时间文案的纯函数测试。 */

import assert from 'node:assert/strict';
import test from 'node:test';

import {
  formatRemaining,
  reminderPrimaryAction,
} from '../src/desk/reminderPresentation';
import type { ReminderSnapshot } from '../src/desk/types';

const base: ReminderSnapshot = {
  protocolVersion: 1,
  available: true,
  state: 'idle',
  phase: 'focus',
  alarmReason: 'none',
  remainingSec: 1500,
  completedFocusCount: 0,
  config: {
    focusMinutes: 25,
    shortBreakMinutes: 5,
    longBreakMinutes: 15,
    focusesPerLongBreak: 4,
  },
  lastError: null,
};

test('maps ESP states to the next legal primary action', () => {
  assert.equal(reminderPrimaryAction(base).action, 'start_focus');
  assert.equal(reminderPrimaryAction({ ...base, state: 'running' }).action, 'pause');
  assert.equal(reminderPrimaryAction({ ...base, state: 'paused' }).action, 'resume');
  assert.equal(reminderPrimaryAction({
    ...base,
    state: 'waiting',
    phase: 'short_break',
  }).action, 'start_break');
  assert.equal(reminderPrimaryAction({ ...base, state: 'snoozed' }).action, 'stop');
});

test('formats the device remaining seconds without local countdown state', () => {
  assert.equal(formatRemaining(1499), '24:59');
  assert.equal(formatRemaining(-1), '00:00');
});
