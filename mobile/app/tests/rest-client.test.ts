/** DeskRestClient 的认证、状态映射与控制端点测试。 */

import assert from 'node:assert/strict';
import test from 'node:test';

import { DeskCommand } from '../src/desk/commands';
import { DeskRestClient } from '../src/desk/DeskRestClient';

const status = {
  status: 'idle',
  height_mm: 990,
  height_known: true,
  height_sim: false,
  child_lock: false,
  upward_blocked: false,
  controller_reset_supported: true,
  controller_reset_active: false,
  controller_reset_recommended: true,
  max_height_mm: 1100,
  preset1_height_mm: 640,
  preset4_height_mm: 1020,
  control_sources: {
    rest: true,
    bluetooth: true,
    panel: false,
  },
  driver: 'yourdesk_v1',
  build_date: 'Aug 11 2026',
  build_time: '21:05:03',
  build_id: '1234abcd',
  git_version: 'fc310ab',
};

test('connects through X-Desk-Key and maps REST status to the shared snapshot', async () => {
  const requests: Array<{ url: string; init?: RequestInit }> = [];
  const client = new DeskRestClient(async (url, init) => {
    requests.push({ url, init });
    return jsonResponse(status);
  });
  let latestTransport: string | null = null;
  let latestHeight: number | null = null;
  let latestPreset4 = 0;
  let latestFirmwareRevision: string | null = null;
  let resetRecommended = false;
  client.subscribe((snapshot) => {
    latestTransport = snapshot.transport;
    latestHeight = snapshot.deskState?.heightMm ?? null;
    latestPreset4 = snapshot.deskConfig?.preset4HeightMm ?? 0;
    latestFirmwareRevision = snapshot.firmwareRevision;
    resetRecommended = snapshot.deskState?.controllerResetRecommended ?? false;
  });

  client.configure('desk-gateway.local/', 'secret');
  await client.initialize();
  await client.connect();

  assert.equal(latestTransport, 'wifi');
  assert.equal(latestHeight, 990);
  assert.equal(latestPreset4, 1020);
  assert.equal(latestFirmwareRevision, 'Aug 11 2026 21:05:03 @ fc310ab');
  assert.equal(resetRecommended, true);
  assert.equal(requests[0].url, 'http://desk-gateway.local/api/v1/desk/status');
  assert.equal(
    (requests[0].init?.headers as Record<string, string>)['X-Desk-Key'],
    'secret',
  );
  client.dispose();
});

test('maps shared commands and configuration writes to existing REST endpoints', async () => {
  const requests: Array<{ url: string; init?: RequestInit }> = [];
  const client = new DeskRestClient(async (url, init) => {
    requests.push({ url, init });
    return jsonResponse(url.endsWith('/status') ? status : { ok: true });
  });
  client.configure('192.168.21.65', 'secret');
  await client.initialize();
  await client.connect();
  requests.length = 0;

  await client.sendCommand(DeskCommand.HoldUp);
  await client.sendCommand(DeskCommand.Stop);
  await client.setPresetHeightsMm(650, 1010);

  assert.deepEqual(
    requests.map((request) => new URL(request.url).pathname),
    [
      '/api/v1/desk/up',
      '/api/v1/desk/stop',
      '/api/v1/desk/presets',
      '/api/v1/desk/status',
    ],
  );
  assert.equal(
    requests[2].init?.body,
    JSON.stringify({ preset1_height_mm: 650, preset4_height_mm: 1010 }),
  );
  client.dispose();
});

test('configures one automatic-lock device and renews its LAN presence', async () => {
  const requests: Array<{ url: string; init?: RequestInit }> = [];
  const client = new DeskRestClient(async (url, init) => {
    requests.push({ url, init });
    return jsonResponse({ ok: true });
  });
  client.configure('desk-gateway.local', 'secret');

  await client.setAutoChildLock(true, 'bond_001122aabbcc');
  await client.sendPresenceHeartbeat('bond_001122aabbcc');

  assert.deepEqual(
    requests.map((request) => new URL(request.url).pathname),
    ['/api/v1/desk/auto-child-lock', '/api/v1/desk/presence'],
  );
  assert.equal(
    requests[0].init?.body,
    JSON.stringify({ enabled: true, device_id: 'bond_001122aabbcc' }),
  );
  assert.equal(
    requests[1].init?.body,
    JSON.stringify({ device_id: 'bond_001122aabbcc' }),
  );
  client.dispose();
});

test('queries and manages Bluetooth bonds through authenticated REST endpoints', async () => {
  const requests: Array<{ url: string; init?: RequestInit }> = [];
  const bonds = {
    devices: [{
      id: 'bond_73c98f21a1b2',
      kind: 'ios',
      label: 'iPhone · A1B2',
      alias: '',
      connected: true,
      controlling: false,
      delete_state: 'idle',
      delete_error: null,
    }],
    capacity: 3,
    pairing_window: { open: true, remaining_seconds: 119 },
  };
  const client = new DeskRestClient(async (url, init) => {
    requests.push({ url, init });
    return jsonResponse(url.endsWith('/bonds') && init?.method !== 'DELETE'
      ? bonds
      : { ok: true }, init?.method === 'DELETE' ? 202 : 200);
  });
  client.configure('desk-gateway.local', 'secret');

  const snapshot = await client.getBluetoothBonds();
  await client.setBluetoothPairingWindow(true);
  await client.setBluetoothPairingWindow(false);
  await client.deleteBluetoothBond('bond_73c98f21a1b2');
  await client.deleteAllBluetoothBonds();
  await client.renameBluetoothBond('bond_73c98f21a1b2', '书房 iPhone');

  assert.equal(snapshot.devices[0].label, 'iPhone · A1B2');
  assert.deepEqual(
    requests.map((request) => [
      new URL(request.url).pathname,
      request.init?.method ?? 'GET',
    ]),
    [
      ['/api/v1/bluetooth/bonds', 'GET'],
      ['/api/v1/bluetooth/pairing-window', 'POST'],
      ['/api/v1/bluetooth/pairing-window', 'DELETE'],
      ['/api/v1/bluetooth/bonds/bond_73c98f21a1b2', 'DELETE'],
      ['/api/v1/bluetooth/bonds', 'DELETE'],
      ['/api/v1/bluetooth/bonds/bond_73c98f21a1b2/alias', 'POST'],
    ],
  );
  assert.equal(
    (requests[5].init?.headers as Record<string, string>)['X-Desk-Key'],
    'secret',
  );
  assert.equal(
    requests[5].init?.body,
    JSON.stringify({ alias: '书房 iPhone' }),
  );
  client.dispose();
});

test('preserves firmware Bond errors and rejects unsafe IDs locally', async () => {
  const client = new DeskRestClient(async () =>
    jsonResponse({ error: 'delete_conflict' }, 409));
  client.configure('desk-gateway.local', 'secret');

  await assert.rejects(
    client.deleteAllBluetoothBonds(),
    /delete_conflict/,
  );
  await assert.rejects(
    client.deleteBluetoothBond('../status'),
    /无效的蓝牙配对设备 ID/,
  );
  client.dispose();
});

test('queries and manages gateway height presets through REST', async () => {
  const requests: Array<{ url: string; init?: RequestInit }> = [];
  const snapshot = {
    presets: [
      { id: 'sit', name: '请坐', height_mm: 560, built_in: true, deletable: false },
      { id: 'custom_00000001', name: '午休', height_mm: 720,
        built_in: false, deletable: true },
    ],
    custom_count: 1,
    custom_capacity: 16,
  };
  const client = new DeskRestClient(async (url, init) => {
    requests.push({ url, init });
    return jsonResponse(init?.method ? { ok: true } : snapshot);
  });
  client.configure('desk-gateway.local', 'secret');

  const loaded = await client.getHeightPresets();
  await client.createHeightPreset('午休', 720);
  await client.updateHeightPreset('custom_00000001', '阅读', 760);
  await client.gotoHeightPreset('custom_00000001');
  await client.deleteHeightPreset('custom_00000001');

  assert.equal(loaded.presets[1].name, '午休');
  assert.deepEqual(
    requests.map((request) => [
      new URL(request.url).pathname,
      request.init?.method ?? 'GET',
    ]),
    [
      ['/api/v1/desk/height-presets', 'GET'],
      ['/api/v1/desk/height-presets', 'POST'],
      ['/api/v1/desk/height-presets/custom_00000001', 'POST'],
      ['/api/v1/desk/height-presets/custom_00000001/goto', 'POST'],
      ['/api/v1/desk/height-presets/custom_00000001', 'DELETE'],
    ],
  );
  await assert.rejects(
    client.deleteHeightPreset('../status'),
    /无效的高度档位 ID/,
  );
  client.dispose();
});

function jsonResponse(payload: unknown, statusCode = 200): Response {
  return new Response(JSON.stringify(payload), {
    status: statusCode,
    headers: { 'Content-Type': 'application/json' },
  });
}
