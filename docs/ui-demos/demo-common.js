/** 各 demo 共用的假控桌逻辑（仅预览） */
(function () {
  const state = {
    height: 95.0,
    status: 'idle',
    lock: false,
    holding: null,
    min: 70,
    max: 120,
  };

  function $(id) { return document.getElementById(id); }

  function render() {
    const h = $('height');
    const st = $('status');
    const hint = $('hint');
    const lock = $('lock');
    const rail = $('rail');
    const desk = $('desk');
    if (h) h.textContent = state.height.toFixed(1) + ' cm';
    if (st) st.textContent = state.status;
    if (hint) {
      hint.textContent = state.status === 'moving_up' ? '正在上升…'
        : state.status === 'moving_down' ? '正在下降…'
        : '松手即停 · 按住升/降';
    }
    if (lock) lock.checked = state.lock;
    const t = (state.height - state.min) / (state.max - state.min);
    if (rail) rail.style.height = (8 + t * 84) + '%';
    if (desk) desk.style.setProperty('--lift', (t * 40).toFixed(1) + 'px');
    document.body.dataset.status = state.status;
  }

  function start(dir) {
    state.holding = dir;
    state.status = dir === 'up' ? 'moving_up' : 'moving_down';
    render();
  }
  function stop() {
    state.holding = null;
    state.status = 'idle';
    render();
  }

  setInterval(() => {
    if (state.holding === 'up') state.height = Math.min(state.max, state.height + 0.15);
    if (state.holding === 'down') state.height = Math.max(state.min, state.height - 0.15);
    if (state.holding) render();
  }, 50);

  function bindHold(id, dir) {
    const el = $(id);
    if (!el) return;
    const down = (e) => { e.preventDefault(); start(dir); try { el.setPointerCapture(e.pointerId); } catch (_) {} };
    const up = (e) => { e.preventDefault(); stop(); };
    el.addEventListener('pointerdown', down);
    el.addEventListener('pointerup', up);
    el.addEventListener('pointercancel', up);
    el.addEventListener('lostpointercapture', () => { if (state.holding) stop(); });
    el.addEventListener('contextmenu', (e) => e.preventDefault());
  }

  window.DeskDemo = {
    init() {
      bindHold('up', 'up');
      bindHold('down', 'down');
      const stopBtn = $('stop');
      if (stopBtn) stopBtn.onclick = () => stop();
      const lock = $('lock');
      if (lock) lock.onchange = () => { state.lock = lock.checked; };
      $('p1') && ($('p1').onclick = () => { state.height = 75; render(); });
      $('p4') && ($('p4').onclick = () => { state.height = 115; render(); });
      render();
    }
  };
})();
