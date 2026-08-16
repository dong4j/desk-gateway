import assert from "node:assert/strict";
import test from "node:test";
import { DeskGatewayClient, DeskGatewayError, errorMessage } from "../plugin/deskGatewayClient.js";

function response(payload = { ok: true }, status = 200) {
  return {
    ok: status >= 200 && status < 300,
    status,
    async json() { return payload; },
  };
}

test("状态请求携带 X-Desk-Key 且不产生请求体", async () => {
  const calls = [];
  const client = new DeskGatewayClient({
    fetchImpl: async (...args) => { calls.push(args); return response({ height_mm: 725 }); },
  });
  client.configure({ gateway_url: "http://desk-gateway.local", api_key: "secret" });

  await client.getStatus();

  assert.equal(calls[0][0], "http://desk-gateway.local/api/v1/desk/status");
  assert.equal(calls[0][1].headers["X-Desk-Key"], "secret");
  assert.equal(calls[0][1].body, undefined);
});

test("三个动作映射到固定白名单接口", async () => {
  const calls = [];
  const client = new DeskGatewayClient({
    fetchImpl: async (...args) => { calls.push(args); return response(); },
  });
  client.configure({ gateway_url: "http://desk-gateway.local", api_key: "secret" });

  await client.gotoSitting();
  await client.gotoStanding();
  await client.startFocus();

  assert.deepEqual(calls.map(([url]) => url), [
    "http://desk-gateway.local/api/v1/desk/preset/1/goto",
    "http://desk-gateway.local/api/v1/desk/preset/4/goto",
    "http://desk-gateway.local/api/v1/reminder/action",
  ]);
  assert.equal(calls[2][1].body, JSON.stringify({ action: "start_focus" }));
});

test("协议错误转换为可读提示且不包含密钥", () => {
  const error = new DeskGatewayError("failed", {
    status: 403,
    payload: { reason: "child_lock" },
  });
  assert.equal(errorMessage(error), "童锁已开启，动作被拒绝");
  assert.doesNotMatch(errorMessage(error), /secret/);
});

