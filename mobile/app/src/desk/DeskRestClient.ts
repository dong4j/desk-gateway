/**
 * Desk Gateway 局域网 REST 客户端。
 *
 * 该实现把 REST JSON 映射为与 BLE 完全相同的快照，并复用 X-Desk-Key 认证。
 * 轮询只负责同步状态；长按续期和松手 STOP 仍由 DeskHoldController 统一处理。
 */

import { DeskCommand, type DeskCommandValue } from './commands';
import {
  DESK_DEFAULT_SIT_HEIGHT_MM,
  DESK_DEFAULT_STAND_HEIGHT_MM,
  DESK_MIN_HEIGHT_MM,
  DESK_MAX_HEIGHT_MM,
} from './heightPresentation';
import type {
  DeskClient,
  DeskClientSnapshot,
  DeskSnapshotListener,
  DeskUnsubscribe,
} from './DeskClient';
import type {
  AudioSnapshot,
  DeskMotion,
  ReminderAction,
  ReminderAlarmReason,
  ReminderPhase,
  ReminderSnapshot,
  ReminderState,
} from './types';

type FetchLike = (
  input: string,
  init?: RequestInit,
) => Promise<Response>;

interface RestStatus {
  status?: string;
  height_mm?: number | null;
  height_known?: boolean;
  height_sim?: boolean;
  child_lock?: boolean;
  child_lock_reason?: string;
  upward_blocked?: boolean;
  controller_reset_supported?: boolean;
  controller_reset_active?: boolean;
  controller_reset_recommended?: boolean;
  min_height_mm?: number;
  max_height_mm?: number;
  preset1_height_mm?: number;
  preset4_height_mm?: number;
  control_sources?: {
    rest?: boolean;
    bluetooth?: boolean;
    panel?: boolean;
  };
  driver?: string;
  build_date?: string;
  build_time?: string;
  build_id?: string;
  git_version?: string;
  reminder?: {
    available?: boolean;
    state?: string;
    phase?: string;
    alarm_reason?: string;
    remaining_sec?: number;
    completed_focus_count?: number;
    focus_minutes?: number;
    short_break_minutes?: number;
    long_break_minutes?: number;
    focuses_per_long_break?: number;
    last_error?: string | null;
  };
  audio?: {
    available?: boolean;
    enabled?: boolean;
    playing?: boolean;
    volume_percent?: number;
    voice_pack?: string;
    current_prompt?: string | null;
    last_error?: string | null;
  };
}

export interface ReminderConfigPatch {
  focusMinutes?: number;
  shortBreakMinutes?: number;
  longBreakMinutes?: number;
  focusesPerLongBreak?: number;
  audioEnabled?: boolean;
  volumePercent?: number;
}

export type ReminderPromptId =
  | 'focus_done'
  | 'break_done'
  | 'snooze_done'
  | 'attention_chime';

export type DeskBondKind = 'unknown' | 'watchos' | 'ios' | 'android';
export type DeskBondDeleteState = 'idle' | 'pending' | 'failed';

export interface DeskBondDevice {
  id: string;
  kind: DeskBondKind;
  label: string;
  alias: string;
  connected: boolean;
  controlling: boolean;
  delete_state: DeskBondDeleteState;
  delete_error: string | null;
}

export interface DeskBondSnapshot {
  devices: DeskBondDevice[];
  capacity: number;
  pairing_window: {
    open: boolean;
    remaining_seconds: number;
  };
  auto_child_lock?: {
    enabled: boolean;
    device_id: string;
    detector_online: boolean;
  };
}

export interface DeskHeightPreset {
  id: string;
  name: string;
  height_mm: number;
  built_in: boolean;
  deletable: boolean;
}

export interface DeskHeightPresetSnapshot {
  presets: DeskHeightPreset[];
  custom_count: number;
  custom_capacity: number;
}

const REQUEST_TIMEOUT_MS = 3_000;
const MOVING_POLL_MS = 250;
const IDLE_POLL_MS = 1_000;

export class DeskRestClient implements DeskClient {
  private snapshot: DeskClientSnapshot = {
    phase: 'uninitialized',
    transport: 'wifi',
    peripheral: null,
    deskState: null,
    deskConfig: null,
    firmwareRevision: null,
    reminder: null,
    audio: null,
    error: null,
  };
  private listeners = new Set<DeskSnapshotListener>();
  private pollTimer: ReturnType<typeof setTimeout> | null = null;
  private pollFailures = 0;
  private baseUrl = '';
  private restKey = '';

  constructor(
    private readonly fetchImpl: FetchLike = fetch,
  ) {}

  configure(host: string, restKey: string): void {
    const normalizedHost = host.trim().replace(/\/+$/, '');
    this.baseUrl = normalizedHost
      ? /^https?:\/\//i.test(normalizedHost)
        ? normalizedHost
        : `http://${normalizedHost}`
      : '';
    this.restKey = restKey;
  }

  subscribe(listener: DeskSnapshotListener): DeskUnsubscribe {
    this.listeners.add(listener);
    listener(this.snapshot);
    return () => this.listeners.delete(listener);
  }

  async initialize(): Promise<void> {
    if (this.snapshot.phase === 'uninitialized' || this.snapshot.phase === 'error') {
      this.update({ phase: 'idle', error: null });
    }
  }

  async connect(): Promise<void> {
    this.ensureConfigured();

    this.stopPolling();
    this.update({
      phase: 'connecting',
      peripheral: {
        id: this.baseUrl,
        name: 'DeskGateway',
        rssi: 0,
      },
      error: null,
    });
    try {
      await this.refreshStatus();
      this.pollFailures = 0;
      this.update({ phase: 'ready', error: null });
      this.schedulePoll();
    } catch (error) {
      throw this.connectionError(errorMessage(error));
    }
  }

  /**
   * BLE 作为桌控主通道时，按需刷新仅存在于 REST 状态中的音频管理字段。
   * 该调用不会启动 REST 轮询，也不会把 REST 切换为当前控制通道。
   */
  async refreshManagementSnapshot(): Promise<void> {
    this.ensureConfigured();
    await this.refreshStatus();
  }

  async sendCommand(command: DeskCommandValue): Promise<void> {
    const endpoint: Record<DeskCommandValue, string> = {
      [DeskCommand.Stop]: '/api/v1/desk/stop',
      [DeskCommand.HoldUp]: '/api/v1/desk/up',
      [DeskCommand.HoldDown]: '/api/v1/desk/down',
      [DeskCommand.Preset1]: '/api/v1/desk/preset/1/goto',
      [DeskCommand.Preset4]: '/api/v1/desk/preset/4/goto',
    };
    await this.operation(() => this.request(endpoint[command], { method: 'POST' }));
  }

  async setChildLock(enabled: boolean): Promise<void> {
    await this.writeAndRefresh('/api/v1/desk/child-lock', { enabled });
  }

  async sendPresenceHeartbeat(deviceId: string): Promise<void> {
    this.ensureBondId(deviceId);
    await this.operation(() => this.request('/api/v1/desk/presence', {
      method: 'POST',
      body: JSON.stringify({ device_id: deviceId }),
    }));
  }

  async setAutoChildLock(enabled: boolean, deviceId: string): Promise<void> {
    this.ensureBondId(deviceId);
    await this.request('/api/v1/desk/auto-child-lock', {
      method: 'POST',
      body: JSON.stringify({ enabled, device_id: deviceId }),
    });
  }

  async setSourceEnabled(
    source: 'rest' | 'bluetooth' | 'panel',
    enabled: boolean,
  ): Promise<void> {
    await this.writeAndRefresh('/api/v1/desk/access', { source, enabled });
  }

  async setMaxHeightMm(maxHeightMm: number): Promise<void> {
    await this.writeAndRefresh('/api/v1/desk/max-height', {
      max_height_mm: maxHeightMm,
    });
  }

  async setMinHeightMm(minHeightMm: number): Promise<void> {
    await this.writeAndRefresh('/api/v1/desk/min-height', {
      min_height_mm: minHeightMm,
    });
  }

  async setPresetHeightsMm(
    preset1HeightMm: number,
    preset4HeightMm: number,
  ): Promise<void> {
    await this.writeAndRefresh('/api/v1/desk/presets', {
      preset1_height_mm: preset1HeightMm,
      preset4_height_mm: preset4HeightMm,
    });
  }

  async restartGateway(): Promise<void> {
    await this.operation(() =>
      this.request('/api/v1/system/restart', { method: 'POST' }),
    );
    this.stopPolling();
    this.update({ phase: 'disconnected' });
  }

  async resetController(): Promise<void> {
    await this.operation(() => this.request(
      '/api/v1/desk/controller/reset',
      { method: 'POST' },
    ));
  }

  async performReminderAction(action: ReminderAction): Promise<void> {
    await this.writeAndRefresh('/api/v1/reminder/action', { action });
  }

  async updateReminderConfig(patch: ReminderConfigPatch): Promise<void> {
    const body: Record<string, unknown> = {};
    if (patch.focusMinutes !== undefined) body.focus_minutes = patch.focusMinutes;
    if (patch.shortBreakMinutes !== undefined) {
      body.short_break_minutes = patch.shortBreakMinutes;
    }
    if (patch.longBreakMinutes !== undefined) {
      body.long_break_minutes = patch.longBreakMinutes;
    }
    if (patch.focusesPerLongBreak !== undefined) {
      body.focuses_per_long_break = patch.focusesPerLongBreak;
    }
    if (patch.audioEnabled !== undefined) body.audio_enabled = patch.audioEnabled;
    if (patch.volumePercent !== undefined) body.volume_percent = patch.volumePercent;
    await this.writeAndRefresh('/api/v1/reminder/config', body);
  }

  async previewReminderAudio(promptId: ReminderPromptId): Promise<void> {
    await this.operation(() => this.request('/api/v1/audio/action', {
      method: 'POST',
      body: JSON.stringify({ action: 'test_audio', prompt_id: promptId }),
    }));
    await this.refreshStatus();
  }

  async stopReminderAudio(): Promise<void> {
    await this.operation(() => this.request('/api/v1/audio/action', {
      method: 'POST',
      body: JSON.stringify({ action: 'stop_audio' }),
    }));
    await this.refreshStatus();
  }

  async getHeightPresets(): Promise<DeskHeightPresetSnapshot> {
    this.ensureConfigured();
    return this.request<DeskHeightPresetSnapshot>('/api/v1/desk/height-presets');
  }

  async createHeightPreset(name: string, heightMm: number): Promise<void> {
    this.ensureConfigured();
    await this.request('/api/v1/desk/height-presets', {
      method: 'POST',
      body: JSON.stringify({ name, height_mm: heightMm }),
    });
  }

  async updateHeightPreset(
    id: string,
    name: string,
    heightMm: number,
  ): Promise<void> {
    this.ensureHeightPresetId(id);
    await this.request(`/api/v1/desk/height-presets/${encodeURIComponent(id)}`, {
      method: 'POST',
      body: JSON.stringify({ name, height_mm: heightMm }),
    });
  }

  async deleteHeightPreset(id: string): Promise<void> {
    this.ensureHeightPresetId(id);
    await this.request(`/api/v1/desk/height-presets/${encodeURIComponent(id)}`, {
      method: 'DELETE',
    });
  }

  async gotoHeightPreset(id: string): Promise<void> {
    this.ensureHeightPresetId(id);
    await this.request(
      `/api/v1/desk/height-presets/${encodeURIComponent(id)}/goto`,
      { method: 'POST' },
    );
  }

  /** Bond 管理始终走已认证 REST，即使当前控制通道是 BLE。 */
  async getBluetoothBonds(): Promise<DeskBondSnapshot> {
    this.ensureConfigured();
    return this.request<DeskBondSnapshot>('/api/v1/bluetooth/bonds');
  }

  async setBluetoothPairingWindow(open: boolean): Promise<void> {
    this.ensureConfigured();
    await this.request('/api/v1/bluetooth/pairing-window', {
      method: open ? 'POST' : 'DELETE',
    });
  }

  async deleteBluetoothBond(id: string): Promise<void> {
    this.ensureConfigured();
    this.ensureBondId(id);
    await this.request(`/api/v1/bluetooth/bonds/${encodeURIComponent(id)}`, {
      method: 'DELETE',
    });
  }

  async deleteAllBluetoothBonds(): Promise<void> {
    this.ensureConfigured();
    await this.request('/api/v1/bluetooth/bonds', { method: 'DELETE' });
  }

  async renameBluetoothBond(id: string, alias: string): Promise<void> {
    this.ensureConfigured();
    if (!/^bond_[0-9a-f]{12}$/.test(id)) {
      throw new Error('无效的蓝牙配对设备 ID');
    }
    await this.request(
      `/api/v1/bluetooth/bonds/${encodeURIComponent(id)}/alias`,
      {
        method: 'POST',
        body: JSON.stringify({ alias }),
      },
    );
  }

  async disconnect(): Promise<void> {
    this.stopPolling();
    if (this.snapshot.phase === 'ready') {
      await this.sendCommand(DeskCommand.Stop).catch(() => undefined);
    }
    this.update({
      phase: 'disconnected',
      deskState: null,
      deskConfig: null,
      firmwareRevision: null,
      reminder: null,
      audio: null,
      error: null,
    });
  }

  dispose(): void {
    this.stopPolling();
    this.listeners.clear();
  }

  private async writeAndRefresh(
    path: string,
    body: Record<string, unknown>,
  ): Promise<void> {
    await this.operation(() => this.request(path, {
      method: 'POST',
      body: JSON.stringify(body),
    }));
    await this.refreshStatus();
  }

  private async refreshStatus(): Promise<void> {
    const status = await this.request<RestStatus>('/api/v1/desk/status');
    const minHeightMm = integerOr(status.min_height_mm, DESK_MIN_HEIGHT_MM);
    const maxHeightMm = integerOr(status.max_height_mm, DESK_MAX_HEIGHT_MM);
    const heightKnown = status.height_known === true &&
      typeof status.height_mm === 'number';
    const sources = status.control_sources ?? {};
    const childLock = status.child_lock === true;
    this.update({
      deskState: {
        protocolVersion: 1,
        motion: parseMotion(status.status),
        heightKnown,
        heightSimulated: status.height_sim === true,
        childLock,
        bluetoothAllowed: sources.bluetooth !== false,
        upwardBlocked: status.upward_blocked === true,
        controllerResetSupported: status.controller_reset_supported === true,
        controllerResetActive: status.controller_reset_active === true,
        controllerResetRecommended:
          status.controller_reset_recommended === true,
        heightMm: heightKnown ? status.height_mm! : null,
        maxHeightMm,
      },
      deskConfig: {
        protocolVersion: 3,
        childLock,
        childLockReason: parseChildLockReason(status.child_lock_reason, childLock),
        restAllowed: sources.rest !== false,
        bluetoothAllowed: sources.bluetooth !== false,
        panelAllowed: sources.panel !== false,
        minHeightMm,
        maxHeightMm,
        preset1HeightMm: integerOr(
          status.preset1_height_mm,
          DESK_DEFAULT_SIT_HEIGHT_MM,
        ),
        preset4HeightMm: integerOr(
          status.preset4_height_mm,
          Math.min(DESK_DEFAULT_STAND_HEIGHT_MM, maxHeightMm),
        ),
      },
      firmwareRevision: firmwareRevision(status),
      reminder: parseReminder(status.reminder),
      audio: parseAudio(status.audio),
      error: null,
    });
  }

  private schedulePoll(): void {
    this.stopPolling();
    if (this.snapshot.phase !== 'ready') {
      return;
    }
    const moving = this.snapshot.deskState?.motion !== 'idle';
    this.pollTimer = setTimeout(() => {
      this.pollTimer = null;
      void this.pollOnce();
    }, moving ? MOVING_POLL_MS : IDLE_POLL_MS);
  }

  private async pollOnce(): Promise<void> {
    try {
      await this.refreshStatus();
      this.pollFailures = 0;
      this.schedulePoll();
    } catch (error) {
      this.pollFailures += 1;
      if (this.pollFailures >= 3) {
        this.update({
          phase: 'disconnected',
          error: `局域网连接中断：${errorMessage(error)}`,
        });
        return;
      }
      this.schedulePoll();
    }
  }

  private stopPolling(): void {
    if (this.pollTimer !== null) {
      clearTimeout(this.pollTimer);
      this.pollTimer = null;
    }
  }

  private async operation<T>(run: () => Promise<T>): Promise<T> {
    try {
      const result = await run();
      this.update({ error: null });
      return result;
    } catch (error) {
      this.update({ error: errorMessage(error) });
      throw error;
    }
  }

  private async request<T = unknown>(
    path: string,
    init: RequestInit = {},
  ): Promise<T> {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), REQUEST_TIMEOUT_MS);
    try {
      const response = await this.fetchImpl(`${this.baseUrl}${path}`, {
        ...init,
        signal: controller.signal,
        headers: {
          'Content-Type': 'application/json',
          'X-Desk-Key': this.restKey,
          ...init.headers,
        },
      });
      const text = await response.text();
      const payload = text ? JSON.parse(text) as Record<string, unknown> : {};
      if (!response.ok) {
        const code = typeof payload.error === 'string'
          ? payload.error
          : typeof payload.err === 'string'
            ? payload.err
          : `HTTP ${response.status}`;
        const reason = typeof payload.reason === 'string'
          ? ` (${payload.reason})`
          : '';
        throw new Error(`${code}${reason}`);
      }
      return payload as T;
    } finally {
      clearTimeout(timer);
    }
  }

  private connectionError(message: string): Error {
    const error = new Error(message);
    this.update({ phase: 'error', error: message });
    return error;
  }

  private ensureConfigured(): void {
    if (!this.baseUrl) {
      throw this.connectionError('请先设置局域网网关地址');
    }
    if (!this.restKey) {
      throw this.connectionError('请先设置 REST 密码');
    }
  }

  private ensureHeightPresetId(id: string): void {
    this.ensureConfigured();
    if (id !== 'sit' && id !== 'stand' &&
        !/^custom_[0-9a-f]{8}$/.test(id)) {
      throw new Error('无效的高度档位 ID');
    }
  }


  private ensureBondId(id: string): void {
    this.ensureConfigured();
    if (!/^bond_[0-9a-f]{12}$/.test(id)) {
      throw new Error('无效的蓝牙配对设备 ID');
    }
  }

  private update(patch: Partial<DeskClientSnapshot>): void {
    this.snapshot = { ...this.snapshot, ...patch };
    for (const listener of this.listeners) {
      listener(this.snapshot);
    }
  }
}

function parseChildLockReason(
  value: string | undefined,
  childLock: boolean,
): 'none' | 'manual' | 'auto_away' | 'unknown' {
  if (!childLock) return 'none';
  if (value === 'manual' || value === 'auto_away') return value;
  return 'unknown';
}

function parseMotion(status: string | undefined): DeskMotion {
  if (status === 'moving_up' || status === 'moving_down' ||
      status === 'goto_preset' || status === 'error') {
    return status;
  }
  return 'idle';
}

function integerOr(value: number | undefined, fallback: number): number {
  return Number.isInteger(value) ? value! : fallback;
}

function firmwareRevision(status: RestStatus): string | null {
  const date = status.build_date?.trim();
  const time = status.build_time?.trim();
  if (!date || !time) {
    return null;
  }
  const gitVersion = status.git_version?.trim();
  if (gitVersion) {
    return `${date} ${time} @ ${gitVersion}`;
  }
  const buildId = status.build_id?.trim();
  return `${date} ${time}${buildId ? ` # ${buildId}` : ''}`;
}

function errorMessage(error: unknown): string {
  if (error instanceof Error && error.name === 'AbortError') {
    return '请求超时';
  }
  return error instanceof Error ? error.message : String(error);
}

function parseReminder(value: RestStatus['reminder']): ReminderSnapshot | null {
  if (!value) return null;
  return {
    protocolVersion: 1,
    available: value.available === true,
    state: oneOf<ReminderState>(
      value.state,
      ['idle', 'running', 'paused', 'waiting', 'snoozed'],
      'idle',
    ),
    phase: oneOf<ReminderPhase>(
      value.phase,
      ['focus', 'short_break', 'long_break'],
      'focus',
    ),
    alarmReason: oneOf<ReminderAlarmReason>(
      value.alarm_reason,
      ['none', 'focus_done', 'break_done'],
      'none',
    ),
    remainingSec: integerOr(value.remaining_sec, 0),
    completedFocusCount: integerOr(value.completed_focus_count, 0),
    config: {
      focusMinutes: integerOr(value.focus_minutes, 25),
      shortBreakMinutes: integerOr(value.short_break_minutes, 5),
      longBreakMinutes: integerOr(value.long_break_minutes, 15),
      focusesPerLongBreak: integerOr(value.focuses_per_long_break, 4),
    },
    lastError: typeof value.last_error === 'string' ? value.last_error : null,
  };
}

function parseAudio(value: RestStatus['audio']): AudioSnapshot | null {
  if (!value) return null;
  return {
    available: value.available === true,
    enabled: value.enabled === true,
    playing: value.playing === true,
    volumePercent: integerOr(value.volume_percent, 0),
    voicePack: typeof value.voice_pack === 'string' ? value.voice_pack : '',
    currentPrompt:
      typeof value.current_prompt === 'string' ? value.current_prompt : null,
    lastError: typeof value.last_error === 'string' ? value.last_error : null,
  };
}

function oneOf<T extends string>(
  value: unknown,
  allowed: readonly T[],
  fallback: T,
): T {
  return typeof value === 'string' && allowed.includes(value as T)
    ? value as T
    : fallback;
}
