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
  const preset1HeightInput = document.getElementById('preset1Height');
  const preset4HeightInput = document.getElementById('preset4Height');
  const upButton = document.getElementById('up');
  const downButton = document.getElementById('down');
  const p1Button = document.getElementById('p1');
  const p4Button = document.getElementById('p4');
  const allowRest = document.getElementById('allowRest');
  const allowBluetooth = document.getElementById('allowBluetooth');
  const allowPanel = document.getElementById('allowPanel');
  const restartButton = document.getElementById('restartButton');
  let failStreak = 0;
  let lastStatus = {};
  let restarting = false;

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
      error.reason = payload?.reason || '';
      error.httpStatus = r.status;
      throw error;
    }
    return payload;
  }

  function motionError(error, fallback) {
    if (error?.code === 'ESP_ERR_NOT_ALLOWED') {
      if (error.reason === 'child_lock' || lastStatus.child_lock) {
        return '童锁已开启，请先解除童锁';
      }
      return 'REST 接口操作已关闭，请先在设置中开启';
    }
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
    const sources = s.control_sources || {};
    const restEnabled = sources.rest !== false;
    const motionBlocked = !!s.child_lock || !restEnabled;
    // Manual UP must remain available after boot so it can trigger the
    // controller's first height display frame. Firmware still owns all limit
    // checks and rejects unsafe upward travel. Permission is the only extra
    // reason for disabling manual UP here.
    upButton.disabled = motionBlocked;
    downButton.disabled = motionBlocked;
    p1Button.disabled = motionBlocked;
    p4Button.disabled = motionBlocked || heightUnknown || upwardBlocked;
    if (s.child_lock) {
      stateHint.textContent = '童锁已开启；解除童锁后才能操作桌子。';
    } else if (!restEnabled) {
      stateHint.textContent = 'REST 接口操作已关闭；可在设置中重新开启。';
    } else if (heightUnknown) {
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
      railFill.style.height = (12 + t * 76).toFixed(1) + '%';
      heightEl.textContent = (s.height_mm / 10).toFixed(1);
    } else {
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
    allowRest.checked = restEnabled;
    allowBluetooth.checked = sources.bluetooth !== false;
    allowPanel.checked = sources.panel !== false;
    if (typeof s.max_height_mm === 'number') {
      const maxCm = (s.max_height_mm / 10).toFixed(1);
      maxHeightBadge.textContent = `上限 ${maxCm} cm`;
      if (document.activeElement !== maxHeightInput) {
        maxHeightInput.value = maxCm;
      }
    }
    if (typeof s.preset1_height_mm === 'number') {
      const preset1Cm = (s.preset1_height_mm / 10).toFixed(0);
      p1Button.textContent = `请坐 · ${preset1Cm} cm`;
      if (document.activeElement !== preset1HeightInput) {
        preset1HeightInput.value = preset1Cm;
      }
    }
    if (typeof s.preset4_height_mm === 'number') {
      const preset4Cm = (s.preset4_height_mm / 10).toFixed(0);
      p4Button.textContent = `站立 · ${preset4Cm} cm`;
      if (document.activeElement !== preset4HeightInput) {
        preset4HeightInput.value = preset4Cm;
      }
    }
  }

  const bindHold = (button, startPath) => DeskHoldControl.bindHold({
    button,
    startPath,
    send: (path) => api(path, 'POST'),
    onStartError: (error) =>
      showBanner(motionError(error, '指令失败，请检查网络')),
    onStopError: () => showBanner('停止失败，可再点「停」'),
  });

  bindHold(upButton, '/api/v1/desk/up');
  bindHold(downButton, '/api/v1/desk/down');
  document.getElementById('stop').onclick = () =>
    api('/api/v1/desk/stop', 'POST').catch(() => showBanner('停止失败'));
  p1Button.onclick = () =>
    api('/api/v1/desk/preset/1/goto', 'POST')
      .catch((error) => showBanner(motionError(error, '档位失败')));
  p4Button.onclick = () =>
    api('/api/v1/desk/preset/4/goto', 'POST')
      .catch((error) => showBanner(motionError(error, '档位失败')));
  lock.onchange = async () => {
    try {
      await api('/api/v1/desk/child-lock', 'POST', { enabled: lock.checked });
      await tick();
    } catch (_) {
      showBanner('童锁设置失败');
      await tick();
    }
  };

  function bindSourceToggle(input, source) {
    input.onchange = async () => {
      const msg = document.getElementById('accessMsg');
      input.disabled = true;
      try {
        await api('/api/v1/desk/access', 'POST', {
          source,
          enabled: input.checked,
        });
        msg.textContent = `${input.nextElementSibling.textContent}已${input.checked ? '开启' : '关闭'}`;
        await tick();
      } catch (_) {
        msg.textContent = '入口权限保存失败，请检查设备状态或网络';
        await tick();
      } finally {
        input.disabled = false;
      }
    };
  }

  bindSourceToggle(allowRest, 'rest');
  bindSourceToggle(allowBluetooth, 'bluetooth');
  bindSourceToggle(allowPanel, 'panel');

  document.getElementById('maxHeightForm').onsubmit = async (e) => {
    e.preventDefault();
    const msg = document.getElementById('maxHeightMsg');
    const maxHeightMm = Math.round(Number(maxHeightInput.value) * 10);
    if (!Number.isInteger(maxHeightMm) || maxHeightMm < 640 || maxHeightMm > 1290) {
      msg.textContent = '请输入 64.0–129.0 cm';
      return;
    }
    const preset4HeightMm = Number(lastStatus.preset4_height_mm);
    if (Number.isFinite(preset4HeightMm) && maxHeightMm < preset4HeightMm) {
      msg.textContent = `最高安全高度不能低于站立档位 ${(preset4HeightMm / 10).toFixed(0)} cm`;
      return;
    }
    const requestedCm = (maxHeightMm / 10).toFixed(1);
    try {
      await api('/api/v1/desk/max-height', 'POST', { max_height_mm: maxHeightMm });
      msg.textContent = `已保存 ${requestedCm} cm，当前已生效`;
      await tick();
    } catch (_) {
      msg.textContent = '保存失败，请检查高度范围或网络';
    }
  };

  document.getElementById('presetHeightForm').onsubmit = async (e) => {
    e.preventDefault();
    const msg = document.getElementById('presetHeightMsg');
    const preset1HeightMm = Math.round(Number(preset1HeightInput.value) * 10);
    const preset4HeightMm = Math.round(Number(preset4HeightInput.value) * 10);
    const maxHeightMm = Number(lastStatus.max_height_mm);
    if (!Number.isInteger(preset1HeightMm) ||
        !Number.isInteger(preset4HeightMm) ||
        preset1HeightMm < 640 || preset1HeightMm >= preset4HeightMm ||
        preset4HeightMm > 1290 || preset4HeightMm > maxHeightMm) {
      msg.textContent = '请坐需低于站立，且站立不得超过最高安全高度';
      return;
    }
    try {
      await api('/api/v1/desk/presets', 'POST', {
        preset1_height_mm: preset1HeightMm,
        preset4_height_mm: preset4HeightMm,
      });
      msg.textContent = '档位高度已保存，App 将自动同步';
      await tick();
    } catch (_) {
      msg.textContent = '保存失败，请检查档位顺序、高度上限或网络';
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

  restartButton.onclick = async () => {
    if (!window.confirm('确定要重启 ESP32 吗？桌子会先停止，重启后需要重新登录。')) {
      return;
    }
    const msg = document.getElementById('restartMsg');
    restartButton.disabled = true;
    try {
      await api('/api/v1/system/restart', 'POST');
      restarting = true;
      msg.textContent = '设备正在重启，稍后将返回登录页…';
      showBanner('ESP32 正在重启，请稍候');
      window.setTimeout(() => {
        sessionStorage.removeItem('desk_token');
        location.href = '/login.html';
      }, 4000);
    } catch (_) {
      restartButton.disabled = false;
      msg.textContent = '重启失败，请检查设备状态或网络';
    }
  };

  async function tick() {
    if (restarting) return;
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
