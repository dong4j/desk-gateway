/** @file height-presets.test.js @brief Web 多高度档位输入策略测试。 */
'use strict';

const assert = require('node:assert/strict');
const presets = require('../www/height-presets.js');

assert.equal(presets.normalizeName('  午休  '), '午休');
assert.throws(() => presets.normalizeName('   '), /名称/);
assert.throws(() => presets.normalizeName('坏\n名称'), /控制字符/);
assert.equal(presets.heightMmFromCm('55'), 550);
assert.throws(() => presets.heightMmFromCm('54.9'), /55.0/);
assert.equal(presets.heightMmFromCm('72.5'), 725);
assert.throws(() => presets.heightMmFromCm('95'), /55.0/);
assert.throws(() => presets.heightMmFromCm('59.9', 600), /60.0/);
assert.equal(presets.canCreate({ custom_count: 2, custom_capacity: 16 }), true);
assert.equal(presets.canCreate({ custom_count: 16, custom_capacity: 16 }), false);

console.log('web height preset vectors: OK');
