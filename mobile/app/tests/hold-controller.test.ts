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

test('queues STOP after an in-flight renewal', async () => {
  const sent: DeskCommandValue[] = [];
  let releaseRenewal: (() => void) | undefined;
  const renewalGate = new Promise<void>((resolve) => {
    releaseRenewal = resolve;
  });
  let upCount = 0;
  const controller = new DeskHoldController(async (command) => {
    sent.push(command);
    if (command === DeskCommand.HoldUp && ++upCount === 2) {
      await renewalGate;
    }
  }, 10);

  await controller.start(DeskCommand.HoldUp);
  await waitUntil(() => upCount === 2);

  const stopPromise = controller.stop();
  await delay(0);
  assert.notEqual(sent.at(-1), DeskCommand.Stop);
  releaseRenewal?.();
  await stopPromise;

  assert.equal(sent.at(-1), DeskCommand.Stop);
  const countAfterStop = sent.length;
  await delay(25);
  assert.equal(sent.length, countAfterStop);
});

test('rejects preset commands as HOLD input', async () => {
  const controller = new DeskHoldController(async () => undefined);
  const invalidCommand = DeskCommand.Preset1 as typeof DeskCommand.HoldUp;
  await assert.rejects(
    controller.start(invalidCommand),
    /only accepts HOLD commands/,
  );
});

test('ignores a stale release from the previous direction', async () => {
  const sent: DeskCommandValue[] = [];
  const controller = new DeskHoldController(async (command) => {
    sent.push(command);
  }, 1_000);

  await controller.start(DeskCommand.HoldUp);
  await controller.start(DeskCommand.HoldDown);
  await controller.stop(DeskCommand.HoldUp);

  assert.equal(sent.at(-1), DeskCommand.HoldDown);
  await controller.stop(DeskCommand.HoldDown);
  assert.equal(sent.at(-1), DeskCommand.Stop);
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

async function waitUntil(predicate: () => boolean): Promise<void> {
  for (let attempt = 0; attempt < 50; attempt += 1) {
    if (predicate()) return;
    await delay(2);
  }
  throw new Error('timed out waiting for delayed renewal');
}
