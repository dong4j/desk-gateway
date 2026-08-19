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
  const tofHeightEl = document.getElementById('tofHeight');
  const rightGapEl = document.getElementById('rightGap');
  const tofHeightReadout = document.getElementById('tofHeightReadout');
  const rightGapReadout = document.getElementById('rightGapReadout');
  const meta = document.getElementById('meta');
  const lock = document.getElementById('lock');
  const banner = document.getElementById('banner');
  const simBadge = document.getElementById('simBadge');
  const stateChip = document.getElementById('stateChip');
  const stateHint = document.getElementById('stateHint');
  const maxHeightBadge = document.getElementById('maxHeightBadge');
  const firmwareBuildBadge = document.getElementById('firmwareBuildBadge');
  const minHeightInput = document.getElementById('minHeight');
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
  const allowMqtt = document.getElementById('allowMqtt');
  const mqttForm = document.getElementById('mqttForm');
  const mqttClientEnabled = document.getElementById('mqttClientEnabled');
  const mqttHost = document.getElementById('mqttHost');
  const mqttPort = document.getElementById('mqttPort');
  const mqttTls = document.getElementById('mqttTls');
  const mqttUser = document.getElementById('mqttUser');
  const mqttPassword = document.getElementById('mqttPassword');
  const mqttPrefix = document.getElementById('mqttPrefix');
  const mqttDeviceId = document.getElementById('mqttDeviceId');
  const mqttStatus = document.getElementById('mqttStatus');
  const mqttMsg = document.getElementById('mqttMsg');
  const controllerResetButton = document.getElementById('controllerResetButton');
  const controllerResetMsg = document.getElementById('controllerResetMsg');
  const restartButton = document.getElementById('restartButton');
  const bondCount = document.getElementById('bondCount');
  const bondList = document.getElementById('bondList');
  const bondMsg = document.getElementById('bondMsg');
  const pairingWindowButton = document.getElementById('pairingWindowButton');
  const pairingWindowHint = document.getElementById('pairingWindowHint');
  const bondRecoveryReady = document.getElementById('bondRecoveryReady');
  const bondRecoveryWindow = document.getElementById('bondRecoveryWindow');
  const deleteAllBondsButton = document.getElementById('deleteAllBondsButton');
  const customPresetControls = document.getElementById('customPresetControls');
  const customPresetCount = document.getElementById('customPresetCount');
  const customPresetList = document.getElementById('customPresetList');
  const customPresetMsg = document.getElementById('customPresetMsg');
  const customPresetName = document.getElementById('customPresetName');
  const customPresetHeight = document.getElementById('customPresetHeight');
  const customPresetAdd = document.getElementById('customPresetAdd');
  const reminderCard = document.getElementById('reminderCard');
  const reminderPhase = document.getElementById('reminderPhase');
  const reminderCompactStatus = document.getElementById('reminderCompactStatus');
  const reminderQuickStart = document.getElementById('reminderQuickStart');
  const reminderExpandButton = document.getElementById('reminderExpandButton');
  const reminderExpandLabel = document.getElementById('reminderExpandLabel');
  const reminderDetails = document.getElementById('reminderDetails');
  const reminderTime = document.getElementById('reminderTime');
  const reminderStatus = document.getElementById('reminderStatus');
  const reminderCycle = document.getElementById('reminderCycle');
  const reminderPrimary = document.getElementById('reminderPrimary');
  const reminderPause = document.getElementById('reminderPause');
  const reminderSkip = document.getElementById('reminderSkip');
  const reminderSnooze = document.getElementById('reminderSnooze');
  const reminderStop = document.getElementById('reminderStop');
  const reminderMsg = document.getElementById('reminderMsg');
  const audioEnabled = document.getElementById('audioEnabled');
  const audioVolume = document.getElementById('audioVolume');
  const audioVolumeLabel = document.getElementById('audioVolumeLabel');
  const audioStatus = document.getElementById('audioStatus');
  const audioStop = document.getElementById('audioStop');
  const focusMinutes = document.getElementById('focusMinutes');
  const shortBreakMinutes = document.getElementById('shortBreakMinutes');
  const longBreakMinutes = document.getElementById('longBreakMinutes');
  const focusesPerLongBreak = document.getElementById('focusesPerLongBreak');
  const appDialog = document.getElementById('appDialog');
  const appDialogForm = document.getElementById('appDialogForm');
  const appDialogEyebrow = document.getElementById('appDialogEyebrow');
  const appDialogTitle = document.getElementById('appDialogTitle');
  const appDialogMessage = document.getElementById('appDialogMessage');
  const appDialogFields = document.getElementById('appDialogFields');
  const appDialogError = document.getElementById('appDialogError');
  const appDialogCancel = document.getElementById('appDialogCancel');
  const appDialogConfirm = document.getElementById('appDialogConfirm');
  let failStreak = 0;
  let lastStatus = {};
  let controllerResetPending = false;
  let controllerResetSeenActive = false;
  let controllerResetPromptShown = false;
  let restarting = false;
  let lastBondSnapshot = null;
  let lastBondRefreshAt = 0;
  let bondRefreshInFlight = false;
  let lastHeightPresetSnapshot = null;
  let lastHeightPresetRefreshAt = 0;
  let heightPresetRefreshInFlight = false;
  let activeDialog = null;
  let dialogQueue = Promise.resolve();

  const STATE_HINT = {
    idle: '按住下方按钮升降，松手即停。',
    moving_up: '正在上升…',
    moving_down: '正在下降…',
    goto_preset: '前往档位…',
    controller_resetting: '控制盒重置中，请等待约 8 秒。',
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

  /**
   * 结束当前应用内弹窗，并恢复触发操作前的键盘焦点。
   * 所有取消路径都返回 null，危险操作只有明确点击确认后才会继续。
   */
  function settleAppDialog(value) {
    if (!activeDialog) return;
    const { resolve, restoreFocus } = activeDialog;
    activeDialog = null;
    appDialog.close();
    resolve(value);
    if (restoreFocus?.isConnected) {
      window.setTimeout(() => restoreFocus.focus(), 0);
    }
  }

  /**
   * 渲染一次自定义确认或输入弹窗。fields 为空时即为确认框；
   * validate 返回错误文案时保持弹窗打开，避免依赖浏览器原生校验提示。
   */
  function presentAppDialog(options) {
    return new Promise((resolve) => {
      const fields = options.fields || [];
      appDialogEyebrow.textContent = options.eyebrow || 'Desk Gateway';
      appDialogTitle.textContent = options.title || '请确认操作';
      appDialogMessage.textContent = options.message || '';
      appDialogMessage.hidden = !options.message;
      appDialogConfirm.textContent = options.confirmLabel || '确定';
      appDialogCancel.textContent = options.cancelLabel || '取消';
      appDialog.classList.toggle('is-danger', options.tone === 'danger');
      appDialogFields.replaceChildren();
      appDialogError.textContent = '';

      fields.forEach((field) => {
        const label = document.createElement('label');
        label.className = 'app-dialog-field';
        label.textContent = field.label;
        const input = document.createElement('input');
        input.name = field.name;
        input.type = field.type || 'text';
        input.value = field.value ?? '';
        input.autocomplete = 'off';
        if (field.inputMode) input.inputMode = field.inputMode;
        if (field.placeholder) input.placeholder = field.placeholder;
        if (field.min !== undefined) input.min = String(field.min);
        if (field.max !== undefined) input.max = String(field.max);
        if (field.step !== undefined) input.step = String(field.step);
        if (field.maxLength !== undefined) input.maxLength = field.maxLength;
        label.appendChild(input);
        appDialogFields.appendChild(label);
      });

      activeDialog = {
        resolve,
        restoreFocus: document.activeElement,
        validate: options.validate,
      };
      appDialog.showModal();
      const firstInput = appDialogFields.querySelector('input');
      window.setTimeout(() => (firstInput || appDialogConfirm).focus(), 0);
    });
  }

  /**
   * 串行打开弹窗，避免状态轮询触发的 B12 提示与用户正在操作的弹窗冲突。
   */
  function openAppDialog(options) {
    const run = () => presentAppDialog(options);
    const result = dialogQueue.then(run, run);
    dialogQueue = result.then(() => undefined, () => undefined);
    return result;
  }

  async function confirmAction(options) {
    return (await openAppDialog(options)) !== null;
  }

  appDialogForm.onsubmit = (event) => {
    event.preventDefault();
    if (!activeDialog) return;
    const values = Object.fromEntries(new FormData(appDialogForm).entries());
    const error = activeDialog.validate?.(values) || '';
    if (error) {
      appDialogError.textContent = error;
      return;
    }
    settleAppDialog(values);
  };
  appDialogCancel.onclick = () => settleAppDialog(null);
  appDialog.addEventListener('cancel', (event) => {
    event.preventDefault();
    settleAppDialog(null);
  });
  appDialog.addEventListener('click', (event) => {
    if (event.target === appDialog) settleAppDialog(null);
  });

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
      const errorCode = payload?.err || payload?.error || '';
      const error = new Error(errorCode || ('http ' + r.status));
      error.code = errorCode;
      error.reason = payload?.reason || '';
      error.httpStatus = r.status;
      throw error;
    }
    return payload;
  }

  function renderBondDevices(snapshot) {
    lastBondSnapshot = snapshot;
    bondCount.textContent = `${snapshot.devices.length} / ${snapshot.capacity}`;
    const pairingOpen = !!snapshot.pairing_window?.open;
    const seconds = Math.max(0, Number(snapshot.pairing_window?.remaining_seconds) || 0);
    pairingWindowButton.textContent = pairingOpen
      ? '提前关闭配对窗口'
      : '允许新设备配对';
    pairingWindowButton.disabled = !pairingOpen &&
      snapshot.devices.length >= snapshot.capacity;
    pairingWindowHint.textContent = pairingOpen
      ? `允许新设备配对，剩余 ${seconds} 秒。第三台设备配对成功后会自动关闭。`
      : snapshot.devices.length >= snapshot.capacity
        ? '已达到 3 台上限，请先删除旧设备。'
        : '配对窗口默认关闭；开启后固定持续 120 秒。';
    if (!bondRecoveryReady.hidden) {
      bondRecoveryWindow.textContent = pairingOpen
        ? `配对窗口剩余 ${seconds} 秒，请返回 App 重新连接。`
        : '配对窗口已关闭，请先点击“允许新设备配对”。';
    }

    const conflict = DeskBondManagement.hasDeleteConflict(snapshot);
    deleteAllBondsButton.disabled = snapshot.devices.length === 0 || conflict;
    bondList.replaceChildren();
    if (snapshot.devices.length === 0) {
      const empty = document.createElement('p');
      empty.className = 'settings-note';
      empty.textContent = '暂无已配对设备';
      bondList.appendChild(empty);
      return;
    }

    snapshot.devices.forEach((device) => {
      const row = document.createElement('div');
      row.className = 'bond-row';
      const main = document.createElement('div');
      main.className = 'bond-row-main';
      const label = document.createElement('span');
      label.className = 'bond-label';
      label.textContent = device.label;
      const status = document.createElement('span');
      status.className = 'bond-status';
      status.textContent = DeskBondManagement.statusText(device);
      main.append(label, status);
      if (device.delete_state === 'failed' && device.delete_error) {
        const error = document.createElement('span');
        error.className = 'bond-status bond-error';
        error.textContent = device.delete_error;
        main.appendChild(error);
      }

      const actions = document.createElement('div');
      actions.className = 'bond-row-actions';
      const rename = document.createElement('button');
      rename.type = 'button';
      rename.className = 'bond-rename-button';
      rename.textContent = '重命名';
      rename.disabled = device.delete_state !== 'idle';
      rename.onclick = async () => {
        const values = await openAppDialog({
          eyebrow: '蓝牙设备',
          title: '重命名设备',
          message: `为“${device.label}”设置别名，留空可恢复默认名称。`,
          confirmLabel: '保存名称',
          fields: [{
            name: 'alias',
            label: '设备别名',
            value: device.alias || '',
            maxLength: 48,
            placeholder: '例如：我的手机',
          }],
          validate: ({ alias: draft }) => {
            try {
              DeskBondManagement.normalizeAlias(draft);
              return '';
            } catch (error) {
              return error.message;
            }
          },
        });
        if (values === null) return;
        let alias;
        try {
          alias = DeskBondManagement.normalizeAlias(values.alias);
        } catch (error) {
          bondMsg.textContent = error.message;
          return;
        }
        rename.disabled = true;
        bondMsg.textContent = '正在保存设备别名…';
        try {
          await api(
            `/api/v1/bluetooth/bonds/${encodeURIComponent(device.id)}/alias`,
            'POST', { alias });
          bondMsg.textContent = alias ? '设备别名已更新' : '已恢复默认名称';
        } catch (_) {
          bondMsg.textContent = '设备别名保存失败';
        }
        await refreshBondDevices(true);
      };

      const remove = document.createElement('button');
      remove.type = 'button';
      remove.className = 'bond-delete-button';
      remove.textContent = device.delete_state === 'failed' ? '重试' : '删除';
      remove.disabled = device.delete_state === 'pending';
      remove.onclick = async () => {
        const confirmed = await confirmAction({
          eyebrow: '蓝牙设备',
          title: '删除配对设备',
          message: `确定删除“${device.label}”吗？在线设备会立即断开。`,
          confirmLabel: '删除设备',
          tone: 'danger',
        });
        if (!confirmed) return;
        remove.disabled = true;
        bondMsg.textContent = '正在提交删除请求…';
        try {
          const result = await api(
            `/api/v1/bluetooth/bonds/${encodeURIComponent(device.id)}`,
            'DELETE');
          bondMsg.textContent = result.ok
            ? '删除请求已处理，正在刷新设备列表…'
            : '删除失败';
        } catch (error) {
          bondMsg.textContent = error.httpStatus === 404
            ? '设备已删除或不存在'
            : '删除失败，请刷新后重试';
        }
        await refreshBondDevices(true);
      };

      const recover = document.createElement('button');
      recover.type = 'button';
      recover.className = 'bond-recovery-button';
      recover.textContent = '恢复连接';
      recover.disabled = device.delete_state !== 'idle';
      recover.onclick = async () => {
        const detectorWarning = snapshot.auto_child_lock?.device_id === device.id
          ? '\n\n该设备是自动童锁检测设备，恢复后需要重新选择。'
          : '';
        const confirmed = await confirmAction({
          eyebrow: '蓝牙设备',
          title: '恢复设备连接',
          message: `恢复“${device.label}”的连接吗？网关会删除这条旧配对记录并开放 120 秒配对窗口。${detectorWarning}`,
          confirmLabel: '恢复连接',
          tone: 'danger',
        });
        if (!confirmed) return;
        recover.disabled = true;
        bondMsg.textContent = '正在清除旧配对信息…';
        try {
          await DeskBondManagement.recoverConnection(
            () => api(
              `/api/v1/bluetooth/bonds/${encodeURIComponent(device.id)}`,
              'DELETE'),
            () => api('/api/v1/bluetooth/pairing-window', 'POST'),
          );
          bondRecoveryReady.hidden = false;
          bondMsg.textContent = '旧配对信息已清除，已开放 120 秒配对窗口';
        } catch (error) {
          bondMsg.textContent = error.httpStatus === 409
            ? '旧设备仍在删除中，请稍后刷新后再开放配对窗口'
            : '连接恢复失败，请刷新后重试';
        }
        await refreshBondDevices(true);
      };
      actions.append(rename, recover, remove);
      row.append(main, actions);
      bondList.appendChild(row);
    });
  }

  async function refreshBondDevices(force = false) {
    const now = Date.now();
    const fast = DeskBondManagement.shouldPollFrequently(lastBondSnapshot);
    const interval = fast ? 750 : 5000;
    if (bondRefreshInFlight || (!force && now - lastBondRefreshAt < interval)) {
      return;
    }
    bondRefreshInFlight = true;
    try {
      const snapshot = await api('/api/v1/bluetooth/bonds');
      lastBondRefreshAt = Date.now();
      renderBondDevices(snapshot);
      if (!snapshot.devices.some((device) => device.delete_state === 'failed')) {
        bondMsg.textContent = '';
      }
    } catch (error) {
      if (error.message !== 'unauthorized') {
        bondMsg.textContent = '无法读取配对设备，请检查网关状态';
      }
    } finally {
      bondRefreshInFlight = false;
    }
  }

  function heightPresetFailure(error, fallback) {
    if (error?.code === 'preset_capacity_full') return '自定义档位已达到上限';
    if (error?.code === 'preset_not_deletable') return '内置档位不能删除';
    if (error?.code === 'preset_not_found') return '档位已不存在，列表已刷新';
    return fallback;
  }

  function updateCustomPresetControlAvailability() {
    customPresetControls.querySelectorAll('[data-height-mm]').forEach((control) => {
      const target = Number(control.dataset.heightMm);
      const movesUp = lastStatus.height_known &&
        typeof lastStatus.height_mm === 'number' &&
        lastStatus.height_mm < target;
      control.disabled = !lastStatus.height_known || !!lastStatus.child_lock ||
        lastStatus.control_sources?.rest === false ||
        !!lastStatus.controller_reset_active ||
        (!!lastStatus.upward_blocked && movesUp);
    });
  }

  function renderHeightPresets(snapshot) {
    lastHeightPresetSnapshot = snapshot;
    customPresetCount.textContent =
      `${snapshot.custom_count} / ${snapshot.custom_capacity}`;
    customPresetAdd.disabled = !DeskHeightPresets.canCreate(snapshot);
    customPresetControls.replaceChildren();
    customPresetList.replaceChildren();

    const custom = snapshot.presets.filter((preset) => !preset.built_in);
    custom.forEach((preset) => {
      const control = document.createElement('button');
      control.type = 'button';
      control.className = 'custom-preset-control';
      control.textContent = `${preset.name} · ${(preset.height_mm / 10).toFixed(1)} cm`;
      control.dataset.heightMm = String(preset.height_mm);
      control.onclick = () => api(
        `/api/v1/desk/height-presets/${encodeURIComponent(preset.id)}/goto`,
        'POST').catch((error) =>
          showBanner(motionError(error, '自定义档位执行失败')));
      customPresetControls.appendChild(control);

      const row = document.createElement('div');
      row.className = 'custom-preset-row';
      const copy = document.createElement('div');
      copy.className = 'custom-preset-copy';
      const label = document.createElement('span');
      label.textContent = preset.name;
      const height = document.createElement('small');
      height.textContent = `${(preset.height_mm / 10).toFixed(1)} cm`;
      copy.append(label, height);
      const actions = document.createElement('div');
      actions.className = 'bond-row-actions';
      const edit = document.createElement('button');
      edit.type = 'button';
      edit.className = 'bond-rename-button';
      edit.textContent = '修改';
      edit.onclick = async () => {
        const values = await openAppDialog({
          eyebrow: '自定义档位',
          title: '修改档位',
          message: '同时调整档位名称和目标高度。',
          confirmLabel: '保存档位',
          fields: [
            {
              name: 'name',
              label: '档位名称',
              value: preset.name,
              maxLength: 48,
            },
            {
              name: 'height',
              label: '档位高度（cm）',
              type: 'number',
              value: (preset.height_mm / 10).toFixed(1),
              inputMode: 'decimal',
              min: (lastStatus.min_height_mm ?? 550) / 10,
              max: 94,
              step: 0.1,
            },
          ],
          validate: ({ name, height: heightCm }) => {
            try {
              DeskHeightPresets.normalizeName(name);
              DeskHeightPresets.heightMmFromCm(
                heightCm, lastStatus.min_height_mm ?? 550);
              return '';
            } catch (error) {
              return error.message;
            }
          },
        });
        if (values === null) return;
        try {
          await api(`/api/v1/desk/height-presets/${encodeURIComponent(preset.id)}`,
            'POST', {
              name: DeskHeightPresets.normalizeName(values.name),
              height_mm: DeskHeightPresets.heightMmFromCm(
                values.height, lastStatus.min_height_mm ?? 550),
            });
          customPresetMsg.textContent = '自定义档位已更新';
        } catch (error) {
          customPresetMsg.textContent = heightPresetFailure(
            error, error.message || '档位修改失败');
        }
        await refreshHeightPresets(true);
      };
      const remove = document.createElement('button');
      remove.type = 'button';
      remove.className = 'bond-delete-button';
      remove.textContent = '删除';
      remove.onclick = async () => {
        const confirmed = await confirmAction({
          eyebrow: '自定义档位',
          title: '删除档位',
          message: `确定删除“${preset.name}”档位吗？`,
          confirmLabel: '删除档位',
          tone: 'danger',
        });
        if (!confirmed) return;
        try {
          await api(`/api/v1/desk/height-presets/${encodeURIComponent(preset.id)}`,
            'DELETE');
          customPresetMsg.textContent = '自定义档位已删除';
        } catch (error) {
          customPresetMsg.textContent = heightPresetFailure(
            error, '档位删除失败');
        }
        await refreshHeightPresets(true);
      };
      actions.append(edit, remove);
      row.append(copy, actions);
      customPresetList.appendChild(row);
    });
    if (custom.length === 0) {
      const empty = document.createElement('p');
      empty.className = 'settings-note';
      empty.textContent = '暂无自定义档位';
      customPresetList.appendChild(empty);
    }
    updateCustomPresetControlAvailability();
  }

  async function refreshHeightPresets(force = false) {
    const now = Date.now();
    if (heightPresetRefreshInFlight ||
        (!force && now - lastHeightPresetRefreshAt < 5000)) {
      return;
    }
    heightPresetRefreshInFlight = true;
    try {
      const snapshot = await api('/api/v1/desk/height-presets');
      lastHeightPresetRefreshAt = Date.now();
      renderHeightPresets(snapshot);
    } catch (error) {
      if (error.message !== 'unauthorized') {
        customPresetMsg.textContent = '无法读取自定义档位';
      }
    } finally {
      heightPresetRefreshInFlight = false;
    }
  }

  function motionError(error, fallback) {
    if (error?.code === 'ESP_ERR_NOT_ALLOWED') {
      if (error.reason === 'child_lock' || lastStatus.child_lock) {
        return '童锁已开启，请先解除童锁';
      }
      return 'REST 接口操作已关闭，请先在设置中开启';
    }
    if (error?.code !== 'ESP_ERR_INVALID_STATE') return fallback;
    if (lastStatus.controller_reset_active) {
      return '控制盒正在重置，请等待完成或点击“停”中断';
    }
    if (!lastStatus.height_known) {
      return 'TOF400C 高度不可用，当前禁止上升和档位控制';
    }
    return fallback;
  }

  async function startControllerReset(requireConfirmation) {
    if (requireConfirmation) {
      const confirmed = await confirmAction({
        eyebrow: '控制盒维护',
        title: '重置控制盒',
        message: '仅在控制盒显示 B12 等故障时执行。确认桌子周围没有障碍物后，再模拟同时按住升降键约 8 秒。',
        confirmLabel: '开始重置',
        tone: 'danger',
      });
      if (!confirmed) return;
    }
    controllerResetButton.disabled = true;
    controllerResetMsg.textContent = '正在启动控制盒重置…';
    controllerResetPending = true;
    controllerResetSeenActive = false;
    try {
      await api('/api/v1/desk/controller/reset', 'POST');
      await tick();
    } catch (error) {
      controllerResetPending = false;
      controllerResetButton.disabled = false;
      controllerResetMsg.textContent = motionError(
        error, '无法启动控制盒重置，请确认桌子已停止');
    }
  }

  function applyReminderStatus(reminder, audio) {
    const view = DeskReminderControl.viewModel(reminder, audio);
    reminderCard.classList.toggle('is-unavailable', !view.available);
    reminderPhase.textContent = view.phaseLabel;
    reminderCompactStatus.textContent = `${view.statusText} · ${view.timeText}`;
    reminderQuickStart.disabled = !view.available || view.primaryAction !== 'start_focus';
    reminderTime.textContent = view.timeText;
    reminderStatus.textContent = view.available
      ? view.statusText : (reminder.last_error || '番茄时钟不可用');
    const cycleSize = Number(reminder.focuses_per_long_break) || 4;
    const completed = Number(reminder.completed_focus_count) || 0;
    reminderCycle.textContent = `本轮已完成 ${completed % cycleSize} / ${cycleSize} 次专注`;

    reminderPrimary.hidden = !view.primaryAction;
    reminderPrimary.dataset.action = view.primaryAction || '';
    reminderPrimary.textContent = view.primaryLabel;
    reminderPause.hidden = !view.pauseAction;
    reminderPause.dataset.action = view.pauseAction || '';
    reminderPause.textContent = view.pauseLabel;
    reminderPrimary.disabled = !view.available;
    reminderPause.disabled = !view.available;
    reminderSkip.disabled = !view.available || !view.canSkip;
    reminderStop.disabled = !view.available || !view.canStop;
    reminderSnooze.hidden = !view.canSnooze;
    reminderSnooze.disabled = !view.available;

    audioEnabled.checked = !!audio.enabled;
    audioEnabled.disabled = !view.audioAvailable;
    audioVolume.disabled = !view.audioAvailable;
    if (document.activeElement !== audioVolume) {
      audioVolume.value = Number(audio.volume_percent) || 0;
    }
    audioVolumeLabel.textContent = `${audioVolume.value}%`;
    audioStatus.textContent = `${view.audioStatus} · ${audio.voice_pack || 'zh-CN-default'}`;
    document.querySelectorAll('[data-prompt]').forEach((button) => {
      button.disabled = !view.audioAvailable || !audio.enabled || audio.volume_percent === 0;
    });
    audioStop.disabled = !view.audioAvailable || !audio.playing;
    reminderMsg.textContent = DeskReminderControl.previewActionHint(
      reminderMsg.textContent, !!audio.playing);

    const configValues = [
      [focusMinutes, reminder.focus_minutes],
      [shortBreakMinutes, reminder.short_break_minutes],
      [longBreakMinutes, reminder.long_break_minutes],
      [focusesPerLongBreak, reminder.focuses_per_long_break],
    ];
    configValues.forEach(([input, value]) => {
      if (document.activeElement !== input && Number.isInteger(value)) {
        input.value = value;
      }
    });
  }

  function applyStatus(s) {
    failStreak = 0;
    showBanner('');
    lastStatus = s;
    applyReminderStatus(s.reminder || {}, s.audio || {});
    const resetting = !!s.controller_reset_active;
    if (!s.controller_reset_recommended) {
      controllerResetPromptShown = false;
    } else if (!controllerResetPromptShown) {
      controllerResetPromptShown = true;
      showBanner('升降后高度没有变化，控制盒可能出现 B12 错误');
      void confirmAction({
        eyebrow: '检测到控制盒异常',
        title: '可能出现 B12 错误',
        message: '升降指令发出后高度没有正常变化。确认桌子周围没有障碍物后，可以立即执行约 8 秒控制盒重置。',
        confirmLabel: '立即重置',
        tone: 'danger',
      }).then((confirmed) => {
        if (confirmed && lastStatus.controller_reset_recommended &&
            !lastStatus.controller_reset_active) {
          void startControllerReset(false);
        }
      });
    }
    const st = resetting ? 'controller_resetting' : (s.status || 'idle');
    const moving = st === 'moving_up' || st === 'moving_down' ||
      st === 'goto_preset' || resetting;
    document.body.classList.toggle('is-moving', moving);
    stateChip.textContent = st;
    const heightUnknown = !s.height_known;
    const tofHeightKnown = !!s.tof_height_known && typeof s.tof_height_mm === 'number';
    const displayHeightKnown = tofHeightKnown ||
      (!!s.height_known && typeof s.height_mm === 'number');
    const displayHeightMm = tofHeightKnown ? s.tof_height_mm : s.height_mm;
    const sources = s.control_sources || {};
    const restEnabled = sources.rest !== false;
    const motionBlocked = !!s.child_lock || !restEnabled || resetting;
    const upwardBlocked = !!s.upward_blocked;
    const p1MovesUp = !heightUnknown &&
      typeof s.preset1_height_mm === 'number' && s.height_mm < s.preset1_height_mm;
    const p4MovesUp = !heightUnknown &&
      typeof s.preset4_height_mm === 'number' && s.height_mm < s.preset4_height_mm;
    // 前端只做状态提示；固件仍会在命令入口和运动过程中重复执行相同限制。
    upButton.disabled = motionBlocked || upwardBlocked;
    downButton.disabled = motionBlocked;
    p1Button.disabled = motionBlocked || heightUnknown ||
      (upwardBlocked && p1MovesUp);
    p4Button.disabled = motionBlocked || heightUnknown ||
      (upwardBlocked && p4MovesUp);
    controllerResetButton.disabled = !s.controller_reset_supported ||
      motionBlocked || st !== 'idle';
    if (!s.controller_reset_supported) {
      controllerResetMsg.textContent = '当前桌型不支持控制盒重置';
    } else if (resetting) {
      controllerResetSeenActive = true;
      controllerResetMsg.textContent = '正在输出重置码，请勿操作升降键…';
    } else if (controllerResetPending && controllerResetSeenActive) {
      controllerResetPending = false;
      controllerResetSeenActive = false;
      controllerResetMsg.textContent = '重置序列已结束，请检查控制盒错误码';
    }
    if (s.child_lock) {
      stateHint.textContent = '童锁已开启；解除童锁后才能操作桌子。';
    } else if (!restEnabled) {
      stateHint.textContent = 'REST 接口操作已关闭；可在设置中重新开启。';
    } else if (resetting) {
      stateHint.textContent = STATE_HINT.controller_resetting;
    } else if (heightUnknown) {
      stateHint.textContent = 'TOF400C 高度不可用；下降仍可操作，上升已锁定。';
    } else if (upwardBlocked) {
      stateHint.textContent = s.height_mm >= s.max_height_mm
        ? '已到最高安全高度；可以下降。'
        : '80 cm 以下检测到右侧障碍或距离数据不可用；可以下降。';
    } else {
      stateHint.textContent = STATE_HINT[st] || st;
    }

    if (displayHeightKnown) {
      const t = Math.min(1, Math.max(0, (displayHeightMm - 550) / 390));
      railFill.style.height = (12 + t * 76).toFixed(1) + '%';
    } else {
      railFill.style.height = '12%';
    }
    tofHeightEl.textContent = tofHeightKnown ? (s.tof_height_mm / 10).toFixed(1) : '—';
    tofHeightReadout.classList.toggle('is-valid', tofHeightKnown);
    const rightGapKnown = !!s.right_gap_known && typeof s.right_gap_mm === 'number';
    rightGapEl.textContent = rightGapKnown ? (s.right_gap_mm / 10).toFixed(1) : '—';
    rightGapReadout.classList.toggle('is-valid', rightGapKnown);
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
    allowMqtt.checked = sources.mqtt === true;
    const mqtt = s.mqtt && typeof s.mqtt === 'object' ? s.mqtt : {};
    if (mqttDeviceId) {
      mqttDeviceId.textContent = mqtt.device_id
        ? `设备 ID ${mqtt.device_id}`
        : '设备 ID —';
    }
    if (mqttStatus) {
      if (mqtt.connected) {
        mqttStatus.textContent = '已连接 Broker，正在上报状态。';
      } else if (mqtt.client_enabled && mqtt.last_error) {
        mqttStatus.textContent = `未连接：${mqtt.last_error}`;
      } else if (mqtt.client_enabled) {
        mqttStatus.textContent = mqtt.sta_ready
          ? '正在连接 Broker…'
          : '等待 STA 拿到 IP 后再连接 Broker。';
      } else {
        mqttStatus.textContent = '未启用 MQTT Client。';
      }
    }
    if (typeof s.min_height_mm === 'number') {
      const minCm = (s.min_height_mm / 10).toFixed(1);
      if (document.activeElement !== minHeightInput) {
        minHeightInput.value = minCm;
      }
      maxHeightInput.min = String(s.min_height_mm / 10);
      preset1HeightInput.min = String(s.min_height_mm / 10);
      customPresetHeight.min = String(s.min_height_mm / 10);
    }
    if (typeof s.max_height_mm === 'number') {
      const maxCm = (s.max_height_mm / 10).toFixed(1);
      maxHeightBadge.textContent = `最高 ${maxCm} cm`;
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
    updateCustomPresetControlAvailability();
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
  controllerResetButton.onclick = () => void startControllerReset(true);
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
      /* 状态轮询可能在请求完成前改写 checkbox，必须冻结本次用户选择。 */
      const enabled = input.checked;
      input.disabled = true;
      try {
        await api('/api/v1/desk/access', 'POST', {
          source,
          enabled,
        });
        msg.textContent = source === 'panel'
          ? (enabled
              ? '原厂面板操作已开启'
              : '原厂面板已锁定，仅显示高度')
          : `${input.nextElementSibling.textContent}已${enabled ? '开启' : '关闭'}`;
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
  bindSourceToggle(allowMqtt, 'mqtt');

  async function loadMqttConfig() {
    const cfg = await api('/api/v1/mqtt');
    mqttClientEnabled.checked = !!cfg.client_enabled;
    mqttHost.value = cfg.host || '';
    mqttPort.value = String(cfg.port || 1883);
    mqttTls.value = cfg.tls_mode === 'certificate_bundle'
      ? 'certificate_bundle'
      : 'none';
    mqttUser.value = cfg.username || '';
    mqttPassword.value = '';
    mqttPassword.placeholder = cfg.password_configured ? '已配置，留空则保留' : 'MQTT 密码';
    mqttPrefix.value = cfg.discovery_prefix || 'homeassistant';
    mqttDeviceId.textContent = cfg.device_id
      ? `设备 ID ${cfg.device_id}`
      : '设备 ID —';
  }

  mqttForm.onsubmit = async (event) => {
    event.preventDefault();
    mqttMsg.textContent = '正在保存…';
    const body = {
      client_enabled: mqttClientEnabled.checked,
      host: mqttHost.value.trim(),
      port: Number(mqttPort.value),
      tls_mode: mqttTls.value,
      username: mqttUser.value.trim(),
      discovery_prefix: mqttPrefix.value.trim() || 'homeassistant',
    };
    if (mqttPassword.value !== '') {
      body.password = mqttPassword.value;
    }
    try {
      await api('/api/v1/mqtt', 'PUT', body);
      mqttPassword.value = '';
      mqttMsg.textContent = 'MQTT 配置已保存';
      await loadMqttConfig();
      await tick();
    } catch (_) {
      mqttMsg.textContent = 'MQTT 配置无效或保存失败，请检查主机名和端口';
    }
  };

  function setReminderExpanded(expanded) {
    reminderDetails.hidden = !expanded;
    reminderExpandButton.setAttribute('aria-expanded', String(expanded));
    reminderExpandLabel.textContent = expanded ? '收起' : '展开';
  }
  reminderExpandButton.onclick = () => {
    setReminderExpanded(reminderExpandButton.getAttribute('aria-expanded') !== 'true');
  };

  async function reminderAction(action) {
    if (!action) return;
    reminderMsg.textContent = '正在更新…';
    try {
      await api('/api/v1/reminder/action', 'POST', { action });
      reminderMsg.textContent = '';
      await tick();
    } catch (error) {
      reminderMsg.textContent = error.httpStatus === 409
        ? '当前阶段不能执行这个操作，请刷新状态后重试'
        : '番茄时钟操作失败，请检查设备状态';
    }
  }
  reminderQuickStart.onclick = async () => {
    reminderQuickStart.disabled = true;
    await reminderAction('start_focus');
    applyReminderStatus(lastStatus.reminder || {}, lastStatus.audio || {});
  };
  reminderPrimary.onclick = () => reminderAction(reminderPrimary.dataset.action);
  reminderPause.onclick = () => reminderAction(reminderPause.dataset.action);
  reminderSkip.onclick = () => reminderAction('skip');
  reminderStop.onclick = () => reminderAction('stop');
  reminderSnooze.onclick = () => reminderAction('snooze');

  async function saveAudioConfig(values) {
    try {
      await api('/api/v1/reminder/config', 'POST', values);
      reminderMsg.textContent = '语音设置已保存';
      await tick();
    } catch (_) {
      reminderMsg.textContent = '语音设置保存失败';
      await tick();
    }
  }
  audioEnabled.onchange = () => saveAudioConfig({ audio_enabled: audioEnabled.checked });
  audioVolume.oninput = () => { audioVolumeLabel.textContent = `${audioVolume.value}%`; };
  audioVolume.onchange = () => saveAudioConfig({ volume_percent: Number(audioVolume.value) });
  document.querySelectorAll('[data-prompt]').forEach((button) => {
    button.onclick = async () => {
      try {
        await api('/api/v1/audio/action', 'POST', {
          action: 'test_audio',
          prompt_id: button.dataset.prompt,
        });
        reminderMsg.textContent = '正在播放试听语音';
        await tick();
      } catch (_) {
        reminderMsg.textContent = '试听失败，请检查音频分区和扬声器状态';
      }
    };
  });
  audioStop.onclick = () => api('/api/v1/audio/action', 'POST', {
    action: 'stop_audio',
  }).then(tick).catch(() => { reminderMsg.textContent = '停止声音失败'; });

  document.getElementById('reminderConfigForm').onsubmit = async (event) => {
    event.preventDefault();
    const config = {
      focus_minutes: Number(focusMinutes.value),
      short_break_minutes: Number(shortBreakMinutes.value),
      long_break_minutes: Number(longBreakMinutes.value),
      focuses_per_long_break: Number(focusesPerLongBreak.value),
    };
    if (!Number.isInteger(config.focus_minutes) || config.focus_minutes < 1 || config.focus_minutes > 180 ||
        !Number.isInteger(config.short_break_minutes) || config.short_break_minutes < 1 || config.short_break_minutes > 60 ||
        !Number.isInteger(config.long_break_minutes) || config.long_break_minutes < 1 || config.long_break_minutes > 120 ||
        !Number.isInteger(config.focuses_per_long_break) || config.focuses_per_long_break < 1 || config.focuses_per_long_break > 12) {
      reminderMsg.textContent = '提醒时长或长休息间隔超出允许范围';
      return;
    }
    try {
      await api('/api/v1/reminder/config', 'POST', config);
      reminderMsg.textContent = '提醒设置已保存，将从下一阶段生效';
      await tick();
    } catch (_) {
      reminderMsg.textContent = '提醒设置保存失败';
    }
  };

  pairingWindowButton.onclick = async () => {
    pairingWindowButton.disabled = true;
    const open = !!lastBondSnapshot?.pairing_window?.open;
    try {
      await api('/api/v1/bluetooth/pairing-window', open ? 'DELETE' : 'POST');
      bondMsg.textContent = open ? '配对窗口已关闭' : '已开放 120 秒配对窗口';
    } catch (error) {
      bondMsg.textContent = error.httpStatus === 409
        ? '已达到配对上限，请先删除旧设备'
        : '配对窗口设置失败，请重试';
    }
    await refreshBondDevices(true);
  };

  deleteAllBondsButton.onclick = async () => {
    const confirmed = await confirmAction({
      eyebrow: '蓝牙设备',
      title: '删除全部配对设备',
      message: '桌子会先停止，所有在线蓝牙设备会立即断开。此操作需要各设备重新配对。',
      confirmLabel: '删除全部',
      tone: 'danger',
    });
    if (!confirmed) return;
    deleteAllBondsButton.disabled = true;
    bondMsg.textContent = '正在删除全部配对设备…';
    try {
      await api('/api/v1/bluetooth/bonds', 'DELETE');
      bondMsg.textContent = '全部删除请求已受理，正在等待设备断开…';
    } catch (error) {
      bondMsg.textContent = error.httpStatus === 409
        ? '存在删除失败或进行中的设备，请先逐台处理'
        : '删除全部失败，请重试';
    }
    await refreshBondDevices(true);
  };

  document.getElementById('minHeightForm').onsubmit = async (e) => {
    e.preventDefault();
    const msg = document.getElementById('minHeightMsg');
    const minHeightMm = Math.round(Number(minHeightInput.value) * 10);
    const preset1HeightMm = Number(lastStatus.preset1_height_mm);
    if (!Number.isInteger(minHeightMm) || minHeightMm < 550 ||
        minHeightMm > 940 ||
        (Number.isInteger(preset1HeightMm) && minHeightMm > preset1HeightMm)) {
      msg.textContent = '请输入 55.0–94.0 cm，且不能高于当前最低档位';
      return;
    }
    try {
      await api('/api/v1/desk/min-height', 'POST', {
        min_height_mm: minHeightMm,
      });
      msg.textContent = '最低档位高度已保存，不影响手动下降';
      await tick();
    } catch (_) {
      msg.textContent = '保存失败，请先调整低于该值的档位';
    }
  };

  document.getElementById('maxHeightForm').onsubmit = async (e) => {
    e.preventDefault();
    const msg = document.getElementById('maxHeightMsg');
    const maxHeightMm = Math.round(Number(maxHeightInput.value) * 10);
    const minimumHeightMm = lastStatus.min_height_mm ?? 550;
    if (!Number.isInteger(maxHeightMm) || maxHeightMm < minimumHeightMm ||
        maxHeightMm > 940) {
      msg.textContent = `请输入 ${(minimumHeightMm / 10).toFixed(1)}–94.0 cm`;
      return;
    }
    const requestedCm = (maxHeightMm / 10).toFixed(1);
    try {
      await api('/api/v1/desk/max-height', 'POST', { max_height_mm: maxHeightMm });
      msg.textContent = `已保存 ${requestedCm} cm，已用于上升限制`;
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
    if (!Number.isInteger(preset1HeightMm) ||
        !Number.isInteger(preset4HeightMm) ||
        preset1HeightMm < (lastStatus.min_height_mm ?? 550) ||
        preset1HeightMm >= preset4HeightMm ||
        preset4HeightMm > 940) {
      msg.textContent = `请坐需低于站立，档位高度范围为 ${
        ((lastStatus.min_height_mm ?? 550) / 10).toFixed(1)}–94 cm`;
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
      msg.textContent = '保存失败，请检查档位顺序、高度范围或网络';
    }
  };

  document.getElementById('customPresetForm').onsubmit = async (event) => {
    event.preventDefault();
    try {
      await api('/api/v1/desk/height-presets', 'POST', {
        name: DeskHeightPresets.normalizeName(customPresetName.value),
        height_mm: DeskHeightPresets.heightMmFromCm(
          customPresetHeight.value, lastStatus.min_height_mm ?? 550),
      });
      customPresetName.value = '';
      customPresetHeight.value = '';
      customPresetMsg.textContent = '自定义档位已新增';
    } catch (error) {
      customPresetMsg.textContent = heightPresetFailure(
        error, error.message || '新增档位失败');
    }
    await refreshHeightPresets(true);
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
    const confirmed = await confirmAction({
      eyebrow: '设备维护',
      title: '重启 ESP32',
      message: '桌子会先停止。设备重新联网后，当前登录状态会失效，需要重新登录。',
      confirmLabel: '确认重启',
      tone: 'danger',
    });
    if (!confirmed) return;
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
    void refreshBondDevices();
    void refreshHeightPresets();
  }
  tick();
  loadMqttConfig().catch(() => {
    mqttMsg.textContent = '无法读取 MQTT 配置';
  });
  setInterval(tick, 250);
})();
