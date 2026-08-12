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

  console.log('web hold renewal vectors: OK');
}

void main();
