/**
 * @file hold-control.js
 * @brief Browser-side lease renewal for press-and-hold desk controls.
 *
 * A single REST command is bounded by the firmware motion timeout. While the
 * pointer remains down, this helper renews the same command so a genuine long
 * press does not stop after 15 seconds. Releasing or losing the pointer always
 * clears renewal before sending STOP.
 */
(function exposeHoldControl(root, factory) {
  const api = factory();
  if (typeof module !== 'undefined' && module.exports) {
    module.exports = api;
  } else {
    root.DeskHoldControl = api;
  }
}(typeof globalThis !== 'undefined' ? globalThis : this, () => {
  'use strict';

  const DEFAULT_RENEW_INTERVAL_MS = 1000;

  function bindHold({
    button,
    startPath,
    send,
    onStartError,
    onStopError,
    renewIntervalMs = DEFAULT_RENEW_INTERVAL_MS,
  }) {
    let holding = false;
    let renewTimer = null;
    let writeInFlight = false;

    const clearRenewTimer = () => {
      if (renewTimer !== null) {
        clearInterval(renewTimer);
        renewTimer = null;
      }
    };

    const renew = async () => {
      if (!holding || writeInFlight) return;
      writeInFlight = true;
      try {
        await send(startPath);
      } catch (error) {
        holding = false;
        clearRenewTimer();
        button.classList.remove('active');
        onStartError(error);
      } finally {
        writeInFlight = false;
      }
    };

    const start = async (event) => {
      event.preventDefault();
      if (holding || button.disabled) return;
      holding = true;
      try {
        button.setPointerCapture(event.pointerId);
      } catch (_) { /* Pointer capture is optional on older WebViews. */ }
      button.classList.add('active');
      await renew();
      if (holding && renewTimer === null) {
        renewTimer = setInterval(() => void renew(), renewIntervalMs);
      }
    };

    const end = async (event) => {
      event.preventDefault();
      if (!holding) return;
      holding = false;
      clearRenewTimer();
      button.classList.remove('active');
      try {
        if (button.hasPointerCapture?.(event.pointerId)) {
          button.releasePointerCapture(event.pointerId);
        }
      } catch (_) { /* Pointer capture may already have been released. */ }
      try {
        await send('/api/v1/desk/stop');
      } catch (error) {
        onStopError(error);
      }
    };

    button.addEventListener('pointerdown', start);
    button.addEventListener('pointerup', end);
    button.addEventListener('pointercancel', end);
    button.addEventListener('lostpointercapture', (event) => {
      if (holding) void end(event);
    });
    button.addEventListener('contextmenu', (event) => event.preventDefault());
  }

  return { bindHold, DEFAULT_RENEW_INTERVAL_MS };
}));
