/**
 * @file bond-management.test.js
 * @brief Web 配对设备轮询与状态文案主机测试。
 */
'use strict';

const assert = require('node:assert/strict');
const policy = require('../www/bond-management.js');

assert.equal(policy.statusText({ connected: false, controlling: false,
  delete_state: 'idle' }), '离线');
assert.equal(policy.statusText({ connected: true, controlling: true,
  delete_state: 'idle' }), '在线 · 控制中');
assert.equal(policy.statusText({ delete_state: 'pending' }), '正在删除');
assert.equal(policy.statusText({ delete_state: 'failed' }), '删除失败');

assert.equal(policy.shouldPollFrequently({
  pairing_window: { open: true }, devices: [],
}), true);
assert.equal(policy.shouldPollFrequently({
  pairing_window: { open: false },
  devices: [{ delete_state: 'pending' }],
}), true);
assert.equal(policy.shouldPollFrequently({
  pairing_window: { open: false },
  devices: [{ delete_state: 'idle' }],
}), false);
assert.equal(policy.hasDeleteConflict({
  devices: [{ delete_state: 'failed' }],
}), true);
assert.equal(policy.normalizeAlias('  书房 iPhone  '), '书房 iPhone');
assert.equal(policy.normalizeAlias('   '), '');
assert.throws(() => policy.normalizeAlias('a'.repeat(49)), /48/);
assert.throws(() => policy.normalizeAlias('坏\n名称'), /控制字符/);

console.log('web bond management vectors: OK');
