import assert from "node:assert/strict";
import test from "node:test";
import { SharedStatusPoller } from "../plugin/statusPoller.js";

test("上一轮未结束时跳过下一轮状态请求", async () => {
  let resolveStatus;
  let calls = 0;
  const client = {
    getStatus() {
      calls += 1;
      return new Promise((resolve) => { resolveStatus = resolve; });
    },
  };
  const received = [];
  const poller = new SharedStatusPoller({
    client,
    onStatus: (status) => received.push(status),
    onError: () => assert.fail("unexpected error"),
  });

  const first = poller.pollOnce();
  const second = await poller.pollOnce();
  assert.equal(second, false);
  assert.equal(calls, 1);

  resolveStatus({ height_mm: 725 });
  await first;
  assert.deepEqual(received, [{ height_mm: 725 }]);
});

