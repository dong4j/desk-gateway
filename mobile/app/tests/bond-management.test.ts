/** 手机 Bond 管理状态文案、轮询与错误映射测试。 */

import assert from 'node:assert/strict';
import test from 'node:test';

import {
  bondErrorMessage,
  bondPollIntervalMs,
  bondStatusText,
  hasBondDeleteConflict,
  isBondManagementConfigured,
  isBondPairingCapacityBlocked,
  normalizeBondAlias,
  recoverBluetoothConnection,
} from '../src/desk/BondManagement';
import type { DeskBondSnapshot } from '../src/desk/DeskRestClient';

const snapshot: DeskBondSnapshot = {
  devices: [{
    id: 'bond_73c98f21a1b2',
    kind: 'watchos',
    label: 'Apple Watch · A1B2',
    alias: '',
    connected: true,
    controlling: true,
    delete_state: 'idle',
    delete_error: null,
  }],
  capacity: 3,
  pairing_window: { open: false, remaining_seconds: 0 },
  auto_child_lock: {
    enabled: false,
    device_id: '',
    detector_online: false,
  },
};

test('shows connected, controlling and deletion states', () => {
  assert.equal(bondStatusText(snapshot.devices[0]), '在线 · 控制中');
  assert.equal(bondStatusText({
    ...snapshot.devices[0],
    delete_state: 'pending',
  }), '正在删除');
  assert.equal(bondStatusText({
    ...snapshot.devices[0],
    delete_state: 'failed',
  }), '删除失败');
});

test('polls quickly only while pairing or asynchronous deletion is active', () => {
  assert.equal(bondPollIntervalMs(snapshot), 5_000);
  assert.equal(bondPollIntervalMs({
    ...snapshot,
    pairing_window: { open: true, remaining_seconds: 120 },
  }), 1_000);
  assert.equal(bondPollIntervalMs({
    ...snapshot,
    devices: [{ ...snapshot.devices[0], delete_state: 'pending' }],
  }), 1_000);
});

test('maps stable firmware management errors to actionable copy', () => {
  assert.equal(
    bondErrorMessage(new Error('delete_conflict')),
    '存在删除失败或进行中的设备，请先逐台处理',
  );
  assert.equal(
    bondErrorMessage(new Error('unauthorized')),
    'REST 认证失效，请重新保存局域网管理密码',
  );
});

test('blocks batch deletion while a device is pending or failed', () => {
  assert.equal(hasBondDeleteConflict(snapshot), false);
  assert.equal(hasBondDeleteConflict({
    ...snapshot,
    devices: [{ ...snapshot.devices[0], delete_state: 'pending' }],
  }), true);
  assert.equal(hasBondDeleteConflict({
    ...snapshot,
    devices: [{ ...snapshot.devices[0], delete_state: 'failed' }],
  }), true);
});

test('blocks opening a pairing window only when capacity is full', () => {
  const full = {
    ...snapshot,
    capacity: 1,
  };
  assert.equal(isBondPairingCapacityBlocked(full), true);
  assert.equal(isBondPairingCapacityBlocked({
    ...full,
    pairing_window: { open: true, remaining_seconds: 120 },
  }), false);
  assert.equal(isBondPairingCapacityBlocked(snapshot), false);
});

test('requires both a gateway address and REST password', () => {
  assert.equal(isBondManagementConfigured('desk-gateway.local', 'secret'), true);
  assert.equal(isBondManagementConfigured('  ', 'secret'), false);
  assert.equal(isBondManagementConfigured('desk-gateway.local', ''), false);
});

test('normalizes aliases with the firmware UTF-8 byte limit', () => {
  assert.equal(normalizeBondAlias('  书房手表  '), '书房手表');
  assert.equal(normalizeBondAlias('   '), '');
  assert.throws(() => normalizeBondAlias('a'.repeat(49)), /48/);
  assert.throws(() => normalizeBondAlias('坏\n名称'), /控制字符/);
});

test('clears the selected bond before opening the recovery window', async () => {
  const calls: string[] = [];
  await recoverBluetoothConnection(
    async () => { calls.push('delete'); },
    async () => { calls.push('open'); },
  );
  assert.deepEqual(calls, ['delete', 'open']);
});

test('does not open a recovery window when deleting the old bond fails', async () => {
  let pairingWindowOpened = false;
  await assert.rejects(recoverBluetoothConnection(
    async () => { throw new Error('delete failed'); },
    async () => { pairingWindowOpened = true; },
  ), /delete failed/);
  assert.equal(pairingWindowOpened, false);
});
