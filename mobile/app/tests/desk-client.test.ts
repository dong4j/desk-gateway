/** DeskBleClient 的会话顺序与状态通知测试。 */

import assert from 'node:assert/strict';
import test from 'node:test';

import type {
  BleAdapter,
  BytesListener,
  DisconnectListener,
  Unsubscribe,
} from '../src/ble/BleAdapter';
import { DeskCommand } from '../src/desk/commands';
import { DeskBleClient } from '../src/desk/DeskBleClient';
import {
  DESK_CONFIG_UUID,
  DESK_STATE_UUID,
  FIRMWARE_REVISION_UUID,
} from '../src/desk/protocol';
import type { DeskPeripheral } from '../src/desk/types';

const initialState = [
  0x01,
  0x00,
  0b0000_1001,
  0x00,
  0x80,
  0x02,
  0x4c,
  0x04,
];

test('connects, subscribes, bonds, then verifies encrypted STOP', async () => {
  const adapter = new FakeBleAdapter();
  const client = new DeskBleClient(adapter);
  let latestPhase = 'missing';
  let latestHeight: number | null = null;
  let latestFirmware: string | null = null;
  let latestChildLock = false;
  const unsubscribe = client.subscribe((snapshot) => {
    latestPhase = snapshot.phase;
    latestHeight = snapshot.deskState?.heightMm ?? null;
    latestFirmware = snapshot.firmwareRevision;
    latestChildLock = snapshot.deskConfig?.childLock ?? false;
  });

  await client.initialize();
  await client.scanAndConnect();

  assert.equal(latestPhase, 'ready');
  assert.equal(latestHeight, 640);
  assert.equal(latestFirmware, 'Aug 11 2026 19:52:22 # 1b5e1e21');
  assert.deepEqual(adapter.writes, [[DeskCommand.Stop]]);
  assert.deepEqual(adapter.operations, [
    'initialize',
    'scan',
    'connect',
    'discover',
    'subscribe:state',
    'read:state',
    'subscribe:config',
    'read:config',
    'read:firmware',
    'bond',
    'write:0',
  ]);

  adapter.emitState([
    0x01,
    0x01,
    0b0000_1001,
    0x00,
    0xde,
    0x03,
    0x4c,
    0x04,
  ]);
  assert.equal(latestHeight, 990);

  await client.setChildLock(true);
  assert.equal(latestChildLock, true);
  assert.deepEqual(adapter.writes.at(-1), [2, 1, 1, 0]);

  await client.setPresetHeightMm(1, 650);
  assert.equal(adapter.writes.at(-1)?.[1], 6);

  adapter.emitDisconnect();
  assert.equal(latestPhase, 'disconnected');
  assert.equal(latestHeight, null);
  assert.equal(latestFirmware, null);

  unsubscribe();
  client.dispose();
});

test('keeps old firmware controllable when Device Information is absent', async () => {
  const adapter = new FakeBleAdapter(false);
  const client = new DeskBleClient(adapter);
  let latestFirmware: string | null = 'not-read';
  let latestPhase = 'missing';
  client.subscribe((snapshot) => {
    latestFirmware = snapshot.firmwareRevision;
    latestPhase = snapshot.phase;
  });

  await client.initialize();
  await client.scanAndConnect();

  assert.equal(latestPhase, 'ready');
  assert.equal(latestFirmware, null);
  client.dispose();
});

test('updates standing preset first when the new sitting height crosses the old standing height', async () => {
  const adapter = new FakeBleAdapter();
  const client = new DeskBleClient(adapter);

  await client.initialize();
  await client.scanAndConnect();
  adapter.writes.length = 0;

  await client.setPresetHeightsMm(1050, 1100);

  assert.deepEqual(adapter.writes.map((write) => write[1]), [7, 6]);
  client.dispose();
});

test('keeps Command and State usable when old firmware has no Config', async () => {
  const adapter = new FakeBleAdapter(true, false);
  const client = new DeskBleClient(adapter);
  let latestConfig: unknown = 'not-read';
  let latestPhase = 'missing';
  client.subscribe((snapshot) => {
    latestConfig = snapshot.deskConfig;
    latestPhase = snapshot.phase;
  });

  await client.initialize();
  await client.scanAndConnect();

  assert.equal(latestPhase, 'ready');
  assert.equal(latestConfig, null);
  assert.deepEqual(adapter.writes, [[DeskCommand.Stop]]);
  client.dispose();
});

test('keeps BLE ready when a command write is rejected', async () => {
  const adapter = new FakeBleAdapter();
  const client = new DeskBleClient(adapter);
  let latestPhase = 'missing';
  let latestError: string | null = null;
  client.subscribe((snapshot) => {
    latestPhase = snapshot.phase;
    latestError = snapshot.error;
  });

  await client.initialize();
  await client.scanAndConnect();
  adapter.rejectNextWrite = true;

  await assert.rejects(client.sendCommand(DeskCommand.Preset4));
  assert.equal(latestPhase, 'ready');
  assert.equal(latestError, '操作失败');

  await client.sendCommand(DeskCommand.Stop);
  assert.equal(latestPhase, 'ready');
  assert.equal(latestError, null);
  client.dispose();
});

class FakeBleAdapter implements BleAdapter {
  readonly peripheral: DeskPeripheral = {
    id: 'desk-1',
    name: 'DeskGateway',
    rssi: -42,
  };
  readonly operations: string[] = [];
  readonly writes: number[][] = [];
  rejectNextWrite = false;
  private stateListener: BytesListener | null = null;
  private configListener: BytesListener | null = null;
  private disconnectListener: DisconnectListener | null = null;
  private config = [
    0x02, 0b0000_1110, 0x4c, 0x04, 0x80, 0x02, 0xfc, 0x03,
  ];

  constructor(
    private readonly firmwareAvailable = true,
    private readonly configAvailable = true,
  ) {}

  async initialize(): Promise<void> {
    this.operations.push('initialize');
  }

  async scanForPeripheral(): Promise<DeskPeripheral> {
    this.operations.push('scan');
    return this.peripheral;
  }

  async connect(): Promise<void> {
    this.operations.push('connect');
  }

  async discoverServices(): Promise<void> {
    this.operations.push('discover');
  }

  async ensureBond(): Promise<void> {
    this.operations.push('bond');
  }

  async read(
    _peripheralId: string,
    _serviceUuid: string,
    characteristicUuid: string,
  ): Promise<readonly number[]> {
    if (characteristicUuid === DESK_STATE_UUID) {
      this.operations.push('read:state');
      return initialState;
    }
    if (characteristicUuid === DESK_CONFIG_UUID) {
      this.operations.push('read:config');
      if (!this.configAvailable) {
        throw new Error('Config characteristic not found');
      }
      return this.config;
    }
    if (characteristicUuid === FIRMWARE_REVISION_UUID) {
      this.operations.push('read:firmware');
      if (!this.firmwareAvailable) {
        throw new Error('Firmware Revision characteristic not found');
      }
      return Array.from(
        'Aug 11 2026 19:52:22 # 1b5e1e21',
        (character) => character.charCodeAt(0),
      );
    }
    throw new Error(`Unexpected characteristic: ${characteristicUuid}`);
  }

  async write(
    _peripheralId: string,
    _serviceUuid: string,
    characteristicUuid: string,
    bytes: readonly number[],
  ): Promise<void> {
    this.operations.push(`write:${bytes.join(',')}`);
    this.writes.push(Array.from(bytes));
    if (this.rejectNextWrite) {
      this.rejectNextWrite = false;
      throw new Error('ATT write rejected');
    }
    if (characteristicUuid === DESK_CONFIG_UUID) {
      this.applyConfigWrite(bytes);
      this.configListener?.(this.config);
    }
  }

  async subscribe(
    _peripheralId: string,
    _serviceUuid: string,
    characteristicUuid: string,
    listener: BytesListener,
  ): Promise<Unsubscribe> {
    if (characteristicUuid === DESK_STATE_UUID) {
      this.operations.push('subscribe:state');
      this.stateListener = listener;
    } else if (characteristicUuid === DESK_CONFIG_UUID) {
      if (!this.configAvailable) {
        throw new Error('Config characteristic not found');
      }
      this.operations.push('subscribe:config');
      this.configListener = listener;
    } else {
      throw new Error(`Unexpected subscription: ${characteristicUuid}`);
    }
    return () => {
      if (characteristicUuid === DESK_STATE_UUID) {
        this.stateListener = null;
      } else {
        this.configListener = null;
      }
    };
  }

  onDisconnect(listener: DisconnectListener): Unsubscribe {
    this.disconnectListener = listener;
    return () => {
      this.disconnectListener = null;
    };
  }

  async disconnect(): Promise<void> {
    this.operations.push('disconnect');
  }

  emitState(bytes: readonly number[]): void {
    this.stateListener?.(bytes);
  }

  emitDisconnect(): void {
    this.disconnectListener?.(this.peripheral.id);
  }

  private applyConfigWrite(bytes: readonly number[]): void {
    const field = bytes[1];
    const value = bytes[2] | (bytes[3] << 8);
    if (field >= 1 && field <= 4) {
      const mask = 1 << (field - 1);
      this.config[1] = value ? this.config[1] | mask : this.config[1] & ~mask;
    } else if (field === 5) {
      this.config[2] = value & 0xff;
      this.config[3] = value >> 8;
    } else if (field === 6) {
      this.config[4] = value & 0xff;
      this.config[5] = value >> 8;
    } else if (field === 7) {
      this.config[6] = value & 0xff;
      this.config[7] = value >> 8;
    }
  }
}
