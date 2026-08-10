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
  const maxHeightBadge = document.getElementById('maxHeightBadge');
  const firmwareBuildBadge = document.getElementById('firmwareBuildBadge');
  const maxHeightInput = document.getElementById('maxHeight');
  const upButton = document.getElementById('up');
  const p1Button = document.getElementById('p1');
  const p4Button = document.getElementById('p4');
  let failStreak = 0;
  let lastStatus = {};

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
    const payload = await r.json().catch(() => null);
    if (r.status === 401) {
      sessionStorage.removeItem('desk_token');
      location.href = '/login.html';
      throw new Error('unauthorized');
    }
    if (!r.ok) {
      const error = new Error(payload?.err || ('http ' + r.status));
      error.code = payload?.err || '';
      error.httpStatus = r.status;
      throw error;
    }
    return payload;
  }

  function motionError(error, fallback) {
    if (error?.code !== 'ESP_ERR_INVALID_STATE') return fallback;
    if (!lastStatus.height_known) {
      return '高度未知，请先短按下降或点击档位 1 获取高度';
    }
    if (lastStatus.upward_blocked) {
      return '已触发上限保护，请先下降并等待主机高度重新同步';
    }
    if (typeof lastStatus.height_mm === 'number' &&
        typeof lastStatus.max_height_mm === 'number' &&
        lastStatus.height_mm >= lastStatus.max_height_mm - 10) {
      return '已达到最高安全高度，无法继续上升';
    }
    return fallback;
  }

  function applyStatus(s) {
    failStreak = 0;
    showBanner('');
    lastStatus = s;
    const st = s.status || 'idle';
    const moving = st === 'moving_up' || st === 'moving_down' || st === 'goto_preset';
    document.body.classList.toggle('is-moving', moving);
    stateChip.textContent = st;
    const heightUnknown = !s.height_known;
    const ceilingReached = !heightUnknown &&
      typeof s.height_mm === 'number' && typeof s.max_height_mm === 'number' &&
      s.height_mm >= s.max_height_mm - 10;
    const upwardBlocked = !!s.upward_blocked;
    // Manual UP must remain available after boot so it can trigger the
    // controller's first height display frame. Firmware still owns all limit
    // checks and rejects unsafe upward travel.
    upButton.disabled = false;
    p4Button.disabled = heightUnknown || upwardBlocked;
    if (heightUnknown) {
      stateHint.textContent = moving
        ? '正在等待控制盒高度帧。'
        : '等待控制盒高度；可按住升或降触发显示帧。';
    } else if (upwardBlocked) {
      stateHint.textContent = '已触发上限保护；当前显示为最近一次主机高度，请先下降以重新同步。';
    } else if (ceilingReached && st === 'idle') {
      stateHint.textContent = '已达到最高安全高度。';
    } else {
      stateHint.textContent = STATE_HINT[st] || st;
    }

    if (s.height_known && typeof s.height_mm === 'number') {
      const t = Math.min(1, Math.max(0, (s.height_mm - 700) / 500));
      desk.style.setProperty('--lift', (t * 42).toFixed(1) + 'px');
      // The inner columns extend by the same distance that the complete upper
      // assembly rises, keeping both feet visually anchored to the floor.
      desk.style.setProperty('--extension', (1 + t * 42 / 68).toFixed(3));
      railFill.style.height = (12 + t * 76).toFixed(1) + '%';
      heightEl.textContent = (s.height_mm / 10).toFixed(1);
    } else {
      desk.style.setProperty('--lift', '0px');
      desk.style.setProperty('--extension', '1');
      heightEl.textContent = '—';
      railFill.style.height = '12%';
    }
    simBadge.hidden = !s.height_sim;
    meta.textContent = s.driver || '';
    const buildDate = typeof s.build_date === 'string' ? s.build_date.trim() : '';
    const buildTime = typeof s.build_time === 'string' ? s.build_time.trim() : '';
    const buildId = typeof s.build_id === 'string' ? s.build_id.trim() : '';
    firmwareBuildBadge.textContent = buildDate && buildTime
      ? `构建 ${buildDate} ${buildTime}${buildId ? ` · ${buildId}` : ''}`
      : '构建时间未知';
    lock.checked = !!s.child_lock;
    if (typeof s.max_height_mm === 'number') {
      const maxCm = (s.max_height_mm / 10).toFixed(1);
      maxHeightBadge.textContent = `上限 ${maxCm} cm`;
      if (document.activeElement !== maxHeightInput) {
        maxHeightInput.value = maxCm;
      }
    }
  }

  function bindHold(btn, startPath) {
    let holding = false;
    const start = async (e) => {
      e.preventDefault();
      if (holding || btn.disabled) return;
      holding = true;
      try {
        btn.setPointerCapture(e.pointerId);
      } catch (_) { /* ignore */ }
      btn.classList.add('active');
      try {
        await api(startPath, 'POST');
      } catch (error) {
        holding = false;
        btn.classList.remove('active');
        showBanner(motionError(error, '指令失败，请检查网络'));
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

  bindHold(upButton, '/api/v1/desk/up');
  bindHold(document.getElementById('down'), '/api/v1/desk/down');
  document.getElementById('stop').onclick = () =>
    api('/api/v1/desk/stop', 'POST').catch(() => showBanner('停止失败'));
  p1Button.onclick = () =>
    api('/api/v1/desk/preset/1/goto', 'POST')
      .catch((error) => showBanner(motionError(error, '档位失败')));
  p4Button.onclick = () =>
    api('/api/v1/desk/preset/4/goto', 'POST')
      .catch((error) => showBanner(motionError(error, '档位失败')));
  lock.onchange = () =>
    api('/api/v1/desk/child-lock', 'POST', { enabled: lock.checked })
      .catch(() => showBanner('童锁设置失败'));

  document.getElementById('maxHeightForm').onsubmit = async (e) => {
    e.preventDefault();
    const msg = document.getElementById('maxHeightMsg');
    const maxHeightMm = Math.round(Number(maxHeightInput.value) * 10);
    if (!Number.isInteger(maxHeightMm) || maxHeightMm < 640 || maxHeightMm > 1290) {
      msg.textContent = '请输入 64.0–129.0 cm';
      return;
    }
    const requestedCm = (maxHeightMm / 10).toFixed(1);
    try {
      await api('/api/v1/desk/max-height', 'POST', { max_height_mm: maxHeightMm });
      msg.textContent = `已保存 ${requestedCm} cm，重启后仍生效`;
      await tick();
    } catch (_) {
      msg.textContent = '保存失败，请检查高度范围或网络';
    }
  };

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
