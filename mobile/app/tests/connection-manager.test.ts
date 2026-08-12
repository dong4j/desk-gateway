/** 自动模式必须在 BLE 失败后安全回退到局域网 REST。 */

import assert from 'node:assert/strict';
import test from 'node:test';

import { DeskCommand, type DeskCommandValue } from '../src/desk/commands';
import type {
  DeskClient,
  DeskClientSnapshot,
  DeskSnapshotListener,
} from '../src/desk/DeskClient';
import { DeskConnectionManager } from '../src/desk/DeskConnectionManager';
import { DeskRestClient } from '../src/desk/DeskRestClient';

test('prefers BLE and falls back to REST without replaying a motion command', async () => {
  const ble = new FailingBleClient();
  const rest = new DeskRestClient(async () => new Response(JSON.stringify({
    status: 'idle',
    height_mm: 640,
    height_known: true,
    max_height_mm: 1020,
    preset1_height_mm: 640,
    preset4_height_mm: 1020,
    control_sources: { rest: true, bluetooth: true, panel: false },
  }), { status: 200 }));
  const manager = new DeskConnectionManager(ble, rest, {
    mode: 'auto',
    restHost: 'desk-gateway.local',
    restKey: 'secret',
  });
  const snapshots: DeskClientSnapshot[] = [];
  manager.subscribe((snapshot) => {
    snapshots.push(snapshot);
  });

  await manager.initialize();
  await manager.connect();

  const latest = snapshots.at(-1)!;
  assert.equal(ble.connectAttempts, 1);
  assert.equal(latest.phase, 'ready');
  assert.equal(latest.transport, 'wifi');
  assert.equal(ble.commands.length, 0);
  manager.dispose();
});

test('falls back to REST after a connected BLE device goes out of range', async () => {
  const ble = new ReadyBleClient();
  const requestedPaths: string[] = [];
  const rest = new DeskRestClient(async (input) => {
    requestedPaths.push(new URL(input).pathname);
    return new Response(JSON.stringify({
      status: 'idle',
      height_mm: 710,
      height_known: true,
      control_sources: { rest: true, bluetooth: true, panel: false },
    }), { status: 200 });
  });
  const manager = new DeskConnectionManager(ble, rest, {
    mode: 'auto',
    restHost: 'desk-gateway.local',
    restKey: 'secret',
  });

  await manager.initialize();
  await manager.connect();
  await manager.sendCommand(DeskCommand.HoldUp);

  const wifiReady = new Promise<DeskClientSnapshot>((resolve) => {
    const unsubscribe = manager.subscribe((snapshot) => {
      if (snapshot.phase === 'ready' && snapshot.transport === 'wifi') {
        unsubscribe();
        resolve(snapshot);
      }
    });
  });
  ble.disconnectUnexpectedly();
  const snapshot = await wifiReady;

  assert.equal(snapshot.deskState?.heightMm, 710);
  assert.deepEqual(ble.commands, [DeskCommand.HoldUp]);
  assert.deepEqual(requestedPaths, ['/api/v1/desk/status']);
  manager.dispose();
});

test('uses the REST management channel while BLE remains the active control transport', async () => {
  const ble = new ReadyBleClient();
  const requestedPaths: string[] = [];
  const rest = new DeskRestClient(async (input) => {
    requestedPaths.push(new URL(input).pathname);
    return new Response(JSON.stringify({
      devices: [],
      capacity: 3,
      pairing_window: { open: false, remaining_seconds: 0 },
    }), { status: 200 });
  });
  const manager = new DeskConnectionManager(ble, rest, {
    mode: 'ble',
    restHost: 'desk-gateway.local',
    restKey: 'secret',
  });
  let latestTransport: string | null = null;
  manager.subscribe((snapshot) => {
    latestTransport = snapshot.transport;
  });

  await manager.initialize();
  await manager.connect();
  const bonds = await manager.getBluetoothBonds();

  assert.equal(bonds.capacity, 3);
  assert.equal(latestTransport, 'ble');
  assert.deepEqual(requestedPaths, ['/api/v1/bluetooth/bonds']);
  manager.dispose();
});

class FailingBleClient implements DeskClient {
  connectAttempts = 0;
  commands: DeskCommandValue[] = [];
  private listeners = new Set<DeskSnapshotListener>();
  private snapshot: DeskClientSnapshot = {
    phase: 'uninitialized',
    transport: 'ble',
    peripheral: null,
    deskState: null,
    deskConfig: null,
    firmwareRevision: null,
    error: null,
  };

  subscribe(listener: DeskSnapshotListener): () => void {
    this.listeners.add(listener);
    listener(this.snapshot);
    return () => this.listeners.delete(listener);
  }

  async initialize(): Promise<void> {
    this.emit({ phase: 'idle' });
  }

  async connect(): Promise<void> {
    this.connectAttempts += 1;
    this.emit({ phase: 'error', error: 'BLE unavailable' });
    throw new Error('BLE unavailable');
  }

  async sendCommand(command: DeskCommandValue): Promise<void> {
    this.commands.push(command);
  }

  async setChildLock(): Promise<void> {}
  async setSourceEnabled(): Promise<void> {}
  async setMaxHeightMm(): Promise<void> {}
  async setPresetHeightsMm(): Promise<void> {}
  async restartGateway(): Promise<void> {}
  async disconnect(): Promise<void> {}
  dispose(): void {}

  private emit(patch: Partial<DeskClientSnapshot>): void {
    this.snapshot = { ...this.snapshot, ...patch };
    for (const listener of this.listeners) {
      listener(this.snapshot);
    }
  }
}

class ReadyBleClient implements DeskClient {
  commands: DeskCommandValue[] = [];
  private listeners = new Set<DeskSnapshotListener>();
  private snapshot: DeskClientSnapshot = {
    phase: 'uninitialized',
    transport: 'ble',
    peripheral: null,
    deskState: null,
    deskConfig: null,
    firmwareRevision: null,
    error: null,
  };

  subscribe(listener: DeskSnapshotListener): () => void {
    this.listeners.add(listener);
    listener(this.snapshot);
    return () => this.listeners.delete(listener);
  }

  async initialize(): Promise<void> {
    this.emit({ phase: 'idle' });
  }

  async connect(): Promise<void> {
    this.emit({
      phase: 'ready',
      peripheral: { id: 'ble-1', name: 'DeskGateway', rssi: -45 },
    });
  }

  async sendCommand(command: DeskCommandValue): Promise<void> {
    this.commands.push(command);
  }

  disconnectUnexpectedly(): void {
    this.emit({ phase: 'disconnected' });
  }

  async setChildLock(): Promise<void> {}
  async setSourceEnabled(): Promise<void> {}
  async setMaxHeightMm(): Promise<void> {}
  async setPresetHeightsMm(): Promise<void> {}
  async restartGateway(): Promise<void> {}
  async disconnect(): Promise<void> {}
  dispose(): void {}

  private emit(patch: Partial<DeskClientSnapshot>): void {
    this.snapshot = { ...this.snapshot, ...patch };
    for (const listener of this.listeners) {
      listener(this.snapshot);
    }
  }
}
