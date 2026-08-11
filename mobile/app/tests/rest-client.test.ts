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
  client.subscribe((snapshot) => {
    latestTransport = snapshot.transport;
    latestHeight = snapshot.deskState?.heightMm ?? null;
    latestPreset4 = snapshot.deskConfig?.preset4HeightMm ?? 0;
    latestFirmwareRevision = snapshot.firmwareRevision;
  });

  client.configure('desk-gateway.local/', 'secret');
  await client.initialize();
  await client.connect();

  assert.equal(latestTransport, 'wifi');
  assert.equal(latestHeight, 990);
  assert.equal(latestPreset4, 1020);
  assert.equal(latestFirmwareRevision, 'Aug 11 2026 21:05:03 @ fc310ab');
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

function jsonResponse(payload: unknown, statusCode = 200): Response {
  return new Response(JSON.stringify(payload), {
    status: statusCode,
    headers: { 'Content-Type': 'application/json' },
  });
}
