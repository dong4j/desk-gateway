/**
 * @file hold-control.test.js
 * @brief Host regression test for browser press-and-hold REST renewal.
 */
'use strict';

const assert = require('node:assert/strict');
const { bindHold } = require('../www/hold-control.js');

const delay = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

function fakeButton() {
  const listeners = new Map();
  const classes = new Set();
  return {
    disabled: false,
    listeners,
    classList: {
      add: (name) => classes.add(name),
      remove: (name) => classes.delete(name),
    },
    addEventListener: (name, handler) => listeners.set(name, handler),
    setPointerCapture: () => undefined,
    hasPointerCapture: () => true,
    releasePointerCapture: () => undefined,
  };
}

async function main() {
  await testRenewalWhileHeld();
  await testStopWaitsForInFlightRenewal();
  console.log('web hold renewal vectors: OK');
}

async function testRenewalWhileHeld() {
  const button = fakeButton();
  const commands = [];
  bindHold({
    button,
    startPath: '/api/v1/desk/up',
    send: async (path) => commands.push(path),
    onStartError: (error) => { throw error; },
    onStopError: (error) => { throw error; },
    renewIntervalMs: 10,
  });

  const event = { pointerId: 1, preventDefault: () => undefined };
  await button.listeners.get('pointerdown')(event);
  await delay(35);
  assert(commands.filter((path) => path.endsWith('/up')).length >= 3);

  await button.listeners.get('pointerup')(event);
  const countAfterStop = commands.length;
  assert.equal(commands.at(-1), '/api/v1/desk/stop');
  await delay(25);
  assert.equal(commands.length, countAfterStop,
               'renewal must stop immediately after pointer release');
}

async function testStopWaitsForInFlightRenewal() {
  const button = fakeButton();
  const commands = [];
  let releaseRenewal;
  const renewalGate = new Promise((resolve) => { releaseRenewal = resolve; });
  let upCount = 0;
  bindHold({
    button,
    startPath: '/api/v1/desk/up',
    send: async (path) => {
      commands.push(path);
      if (path.endsWith('/up') && ++upCount === 2) {
        await renewalGate;
      }
    },
    onStartError: (error) => { throw error; },
    onStopError: (error) => { throw error; },
    renewIntervalMs: 10,
  });

  const event = { pointerId: 2, preventDefault: () => undefined };
  await button.listeners.get('pointerdown')(event);
  await waitUntil(() => upCount === 2);

  const stopPromise = button.listeners.get('pointerup')(event);
  await delay(0);
  assert.notEqual(commands.at(-1), '/api/v1/desk/stop',
                  'STOP must wait behind an already-started renewal');
  releaseRenewal();
  await stopPromise;
  assert.equal(commands.at(-1), '/api/v1/desk/stop',
               'STOP must be the final command after pointer release');

  const countAfterStop = commands.length;
  await delay(25);
  assert.equal(commands.length, countAfterStop,
               'no renewal may be queued after STOP');
}

async function waitUntil(predicate) {
  for (let attempt = 0; attempt < 50; attempt += 1) {
    if (predicate()) return;
    await delay(2);
  }
  throw new Error('timed out waiting for delayed renewal');
}

void main();
