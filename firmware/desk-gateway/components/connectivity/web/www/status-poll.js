/**
 * @file status-poll.js
 * @brief Choose Web status-poll cadence from the last snapshot.
 *
 * Idle tabs used to hit /api/v1/desk/status every 250 ms forever. Combined with
 * MQTT and a 10-socket LWIP budget that filled httpd until accept(23). Motion
 * still needs 250 ms for the height animation; idle and errors drop to 1 s.
 */
(function exposeStatusPoll(root, factory) {
  const api = factory();
  if (typeof module !== 'undefined' && module.exports) {
    module.exports = api;
  } else {
    root.DeskStatusPoll = api;
  }
}(typeof globalThis !== 'undefined' ? globalThis : this, () => {
  'use strict';

  const MOTION_POLL_MS = 250;
  const IDLE_POLL_MS = 1000;

  function isMovingStatus(status) {
    return status === 'moving_up' ||
           status === 'moving_down' ||
           status === 'goto_preset';
  }

  /**
   * @param {object} [snapshot]
   * @returns {number} delay before the next GET /status
   */
  function intervalMs(snapshot) {
    if (snapshot && snapshot.controller_reset_active) {
      return MOTION_POLL_MS;
    }
    if (snapshot && isMovingStatus(snapshot.status)) {
      return MOTION_POLL_MS;
    }
    return IDLE_POLL_MS;
  }

  return {
    MOTION_POLL_MS,
    IDLE_POLL_MS,
    intervalMs,
  };
}));
