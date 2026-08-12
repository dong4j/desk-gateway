/**
 * 移动端 BLE 库隔离层。
 *
 * Phase 0 通过该接口验证 react-native-ble-manager；若需要与 ble-plx A/B，
 * 只替换适配器而不改 Desk 协议和控制逻辑。
 */

import type { DeskPeripheral } from '../desk/types';

export type BytesListener = (bytes: readonly number[]) => void;
export type DisconnectListener = (peripheralId: string) => void;
export type Unsubscribe = () => void;

export interface BleAdapter {
  /** Client Info 使用的稳定平台枚举；协议层不直接依赖 React Native Platform。 */
  readonly clientKind: 0x02 | 0x03;
  initialize(): Promise<void>;
  scanForPeripheral(
    serviceUuid: string,
    advertisingName: string,
    timeoutMs: number,
  ): Promise<DeskPeripheral>;
  connect(peripheralId: string): Promise<void>;
  discoverServices(peripheralId: string, serviceUuids: string[]): Promise<void>;
  ensureBond(peripheralId: string): Promise<void>;
  read(
    peripheralId: string,
    serviceUuid: string,
    characteristicUuid: string,
  ): Promise<readonly number[]>;
  write(
    peripheralId: string,
    serviceUuid: string,
    characteristicUuid: string,
    bytes: readonly number[],
  ): Promise<void>;
  subscribe(
    peripheralId: string,
    serviceUuid: string,
    characteristicUuid: string,
    listener: BytesListener,
  ): Promise<Unsubscribe>;
  onDisconnect(listener: DisconnectListener): Unsubscribe;
  disconnect(peripheralId: string): Promise<void>;
}
