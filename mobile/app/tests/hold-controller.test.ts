/** HOLD 续期与松手停止的最小行为测试。 */

import assert from 'node:assert/strict';
import test from 'node:test';

import { DeskCommand, type DeskCommandValue } from '../src/desk/commands';
import { DeskHoldController } from '../src/desk/DeskHoldController';

test('renews while active and ends with STOP', async () => {
  const sent: DeskCommandValue[] = [];
  const controller = new DeskHoldController(async (command) => {
    sent.push(command);
  }, 10);

  await controller.start(DeskCommand.HoldUp);
  await delay(35);
  await controller.stop();

  assert.equal(sent[0], DeskCommand.HoldUp);
  assert.ok(sent.filter((command) => command === DeskCommand.HoldUp).length >= 2);
  assert.equal(sent.at(-1), DeskCommand.Stop);
});

test('rejects preset commands as HOLD input', async () => {
  const controller = new DeskHoldController(async () => undefined);
  await assert.rejects(
    controller.start(DeskCommand.Preset1),
    /only accepts HOLD commands/,
  );
});

test('does not start a renewal timer after the first write fails', async () => {
  let attempts = 0;
  const controller = new DeskHoldController(async () => {
    attempts += 1;
    throw new Error('write failed');
  }, 10);

  await assert.rejects(
    controller.start(DeskCommand.HoldDown),
    /write failed/,
  );
  await delay(30);

  assert.equal(attempts, 1);
});

function delay(milliseconds: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}
