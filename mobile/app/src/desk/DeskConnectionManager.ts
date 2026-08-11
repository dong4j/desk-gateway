/**
 * BLE 优先的 Desk Gateway 通道协调器。
 *
 * 自动模式只在连接失败或 BLE 断开后切到 REST；切换前由上层取消 HOLD，且协调器
 * 从不重放运动命令，避免“恢复连接”等同于“恢复运动”。
 */

import type { DeskCommandValue } from './commands';
import type {
  DeskClient,
  DeskClientSnapshot,
  DeskConnectionSettings,
  DeskSnapshotListener,
  DeskUnsubscribe,
} from './DeskClient';
import { DeskRestClient } from './DeskRestClient';

const initialSnapshot: DeskClientSnapshot = {
  phase: 'uninitialized',
  transport: null,
  peripheral: null,
  deskState: null,
  deskConfig: null,
  firmwareRevision: null,
  error: null,
};

export class DeskConnectionManager implements DeskClient {
  private snapshot = initialSnapshot;
  private listeners = new Set<DeskSnapshotListener>();
  private activeClient: DeskClient | null = null;
  private activeUnsubscribe: DeskUnsubscribe | null = null;
  private manualDisconnect = false;
  private connectPromise: Promise<void> | null = null;
  private fallbackPromise: Promise<void> | null = null;

  constructor(
    private readonly bleClient: DeskClient,
    private readonly restClient: DeskRestClient,
    private settings: DeskConnectionSettings,
  ) {
    this.restClient.configure(settings.restHost, settings.restKey);
  }

  configure(settings: DeskConnectionSettings): void {
    this.settings = settings;
    this.restClient.configure(settings.restHost, settings.restKey);
  }

  subscribe(listener: DeskSnapshotListener): DeskUnsubscribe {
    this.listeners.add(listener);
    listener(this.snapshot);
    return () => this.listeners.delete(listener);
  }

  async initialize(): Promise<void> {
    this.update({ phase: 'idle', error: null });
  }

  connect(): Promise<void> {
    if (this.connectPromise) {
      return this.connectPromise;
    }
    this.connectPromise = this.connectInternal().finally(() => {
      this.connectPromise = null;
    });
    return this.connectPromise;
  }

  private async connectInternal(): Promise<void> {
    this.manualDisconnect = false;
    await this.detachActive(true);

    if (this.settings.mode === 'ble') {
      await this.activate(this.bleClient);
      return;
    }
    if (this.settings.mode === 'wifi') {
      await this.activate(this.restClient);
      return;
    }

    let bleError: unknown;
    try {
      await this.activate(this.bleClient);
      return;
    } catch (error) {
      bleError = error;
      await this.detachActive(true);
    }

    try {
      await this.activate(this.restClient);
    } catch (restError) {
      const message = `BLE：${errorMessage(bleError)}；Wi-Fi：${errorMessage(restError)}`;
      this.update({ phase: 'error', error: message });
      throw new Error(message);
    }
  }

  sendCommand(command: DeskCommandValue): Promise<void> {
    return this.requireActive().sendCommand(command);
  }

  setChildLock(enabled: boolean): Promise<void> {
    return this.requireActive().setChildLock(enabled);
  }

  setSourceEnabled(
    source: 'rest' | 'bluetooth' | 'panel',
    enabled: boolean,
  ): Promise<void> {
    return this.requireActive().setSourceEnabled(source, enabled);
  }

  setMaxHeightMm(maxHeightMm: number): Promise<void> {
    return this.requireActive().setMaxHeightMm(maxHeightMm);
  }

  setPresetHeightsMm(
    preset1HeightMm: number,
    preset4HeightMm: number,
  ): Promise<void> {
    return this.requireActive().setPresetHeightsMm(
      preset1HeightMm,
      preset4HeightMm,
    );
  }

  restartGateway(): Promise<void> {
    return this.requireActive().restartGateway();
  }

  async disconnect(): Promise<void> {
    this.manualDisconnect = true;
    await this.detachActive(true);
    this.update({
      phase: 'disconnected',
      transport: null,
      peripheral: null,
      deskState: null,
      deskConfig: null,
      firmwareRevision: null,
      error: null,
    });
  }

  dispose(): void {
    this.manualDisconnect = true;
    this.activeUnsubscribe?.();
    this.activeUnsubscribe = null;
    this.activeClient = null;
    this.bleClient.dispose();
    this.restClient.dispose();
    this.listeners.clear();
  }

  private async activate(client: DeskClient): Promise<void> {
    this.activeUnsubscribe?.();
    this.activeClient = client;
    this.activeUnsubscribe = client.subscribe((snapshot) => {
      if (this.activeClient !== client) {
        return;
      }
      this.update(snapshot);
      if (
        client === this.bleClient &&
        snapshot.phase === 'disconnected' &&
        this.settings.mode === 'auto' &&
        !this.manualDisconnect
      ) {
        void this.fallbackToRest();
      }
    });
    await client.initialize();
    await client.connect();
  }

  /** BLE 断开时只建立 REST 会话，不恢复之前的 HOLD 或档位动作。 */
  private fallbackToRest(): Promise<void> {
    if (this.fallbackPromise) {
      return this.fallbackPromise;
    }
    this.fallbackPromise = this.activate(this.restClient)
      .catch((error) => {
        this.update({
          phase: 'error',
          error: `BLE 已断开，Wi-Fi 回退失败：${errorMessage(error)}`,
        });
      })
      .finally(() => {
        this.fallbackPromise = null;
      });
    return this.fallbackPromise;
  }

  private async detachActive(disconnect: boolean): Promise<void> {
    const client = this.activeClient;
    this.activeUnsubscribe?.();
    this.activeUnsubscribe = null;
    this.activeClient = null;
    if (disconnect && client) {
      await client.disconnect().catch(() => undefined);
    }
  }

  private requireActive(): DeskClient {
    if (!this.activeClient || this.snapshot.phase !== 'ready') {
      throw new Error('DeskGateway is not connected');
    }
    return this.activeClient;
  }

  private update(patch: Partial<DeskClientSnapshot>): void {
    this.snapshot = { ...this.snapshot, ...patch };
    for (const listener of this.listeners) {
      listener(this.snapshot);
    }
  }
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}
