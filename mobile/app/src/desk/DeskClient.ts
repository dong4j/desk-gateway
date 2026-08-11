/**
 * Desk Gateway 移动端统一控制契约。
 *
 * UI 和长按控制只依赖本接口，不关心底层使用 BLE GATT 还是局域网 REST。
 * 通道切换时绝不自动恢复运动，所有入口仍由 ESP32 desk_core 做最终安全裁决。
 */

import type { DeskCommandValue } from './commands';
import type { DeskConfig, DeskPeripheral, DeskState } from './types';

export type DeskTransportKind = 'ble' | 'wifi';
export type DeskConnectionMode = 'auto' | DeskTransportKind;

export interface DeskConnectionSettings {
  mode: DeskConnectionMode;
  restHost: string;
  restKey: string;
}

export type DeskClientPhase =
  | 'uninitialized'
  | 'idle'
  | 'scanning'
  | 'connecting'
  | 'pairing'
  | 'ready'
  | 'disconnected'
  | 'error';

export interface DeskClientSnapshot {
  phase: DeskClientPhase;
  transport: DeskTransportKind | null;
  peripheral: DeskPeripheral | null;
  deskState: DeskState | null;
  deskConfig: DeskConfig | null;
  firmwareRevision: string | null;
  error: string | null;
}

export type DeskSnapshotListener = (snapshot: DeskClientSnapshot) => void;
export type DeskUnsubscribe = () => void;

export interface DeskClient {
  subscribe(listener: DeskSnapshotListener): DeskUnsubscribe;
  initialize(): Promise<void>;
  connect(): Promise<void>;
  sendCommand(command: DeskCommandValue): Promise<void>;
  setChildLock(enabled: boolean): Promise<void>;
  setSourceEnabled(
    source: 'rest' | 'bluetooth' | 'panel',
    enabled: boolean,
  ): Promise<void>;
  setMaxHeightMm(maxHeightMm: number): Promise<void>;
  setPresetHeightsMm(
    preset1HeightMm: number,
    preset4HeightMm: number,
  ): Promise<void>;
  restartGateway(): Promise<void>;
  disconnect(): Promise<void>;
  dispose(): void;
}
