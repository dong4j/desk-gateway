/** 番茄页面的纯展示规则；不维护计时器，也不推测 ESP 状态。 */

import type { ReminderAction, ReminderSnapshot } from './types';

export interface ReminderPrimaryAction {
  action: ReminderAction;
  label: string;
}

export function reminderPrimaryAction(
  reminder: ReminderSnapshot,
): ReminderPrimaryAction {
  switch (reminder.state) {
    case 'idle':
      return { action: 'start_focus', label: '开始专注' };
    case 'running':
      return { action: 'pause', label: '暂停' };
    case 'paused':
      return { action: 'resume', label: '继续' };
    case 'waiting':
      return reminder.phase === 'focus'
        ? { action: 'start_focus', label: '开始下一轮专注' }
        : { action: 'start_break', label: '开始休息' };
    case 'snoozed':
      return { action: 'stop', label: '结束稍后提醒' };
  }
}

export function reminderPhaseLabel(reminder: ReminderSnapshot): string {
  if (reminder.state === 'idle') return '准备开始';
  if (reminder.phase === 'focus') return '专注';
  return reminder.phase === 'long_break' ? '长休息' : '短休息';
}

export function formatRemaining(remainingSec: number): string {
  const safe = Math.max(0, Math.floor(remainingSec));
  const minutes = Math.floor(safe / 60);
  const seconds = safe % 60;
  return `${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}`;
}
