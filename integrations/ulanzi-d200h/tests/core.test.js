import assert from "node:assert/strict";
import test from "node:test";
import { createKeyView, formatCountdown, formatHeight, normalizeConfig, renderKeySvg } from "../plugin/core.js";

test("高度按一位小数厘米显示", () => {
  assert.equal(formatHeight(725), "72.5 cm");
  assert.equal(formatHeight(Number.NaN), "-- cm");
});

test("倒计时补齐秒数并限制为非负数", () => {
  assert.equal(formatCountdown(1499), "24:59");
  assert.equal(formatCountdown(-2), "00:00");
});

test("配置只接受 HTTP 或 HTTPS", () => {
  assert.deepEqual(normalizeConfig({ gateway_url: "http://desk-gateway.local///", api_key: " key " }), {
    gateway_url: "http://desk-gateway.local",
    api_key: "key",
  });
  assert.equal(normalizeConfig({ gateway_url: "file:///tmp/secret", api_key: "key" }).gateway_url, "");
});

test("专注状态使用设备端剩余秒数", () => {
  const view = createKeyView("pomodoro", {
    configured: true,
    online: true,
    snapshot: {
      reminder: { available: true, state: "running", phase: "focus", remaining_sec: 1499 },
    },
  });
  assert.equal(view.label, "专注");
  assert.equal(view.value, "24:59");
  assert.match(renderKeySvg(view), /24:59/);
});

test("离线状态不继续展示陈旧高度", () => {
  const view = createKeyView("sit", {
    configured: true,
    online: false,
    snapshot: { height_known: true, height_mm: 725 },
  });
  assert.equal(view.value, "离线");
});

test("首次轮询失败时直接显示离线", () => {
  const view = createKeyView("stand", {
    configured: true,
    online: false,
    snapshot: null,
  });
  assert.equal(view.value, "离线");
});

test("配置生效到首次快照之间显示连接中", () => {
  const view = createKeyView("sit", {
    configured: true,
    online: null,
    snapshot: null,
  });
  assert.equal(view.value, "连接中");
});

test("提醒等待状态使用固件真实状态名", () => {
  const view = createKeyView("pomodoro", {
    configured: true,
    online: true,
    snapshot: {
      reminder: { available: true, state: "waiting", phase: "focus", remaining_sec: 0 },
    },
  });
  assert.equal(view.detail, "等待处理");
});
