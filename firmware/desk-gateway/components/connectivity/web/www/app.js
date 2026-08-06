(() => {
  const token = sessionStorage.getItem('desk_token');
  if (!token) {
    location.href = '/login.html';
    return;
  }
  const headers = {
    Authorization: `Bearer ${token}`,
    'Content-Type': 'application/json',
  };
  const desk = document.getElementById('desk');
  const railFill = document.getElementById('railFill');
  const heightEl = document.getElementById('height');
  const meta = document.getElementById('meta');
  const lock = document.getElementById('lock');
  const banner = document.getElementById('banner');
  const simBadge = document.getElementById('simBadge');
  const stateChip = document.getElementById('stateChip');
  const stateHint = document.getElementById('stateHint');
  let failStreak = 0;

  const STATE_HINT = {
    idle: '按住下方按钮升降，松手即停。',
    moving_up: '正在上升…',
    moving_down: '正在下降…',
    goto_preset: '前往档位…',
    error: '状态异常',
  };

  function showBanner(text) {
    if (!text) {
      banner.hidden = true;
      banner.textContent = '';
      return;
    }
    banner.hidden = false;
    banner.textContent = text;
  }

  async function api(path, method = 'GET', body) {
    const r = await fetch(path, {
      method,
      headers,
      body: body ? JSON.stringify(body) : undefined,
    });
    if (r.status === 401) {
      sessionStorage.removeItem('desk_token');
      location.href = '/login.html';
      throw new Error('unauthorized');
    }
    if (!r.ok) {
      throw new Error('http ' + r.status);
    }
    return r.json();
  }

  function applyStatus(s) {
    failStreak = 0;
    showBanner('');
    const st = s.status || 'idle';
    const moving = st === 'moving_up' || st === 'moving_down';
    document.body.classList.toggle('is-moving', moving);
    stateChip.textContent = st;
    stateHint.textContent = STATE_HINT[st] || st;

    if (s.height_known && typeof s.height_mm === 'number') {
      const t = Math.min(1, Math.max(0, (s.height_mm - 700) / 500));
      desk.style.setProperty('--lift', (t * 42).toFixed(1) + 'px');
      railFill.style.height = (12 + t * 76).toFixed(1) + '%';
      heightEl.textContent = (s.height_mm / 10).toFixed(1);
    } else {
      heightEl.textContent = '—';
      railFill.style.height = '12%';
    }
    simBadge.hidden = !s.height_sim;
    meta.textContent = s.driver || '';
    lock.checked = !!s.child_lock;
  }

  function bindHold(btn, startPath) {
    let holding = false;
    const start = async (e) => {
      e.preventDefault();
      if (holding) return;
      holding = true;
      try {
        btn.setPointerCapture(e.pointerId);
      } catch (_) { /* ignore */ }
      btn.classList.add('active');
      try {
        await api(startPath, 'POST');
      } catch (_) {
        holding = false;
        btn.classList.remove('active');
        showBanner('指令失败，请检查网络');
      }
    };
    const end = async (e) => {
      e.preventDefault();
      if (!holding) return;
      holding = false;
      btn.classList.remove('active');
      try {
        if (btn.hasPointerCapture?.(e.pointerId)) {
          btn.releasePointerCapture(e.pointerId);
        }
      } catch (_) { /* ignore */ }
      try {
        await api('/api/v1/desk/stop', 'POST');
      } catch (_) {
        showBanner('停止失败，可再点「停」');
      }
    };
    btn.addEventListener('pointerdown', start);
    btn.addEventListener('pointerup', end);
    btn.addEventListener('pointercancel', end);
    btn.addEventListener('lostpointercapture', (e) => {
      if (holding) end(e);
    });
    btn.addEventListener('contextmenu', (e) => e.preventDefault());
  }

  bindHold(document.getElementById('up'), '/api/v1/desk/up');
  bindHold(document.getElementById('down'), '/api/v1/desk/down');
  document.getElementById('stop').onclick = () =>
    api('/api/v1/desk/stop', 'POST').catch(() => showBanner('停止失败'));
  document.getElementById('p1').onclick = () =>
    api('/api/v1/desk/preset/1/goto', 'POST').catch(() => showBanner('档位失败'));
  document.getElementById('p4').onclick = () =>
    api('/api/v1/desk/preset/4/goto', 'POST').catch(() => showBanner('档位失败'));
  lock.onchange = () =>
    api('/api/v1/desk/child-lock', 'POST', { enabled: lock.checked })
      .catch(() => showBanner('童锁设置失败'));

  document.getElementById('pwForm').onsubmit = async (e) => {
    e.preventDefault();
    const password = new FormData(e.target).get('password');
    const msg = document.getElementById('pwMsg');
    try {
      const j = await api('/api/v1/auth/password', 'POST', { password });
      msg.textContent = j.ok ? '已保存，下次登录用新密码' : '保存失败';
      if (j.ok) e.target.reset();
    } catch (_) {
      msg.textContent = '保存失败（网络或未授权）';
    }
  };

  async function tick() {
    try {
      applyStatus(await api('/api/v1/desk/status'));
    } catch (_) {
      failStreak += 1;
      if (failStreak >= 3) {
        showBanner('无法连接网关，请确认与板子在同一局域网');
      }
    }
  }
  tick();
  setInterval(tick, 250);
})();
