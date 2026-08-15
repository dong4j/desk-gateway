/**
 * Desk Gateway 局域网 REST 客户端。
 *
 * 该实现把 REST JSON 映射为与 BLE 完全相同的快照，并复用 X-Desk-Key 认证。
 * 轮询只负责同步状态；长按续期和松手 STOP 仍由 DeskHoldController 统一处理。
 */

import { DeskCommand, type DeskCommandValue } from './commands';
import type {
  DeskClient,
  DeskClientSnapshot,
  DeskSnapshotListener,
  DeskUnsubscribe,
} from './DeskClient';
import type { DeskMotion } from './types';

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
}

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
    const maxHeightMm = integerOr(status.max_height_mm, 1020);
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
        protocolVersion: 2,
        childLock,
        childLockReason: parseChildLockReason(status.child_lock_reason, childLock),
        restAllowed: sources.rest !== false,
        bluetoothAllowed: sources.bluetooth !== false,
        panelAllowed: sources.panel !== false,
        maxHeightMm,
        preset1HeightMm: integerOr(status.preset1_height_mm, 640),
        preset4HeightMm: integerOr(status.preset4_height_mm, Math.min(1020, maxHeightMm)),
      },
      firmwareRevision: firmwareRevision(status),
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
