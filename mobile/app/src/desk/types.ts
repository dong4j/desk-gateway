/** Desk Gateway GATT v1 对外状态类型。 */

export type DeskMotion =
  | 'idle'
  | 'moving_up'
  | 'moving_down'
  | 'goto_preset'
  | 'error';

export interface DeskState {
  protocolVersion: number;
  motion: DeskMotion;
  heightKnown: boolean;
  heightSimulated: boolean;
  childLock: boolean;
  bluetoothAllowed: boolean;
  upwardBlocked: boolean;
  controllerResetSupported: boolean;
  controllerResetActive: boolean;
  controllerResetRecommended: boolean;
  heightMm: number | null;
  maxHeightMm: number;
}

/** Config characteristic 返回的设备持久化设置。 */
export interface DeskConfig {
  protocolVersion: number;
  childLock: boolean;
  childLockReason: 'none' | 'manual' | 'auto_away' | 'unknown';
  restAllowed: boolean;
  bluetoothAllowed: boolean;
  panelAllowed: boolean;
  minHeightMm: number;
  maxHeightMm: number;
  preset1HeightMm: number;
  preset4HeightMm: number;
}

export type DeskConfigField =
  | 'child_lock'
  | 'rest_allowed'
  | 'bluetooth_allowed'
  | 'panel_allowed'
  | 'min_height_mm'
  | 'max_height_mm'
  | 'preset1_height_mm'
  | 'preset4_height_mm';

export interface DeskPeripheral {
  id: string;
  name: string | null;
  rssi: number;
}

export type ReminderState = 'idle' | 'running' | 'paused' | 'waiting' | 'snoozed';
export type ReminderPhase = 'focus' | 'short_break' | 'long_break';
export type ReminderAlarmReason = 'none' | 'focus_done' | 'break_done';
export type ReminderAction =
  | 'start_focus'
  | 'start_break'
  | 'pause'
  | 'resume'
  | 'skip'
  | 'stop'
  | 'snooze';

export interface ReminderConfig {
  focusMinutes: number;
  shortBreakMinutes: number;
  longBreakMinutes: number;
  focusesPerLongBreak: number;
}

/** 计时值只来自 ESP 快照；移动端不得用本地 deadline 覆盖它。 */
export interface ReminderSnapshot {
  protocolVersion: number;
  available: boolean;
  state: ReminderState;
  phase: ReminderPhase;
  alarmReason: ReminderAlarmReason;
  remainingSec: number;
  completedFocusCount: number;
  config: ReminderConfig;
  lastError: string | null;
}

export interface AudioSnapshot {
  available: boolean;
  enabled: boolean;
  playing: boolean;
  volumePercent: number;
  voicePack: string;
  currentPrompt: string | null;
  lastError: string | null;
}
