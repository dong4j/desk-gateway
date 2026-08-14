/** 番茄时钟页面规则测试：设备快照是唯一事实来源。 */
const assert = require('node:assert/strict');
const Reminder = require('../www/reminder-control.js');

assert.equal(Reminder.formatTime(1500), '25:00');
assert.equal(Reminder.formatTime(-1), '00:00');

let view = Reminder.viewModel(
  { available: true, state: 'idle', phase: 'focus', remaining_sec: 0 },
  { available: true, enabled: true, volume_percent: 60 });
assert.equal(view.primaryAction, 'start_focus');
assert.equal(view.primaryLabel, '开始专注');

view = Reminder.viewModel(
  { state: 'waiting', phase: 'long_break', alarm_reason: 'focus_done' },
  { available: true, playing: true, current_prompt: 'focus_done' });
assert.equal(view.primaryAction, 'start_break');
assert.equal(view.primaryLabel, '开始休息');
assert.equal(view.canSnooze, true);
assert.match(view.audioStatus, /focus_done/);

view = Reminder.viewModel({ state: 'paused', phase: 'focus' }, {});
assert.equal(view.pauseAction, 'resume');
assert.equal(view.canSkip, true);

console.log('reminder-control.test: ok');
