/**
 * Demo 共用假数据：按住升/降改高度，松手停止。
 * 竖条（默认）：改 height；仅当父级是矮横轨(h-1.5/h-2)时改 width。
 */
(() => {
  let mm = 950;
  let dir = 0;
  const min = 700;
  const max = 1200;

  const heightEl = document.getElementById('height');
  const stateEl = document.getElementById('state');
  const hintEl = document.getElementById('hint');
  const rail = document.getElementById('rail');
  const top = document.getElementById('top');
  if (hintEl) {
    hintEl.dataset.idle = hintEl.textContent;
  }

  function paint() {
    const cm = (mm / 10).toFixed(1);
    if (heightEl) {
      const span = heightEl.querySelector('span');
      if (span) {
        heightEl.replaceChildren(document.createTextNode(cm + ' '), span);
      } else {
        heightEl.textContent = cm;
      }
    }

    const t = Math.min(1, Math.max(0, (mm - min) / (max - min)));
    if (rail) {
      const parent = rail.parentElement;
      const cls = parent ? parent.className : '';
      const horizontal = /\bh-1\.5\b|\bh-2\b/.test(cls);
      if (horizontal) {
        rail.style.width = `${(t * 100).toFixed(1)}%`;
        rail.style.height = '100%';
      } else {
        rail.style.height = `${(12 + t * 76).toFixed(1)}%`;
        rail.style.width = '100%';
      }
    }
    if (top) {
      top.setAttribute('y', String(58 - t * 28));
    }

    const st = dir > 0 ? 'moving_up' : dir < 0 ? 'moving_down' : 'idle';
    if (stateEl) {
      stateEl.textContent = stateEl.textContent.startsWith('状态') ? `状态 ${st}` : st;
    }
    if (hintEl) {
      hintEl.textContent =
        dir > 0 ? '正在上升…' : dir < 0 ? '正在下降…' : hintEl.dataset.idle;
    }
  }

  function bindHold(el, d) {
    if (!el) return;
    const start = (e) => {
      e.preventDefault();
      dir = d;
      paint();
    };
    const end = (e) => {
      e.preventDefault();
      if (dir === d) {
        dir = 0;
        paint();
      }
    };
    el.addEventListener('pointerdown', start);
    el.addEventListener('pointerup', end);
    el.addEventListener('pointercancel', end);
    el.addEventListener('lostpointercapture', end);
  }

  bindHold(document.getElementById('up'), 1);
  bindHold(document.getElementById('down'), -1);
  document.getElementById('stop')?.addEventListener('click', () => {
    dir = 0;
    paint();
  });
  document.querySelectorAll('[data-preset]').forEach((btn) => {
    btn.addEventListener('click', () => {
      mm = Number(btn.getAttribute('data-preset')) || mm;
      dir = 0;
      paint();
    });
  });

  setInterval(() => {
    if (!dir) return;
    mm = Math.min(max, Math.max(min, mm + dir * 4));
    paint();
  }, 50);

  paint();
})();
