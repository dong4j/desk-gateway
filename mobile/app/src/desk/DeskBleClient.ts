/**
 * Desk Gateway GATT v1 客户端。
 *
 * 该类负责 BLE 会话和串行 Write，不包含 React 状态或正式 UI。所有命令最终仍由
 * ESP32 的 desk_core 执行童锁、来源权限和安全上限裁决。
 */

import type { BleAdapter, Unsubscribe } from '../ble/BleAdapter';
import {
  DeskCommand,
  encodeDeskCommand,
  type DeskCommandValue,
} from './commands';
import {
  DESK_ADVERTISING_NAME,
  DESK_CLIENT_INFO_UUID,
  DESK_COMMAND_UUID,
  DESK_CONFIG_UUID,
  DESK_PRESENCE_UUID,
  DESK_SERVICE_UUID,
  DESK_STATE_UUID,
  DESK_SYSTEM_UUID,
  DEVICE_INFORMATION_SERVICE_UUID,
  DeskSystemCommand,
  decodeDeskConfig,
  FIRMWARE_REVISION_UUID,
  decodeDeskState,
  decodeFirmwareRevision,
  encodeDeskClientInfo,
  encodeDeskConfigWrite,
  encodeDeskPresence,
  encodeDeskSystemCommand,
} from './protocol';
import type {
  DeskConfig,
  DeskConfigField,
  DeskPeripheral,
  DeskState,
} from './types';
import type {
  DeskClient,
  DeskClientSnapshot,
  DeskSnapshotListener,
} from './DeskClient';

export type { DeskClientSnapshot } from './DeskClient';

const PAIRING_TIMEOUT_MS = 10_000;

/**
 * 原生 BLE 写在系统配对状态异常时可能长期不返回。超时只负责结束 UI 等待；
 * 后续连接会重新建立 GATT 会话，不会绕过固件的加密要求。
 */
async function withTimeout<T>(
  operation: Promise<T>,
  timeoutMs: number,
  message: string,
): Promise<T> {
  let timer: ReturnType<typeof setTimeout> | undefined;
  const timeout = new Promise<never>((_resolve, reject) => {
    timer = setTimeout(() => reject(new Error(message)), timeoutMs);
  });
  try {
    return await Promise.race([operation, timeout]);
  } finally {
    if (timer !== undefined) {
      clearTimeout(timer);
    }
  }
}

export class DeskBleClient implements DeskClient {
  private snapshot: DeskClientSnapshot = {
    phase: 'uninitialized',
    transport: 'ble',
    peripheral: null,
    deskState: null,
    deskConfig: null,
    firmwareRevision: null,
    error: null,
  };
  private listeners = new Set<DeskSnapshotListener>();
  private stateUnsubscribe: Unsubscribe | null = null;
  private configUnsubscribe: Unsubscribe | null = null;
  private disconnectUnsubscribe: Unsubscribe | null = null;
  private writeQueue: Promise<void> = Promise.resolve();

  constructor(
    private readonly adapter: BleAdapter,
    private readonly scanTimeoutMs = 10_000,
  ) {}

  subscribe(listener: DeskSnapshotListener): Unsubscribe {
    this.listeners.add(listener);
    listener(this.snapshot);
    return () => this.listeners.delete(listener);
  }

  async initialize(): Promise<void> {
    if (
      this.snapshot.phase !== 'uninitialized' &&
      this.snapshot.phase !== 'error'
    ) {
      return;
    }
    try {
      await this.adapter.initialize();
      this.update({ phase: 'idle', error: null });
    } catch (error) {
      this.fail(error);
      throw error;
    }
  }

  /** 扫描第一个 DeskGateway，完成服务发现、Notify 和加密 Client Info 验证。 */
  async connect(): Promise<void> {
    try {
      this.update({ phase: 'scanning', firmwareRevision: null, error: null });
      const peripheral = await this.adapter.scanForPeripheral(
        DESK_SERVICE_UUID,
        DESK_ADVERTISING_NAME,
        this.scanTimeoutMs,
      );
      this.update({ phase: 'connecting', peripheral });
      await this.adapter.connect(peripheral.id);
      await this.adapter.discoverServices(peripheral.id, [
        DESK_SERVICE_UUID,
        DEVICE_INFORMATION_SERVICE_UUID,
      ]);

      this.disconnectUnsubscribe?.();
      this.disconnectUnsubscribe = this.adapter.onDisconnect((id) => {
        if (id === this.snapshot.peripheral?.id) {
          this.stateUnsubscribe?.();
          this.configUnsubscribe?.();
          this.stateUnsubscribe = null;
          this.configUnsubscribe = null;
          this.update({
            phase: 'disconnected',
            deskState: null,
            deskConfig: null,
            firmwareRevision: null,
          });
        }
      });

      this.stateUnsubscribe?.();
      this.stateUnsubscribe = await this.adapter.subscribe(
        peripheral.id,
        DESK_SERVICE_UUID,
        DESK_STATE_UUID,
        (bytes) => this.acceptState(bytes),
      );

      const initialBytes = await this.adapter.read(
        peripheral.id,
        DESK_SERVICE_UUID,
        DESK_STATE_UUID,
      );
      this.acceptState(initialBytes);

      // Config 是向后兼容扩展：旧固件仍可控制，但设置页保持只读。
      await this.setupConfig(peripheral.id);

      const firmwareRevision = await this.readFirmwareRevision(peripheral.id);
      this.update({ firmwareRevision });

      this.update({ phase: 'pairing' });
      await this.adapter.ensureBond(peripheral.id);
      await this.writeClientInfo(peripheral.id);
      this.update({ phase: 'ready', error: null });
    } catch (error) {
      /* 配对超时后的原生 Write 不可取消；断开链路可安全终止它。 */
      const peripheralId = this.snapshot.peripheral?.id;
      if (peripheralId && this.snapshot.phase === 'pairing') {
        await this.adapter.disconnect(peripheralId).catch(() => undefined);
        this.writeQueue = Promise.resolve();
      }
      const connectionError = this.snapshot.phase === 'pairing'
        ? pairingError(error)
        : error;
      this.fail(connectionError);
      throw connectionError;
    }
  }

  /** 保留 Phase 0 调试入口，已有测试和文档无需随 Transport 抽象改名。 */
  scanAndConnect(): Promise<void> {
    return this.connect();
  }

  /**
   * 串行化所有命令，确保 HOLD 续期和 STOP 不会同时占用原生 GATT Write。
   */
  sendCommand(command: DeskCommandValue): Promise<void> {
    return this.enqueueWrite(
      DESK_COMMAND_UUID,
      encodeDeskCommand(command),
    );
  }

  setChildLock(enabled: boolean): Promise<void> {
    return this.writeConfig('child_lock', enabled);
  }

  sendPresenceHeartbeat(deviceId: string): Promise<void> {
    return this.enqueueWrite(DESK_PRESENCE_UUID, encodeDeskPresence(deviceId));
  }

  setSourceEnabled(
    source: 'rest' | 'bluetooth' | 'panel',
    enabled: boolean,
  ): Promise<void> {
    const fields: Record<typeof source, DeskConfigField> = {
      rest: 'rest_allowed',
      bluetooth: 'bluetooth_allowed',
      panel: 'panel_allowed',
    };
    return this.writeConfig(fields[source], enabled);
  }

  setMaxHeightMm(maxHeightMm: number): Promise<void> {
    return this.writeConfig('max_height_mm', maxHeightMm);
  }

  setPresetHeightMm(preset: 1 | 4, heightMm: number): Promise<void> {
    return this.writeConfig(
      preset === 1 ? 'preset1_height_mm' : 'preset4_height_mm',
      heightMm,
    );
  }

  /**
   * 按不会暂时破坏“请坐 < 站立”的顺序串行更新两个字段。
   * 固件每次写入都会回读 Config，因此 Web 与其他 BLE 订阅者仍以设备状态为准。
   */
  async setPresetHeightsMm(
    preset1HeightMm: number,
    preset4HeightMm: number,
  ): Promise<void> {
    const current = this.snapshot.deskConfig;
    if (!current) {
      throw new Error('Connected firmware does not support BLE Config');
    }
    if (preset1HeightMm >= preset4HeightMm) {
      throw new Error('Sit preset must be lower than standing preset');
    }

    const writes: Array<[1 | 4, number]> =
      preset1HeightMm >= current.preset4HeightMm
        ? [[4, preset4HeightMm], [1, preset1HeightMm]]
        : [[1, preset1HeightMm], [4, preset4HeightMm]];
    for (const [preset, value] of writes) {
      const currentValue = preset === 1
        ? this.snapshot.deskConfig?.preset1HeightMm
        : this.snapshot.deskConfig?.preset4HeightMm;
      if (currentValue !== value) {
        await this.setPresetHeightMm(preset, value);
      }
    }
  }

  restartGateway(): Promise<void> {
    return this.enqueueWrite(
      DESK_SYSTEM_UUID,
      encodeDeskSystemCommand(DeskSystemCommand.Restart),
    );
  }

  resetController(): Promise<void> {
    return this.enqueueWrite(
      DESK_SYSTEM_UUID,
      encodeDeskSystemCommand(DeskSystemCommand.ResetController),
    );
  }

  private enqueueWrite(
    characteristicUuid: string,
    bytes: readonly number[],
    afterWrite?: (peripheralId: string) => Promise<void>,
  ): Promise<void> {
    const peripheralId = this.snapshot.peripheral?.id;
    if (!peripheralId) {
      const error = new Error('DeskGateway is not connected');
      this.fail(error);
      return Promise.reject(error);
    }

    const write = this.writeQueue.then(async () => {
      await this.adapter.write(
        peripheralId,
        DESK_SERVICE_UUID,
        characteristicUuid,
        bytes,
      );
      await afterWrite?.(peripheralId);
      this.update({ error: null });
    });
    const operation = write.catch((error: unknown) => {
      /* ATT 拒绝是一次操作失败，不代表 GATT 链路已经断开。 */
      this.update({ error: isDeskBusyError(error) ? '另一台设备正在控制' : '操作失败' });
      throw error;
    });
    this.writeQueue = operation.catch(() => undefined);
    return operation;
  }

  /** 设置写入后立即回读；UI 只展示 ESP32 确认后的真实状态。 */
  private writeConfig(
    field: DeskConfigField,
    value: boolean | number,
  ): Promise<void> {
    if (!this.snapshot.deskConfig) {
      const error = new Error('Connected firmware does not support BLE Config');
      return Promise.reject(error);
    }
    return this.enqueueWrite(
      DESK_CONFIG_UUID,
      encodeDeskConfigWrite(field, value),
      async (peripheralId) => {
        const bytes = await this.adapter.read(
          peripheralId,
          DESK_SERVICE_UUID,
          DESK_CONFIG_UUID,
        );
        this.acceptConfig(bytes);
      },
    );
  }

  async disconnect(): Promise<void> {
    const peripheralId = this.snapshot.peripheral?.id;
    if (!peripheralId) {
      return;
    }

    // 主动断开前尽力发 STOP；固件仍会在断连回调和租约到期时再次兜底。
    await this.sendCommand(DeskCommand.Stop).catch(() => undefined);
    await this.adapter.disconnect(peripheralId);
  }

  dispose(): void {
    this.stateUnsubscribe?.();
    this.configUnsubscribe?.();
    this.disconnectUnsubscribe?.();
    this.stateUnsubscribe = null;
    this.configUnsubscribe = null;
    this.disconnectUnsubscribe = null;
    this.listeners.clear();
  }

  private acceptState(bytes: readonly number[]): void {
    try {
      this.update({ deskState: decodeDeskState(bytes), error: null });
    } catch (error) {
      this.fail(error);
    }
  }

  private acceptConfig(bytes: readonly number[]): void {
    try {
      this.update({ deskConfig: decodeDeskConfig(bytes), error: null });
    } catch (error) {
      this.fail(error);
    }
  }

  /** Config 为可选扩展，确保尚未升级的 ESP32 固件仍能连接和运动控制。 */
  private async setupConfig(peripheralId: string): Promise<void> {
    try {
      this.configUnsubscribe?.();
      this.configUnsubscribe = await this.adapter.subscribe(
        peripheralId,
        DESK_SERVICE_UUID,
        DESK_CONFIG_UUID,
        (bytes) => this.acceptConfig(bytes),
      );
      const bytes = await this.adapter.read(
        peripheralId,
        DESK_SERVICE_UUID,
        DESK_CONFIG_UUID,
      );
      this.acceptConfig(bytes);
    } catch {
      this.configUnsubscribe?.();
      this.configUnsubscribe = null;
      this.update({ deskConfig: null });
    }
  }

  /** Metadata is additive: old firmware remains controllable when DIS is absent. */
  private async readFirmwareRevision(peripheralId: string): Promise<string | null> {
    try {
      const bytes = await this.adapter.read(
        peripheralId,
        DEVICE_INFORMATION_SERVICE_UUID,
        FIRMWARE_REVISION_UUID,
      );
      return decodeFirmwareRevision(bytes);
    } catch {
      return null;
    }
  }

  /**
   * 新固件通过加密 Client Info 完成配对验证；旧固件缺少该可选特征时直接继续，
   * 不能退回 STOP 握手，否则第二个客户端上线会中断现有运动。
   */
  private async writeClientInfo(peripheralId: string): Promise<void> {
    try {
      await withTimeout(
        this.adapter.write(
          peripheralId,
          DESK_SERVICE_UUID,
          DESK_CLIENT_INFO_UUID,
          encodeDeskClientInfo(this.adapter.clientKind),
        ),
        PAIRING_TIMEOUT_MS,
        'BLE 配对超时',
      );
    } catch (error) {
      if (!isMissingCharacteristicError(error)) {
        throw error;
      }
    }
  }

  private fail(error: unknown): void {
    const message = error instanceof Error ? error.message : String(error);
    this.update({ phase: 'error', error: message });
  }

  private update(patch: Partial<DeskClientSnapshot>): void {
    this.snapshot = { ...this.snapshot, ...patch };
    for (const listener of this.listeners) {
      listener(this.snapshot);
    }
  }
}

/** react-native-ble-manager 在两端分别可能返回 0x80 或十进制 128。 */
export function isDeskBusyError(error: unknown): boolean {
  if (typeof error === 'object' && error !== null) {
    const record = error as Record<string, unknown>;
    if ([record.code, record.errorCode, record.attErrorCode].some(
      (value) => value === 0x80 || value === '0x80',
    )) {
      return true;
    }
  }
  const message = error instanceof Error ? error.message : String(error);
  return /(?:ATT|GATT|status|code)[^\n]*(?:0x80|\b128\b)/i.test(message);
}

function isMissingCharacteristicError(error: unknown): boolean {
  const message = error instanceof Error ? error.message : String(error);
  return /characteristic[^\n]*(?:not found|missing|does not exist)|(?:not found|missing)[^\n]*characteristic/i.test(message);
}

function pairingError(error: unknown): Error {
  const detail = error instanceof Error ? error.message : String(error);
  return new Error(
    `BLE 配对失败：${detail}。请在系统蓝牙设置中忽略/取消配对 DeskGateway，打开配对窗口后重试`,
  );
}
