/** Desk Gateway GATT v1 纯协议单元测试。 */

import assert from 'node:assert/strict';
import test from 'node:test';

import { DeskCommand, encodeDeskCommand } from '../src/desk/commands';
import {
  DeskSystemCommand,
  decodeDeskConfig,
  decodeDeskState,
  decodeFirmwareRevision,
  decodeReminder,
  encodeDeskConfigWrite,
  encodeDeskPresence,
  encodeDeskSystemCommand,
  encodeReminderAction,
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
  assert.equal(state.controllerResetSupported, false);
  assert.equal(state.controllerResetRecommended, false);
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
  assert.equal(config.minHeightMm, 550);
  assert.equal(config.maxHeightMm, 1100);
  assert.equal(config.preset1HeightMm, 640);
  assert.equal(config.preset4HeightMm, 1020);

  const configV2 = decodeDeskConfig([
    0x02, 0b0000_1110, 0x4c, 0x04, 0x8a, 0x02, 0x1a, 0x04,
  ]);
  assert.equal(configV2.preset1HeightMm, 650);
  assert.equal(configV2.preset4HeightMm, 1050);
  assert.equal(configV2.minHeightMm, 550);

  const configV3 = decodeDeskConfig([
    0x03, 0b0000_1110, 0xac, 0x03, 0x26, 0x02, 0x66, 0x03, 0x26, 0x02,
  ]);
  assert.equal(configV3.minHeightMm, 550);
  assert.equal(configV3.maxHeightMm, 940);
  assert.equal(configV3.preset1HeightMm, 550);
  assert.equal(configV3.preset4HeightMm, 870);

  assert.deepEqual(encodeDeskConfigWrite('child_lock', true), [3, 1, 1, 0]);
  assert.deepEqual(encodeDeskConfigWrite('max_height_mm', 1020), [3, 5, 0xfc, 3]);
  assert.deepEqual(encodeDeskConfigWrite('preset1_height_mm', 650), [3, 6, 0x8a, 2]);
  assert.deepEqual(encodeDeskConfigWrite('min_height_mm', 550), [3, 8, 0x26, 2]);
  assert.deepEqual(
    encodeDeskConfigWrite('max_height_mm', 940, 2),
    [2, 5, 0xac, 3],
  );
  assert.throws(
    () => encodeDeskConfigWrite('min_height_mm', 550, 2),
    /minimum preset height/,
  );
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
  assert.deepEqual(
    encodeDeskSystemCommand(DeskSystemCommand.ResetController),
    [0x02],
  );
  assert.throws(() => encodeDeskSystemCommand(0x7f), /system command/);
});

test('encodes the selected Bond ID as a fixed presence packet', () => {
  assert.deepEqual(
    encodeDeskPresence('bond_001122aabbcc'),
    [0x01, ...Array.from('bond_001122aabbcc', (value) => value.charCodeAt(0))],
  );
  assert.throws(
    () => encodeDeskPresence('bond_001122AABBCC'),
    /device ID/,
  );
});

test('decodes Reminder v1 without creating a mobile timer', () => {
  const decoded = decodeReminder([
    0x01, 0x01, 0x00, 0x00, 0x17, 72, 25, 5, 15, 4,
    0xdb, 0x05, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 12, 0x00,
  ]);

  assert.equal(decoded.reminder.state, 'running');
  assert.equal(decoded.reminder.phase, 'focus');
  assert.equal(decoded.reminder.remainingSec, 1499);
  assert.equal(decoded.reminder.completedFocusCount, 7);
  assert.equal(decoded.reminder.autoCycle, true);
  assert.equal(decoded.reminder.autoAdvanceSec, 12);
  assert.equal(decoded.audio.enabled, true);
  assert.equal(decoded.audio.volumePercent, 72);
  assert.deepEqual(encodeReminderAction('pause'), [0x02]);
  assert.deepEqual(encodeReminderAction('start_auto'), [0x07]);
  assert.throws(() => decodeReminder([0x01]), /reminder length/);
});
