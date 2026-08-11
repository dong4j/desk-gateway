/** 按住升降震动节奏的生命周期测试。 */

import assert from 'node:assert/strict';
import test from 'node:test';

import {
  HoldHapticController,
  type HoldHapticEvent,
} from '../src/ui/HoldHapticController';

test('emits start, repeated pulses, and end while held', async () => {
  const events: HoldHapticEvent[] = [];
  const controller = new HoldHapticController((event) => events.push(event), 10);

  controller.start();
  await delay(35);
  controller.stop();

  assert.equal(events[0], 'start');
  assert.ok(events.filter((event) => event === 'pulse').length >= 2);
  assert.equal(events.at(-1), 'end');
});

test('cancel stops pulses without emitting an end feedback', async () => {
  const events: HoldHapticEvent[] = [];
  const controller = new HoldHapticController((event) => events.push(event), 10);

  controller.start();
  await delay(15);
  controller.cancel();
  const eventCountAfterCancel = events.length;
  await delay(25);

  assert.equal(events.length, eventCountAfterCancel);
  assert.notEqual(events.at(-1), 'end');
});

function delay(milliseconds: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}
