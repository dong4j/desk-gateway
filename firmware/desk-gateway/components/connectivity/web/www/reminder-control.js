/**
 * Desk Gateway 番茄时钟展示规则。
 *
 * 纯函数独立于 DOM，确保页面按钮文案只由设备状态决定，浏览器不创建
 * 第二套番茄状态机。
 */
(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  root.DeskReminderControl = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, () => {
  const PHASE_LABEL = {
    focus: '专注',
    short_break: '短休息',
    long_break: '长休息',
  };

  const PREVIEW_PLAYING_HINT = '正在播放试听语音';

  function formatTime(seconds) {
    const total = Math.max(0, Math.floor(Number(seconds) || 0));
    return `${String(Math.floor(total / 60)).padStart(2, '0')}:${String(total % 60).padStart(2, '0')}`;
  }

  /**
   * 试听成功提示跟设备 playing 走。reminderMsg 不是快照字段，
   * 只在 POST 成功时写入的话，播放结束后会一直挂着。
   */
  function previewActionHint(currentHint, playing) {
    if (playing) {
      return PREVIEW_PLAYING_HINT;
    }
    if (currentHint === PREVIEW_PLAYING_HINT) {
      return '';
    }
    return currentHint || '';
  }

  function viewModel(reminder = {}, audio = {}) {
    const state = reminder.state || 'idle';
    const phase = reminder.phase || 'focus';
    const waiting = state === 'waiting';
    const running = state === 'running';
    const paused = state === 'paused';
    return {
      available: reminder.available !== false,
      phaseLabel: PHASE_LABEL[phase] || '专注',
      timeText: formatTime(reminder.remaining_sec),
      statusText: state === 'idle' ? '还没有开始' :
        running ? '正在计时' : paused ? '已暂停' :
          state === 'snoozed' ? '提醒已延后' : '等待你的确认',
      primaryAction: state === 'idle' || (waiting && phase === 'focus')
        ? 'start_focus' : waiting ? 'start_break' : null,
      primaryLabel: state === 'idle' || (waiting && phase === 'focus')
        ? '开始专注' : '开始休息',
      pauseAction: running ? 'pause' : paused ? 'resume' : null,
      pauseLabel: paused ? '继续' : '暂停',
      canSkip: running || paused,
      canStop: state !== 'idle',
      canSnooze: waiting && reminder.alarm_reason !== 'none',
      audioAvailable: audio.available !== false,
      audioStatus: !audio.available ? (audio.last_error || '音频不可用') :
        audio.playing ? `正在播放 ${audio.current_prompt || ''}` :
          audio.enabled && audio.volume_percent > 0 ? '语音提醒已开启' : '语音提醒已关闭',
    };
  }

  return { formatTime, viewModel, previewActionHint, PREVIEW_PLAYING_HINT };
});
