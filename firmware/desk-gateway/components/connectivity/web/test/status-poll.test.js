/**
 * @file status-poll.test.js
 * @brief Host regression: Web status poll must slow down while idle so
 *        ESP32 httpd sockets are not kept full after long unattended tabs.
 */
'use strict';

const assert = require('node:assert/strict');
const { intervalMs } = require('../www/status-poll.js');

function main() {
  assert.equal(intervalMs({}), 1000, 'unknown snapshot stays idle');
  assert.equal(intervalMs({ status: 'idle' }), 1000);
  assert.equal(intervalMs({ status: 'error' }), 1000);
  assert.equal(intervalMs({ status: 'moving_up' }), 250);
  assert.equal(intervalMs({ status: 'moving_down' }), 250);
  assert.equal(intervalMs({ status: 'goto_preset' }), 250);
  assert.equal(intervalMs({ status: 'idle', controller_reset_active: true }),
               250, 'controller reset needs the motion cadence');
  console.log('web status poll intervals: OK');
}

main();
