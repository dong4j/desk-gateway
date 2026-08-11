/** Desk Gateway GATT v1 纯协议单元测试。 */

import assert from 'node:assert/strict';
import test from 'node:test';

import { DeskCommand, encodeDeskCommand } from '../src/desk/commands';
import {
  DeskSystemCommand,
  decodeDeskConfig,
  decodeDeskState,
  decodeFirmwareRevision,
  encodeDeskConfigWrite,
  encodeDeskSystemCommand,
} from '../src/desk/protocol';

test('decodes known height and safety flags', () => {
  const state = decodeDeskState([
    0x01,
    0x01,
    0b0001_1001,
    0x00,
    0xde,
    0x03,
    0x4c,
    0x04,
  ]);

  assert.equal(state.motion, 'moving_up');
  assert.equal(state.heightKnown, true);
  assert.equal(state.heightMm, 990);
  assert.equal(state.maxHeightMm, 1100);
  assert.equal(state.bluetoothAllowed, true);
  assert.equal(state.upwardBlocked, true);
});

test('maps unknown height to null', () => {
  const state = decodeDeskState([
    0x01,
    0x00,
    0x00,
    0x00,
    0xff,
    0xff,
    0xfc,
    0x03,
  ]);

  assert.equal(state.heightKnown, false);
  assert.equal(state.heightMm, null);
  assert.equal(state.maxHeightMm, 1020);
});

test('rejects protocol drift instead of guessing', () => {
  assert.throws(
    () => decodeDeskState([0x02, 0x00, 0x00, 0x00, 0xff, 0xff, 0xfc, 0x03]),
    /Unsupported Desk Gateway protocol version/,
  );
  assert.throws(() => decodeDeskState([0x01, 0x00]), /state length/);
});

test('encodes frozen one-byte commands', () => {
  assert.deepEqual(encodeDeskCommand(DeskCommand.Stop), [0x00]);
  assert.deepEqual(encodeDeskCommand(DeskCommand.HoldUp), [0x01]);
  assert.deepEqual(encodeDeskCommand(DeskCommand.HoldDown), [0x02]);
  assert.deepEqual(encodeDeskCommand(DeskCommand.Preset1), [0x11]);
  assert.deepEqual(encodeDeskCommand(DeskCommand.Preset4), [0x14]);
});

test('decodes the firmware revision string', () => {
  const revision = 'Aug 11 2026 19:52:22 # 1b5e1e21';
  assert.equal(
    decodeFirmwareRevision(
      Array.from(revision, (character) => character.charCodeAt(0)),
    ),
    revision,
  );
  assert.throws(() => decodeFirmwareRevision([]), /revision length/);
  assert.throws(() => decodeFirmwareRevision([0x41, 0x0a]), /non-printable/);
});

test('decodes Config snapshots and encodes field-only writes', () => {
  const config = decodeDeskConfig([0x01, 0b0000_1110, 0x4c, 0x04]);
  assert.equal(config.childLock, false);
  assert.equal(config.restAllowed, true);
  assert.equal(config.bluetoothAllowed, true);
  assert.equal(config.panelAllowed, true);
  assert.equal(config.maxHeightMm, 1100);

  assert.deepEqual(encodeDeskConfigWrite('child_lock', true), [1, 1, 1, 0]);
  assert.deepEqual(encodeDeskConfigWrite('max_height_mm', 1020), [1, 5, 0xfc, 3]);
  assert.throws(
    () => encodeDeskConfigWrite('rest_allowed', 2),
    /boolean config value/,
  );
  assert.throws(() => decodeDeskConfig([1, 0]), /config length/);
});

test('keeps system management commands separate from motion commands', () => {
  assert.deepEqual(
    encodeDeskSystemCommand(DeskSystemCommand.Restart),
    [0x01],
  );
  assert.throws(() => encodeDeskSystemCommand(0x7f), /system command/);
});
