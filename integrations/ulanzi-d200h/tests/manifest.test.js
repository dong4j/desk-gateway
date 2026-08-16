import assert from "node:assert/strict";
import { access, readFile } from "node:fs/promises";
import path from "node:path";
import test from "node:test";

const pluginRoot = path.resolve(import.meta.dirname, "..");

test("manifest 只暴露三个 D200H Keypad Action", async () => {
  const manifest = JSON.parse(await readFile(path.join(pluginRoot, "manifest.json"), "utf8"));
  assert.deepEqual(manifest.Actions.map((action) => action.Name), ["请坐", "站立", "番茄时刻"]);
  assert.equal(new Set(manifest.Actions.map((action) => action.UUID)).size, 3);
  for (const action of manifest.Actions) {
    assert.deepEqual(action.Devices, ["D200H"]);
    assert.deepEqual(action.Controllers, ["Keypad"]);
    await access(path.join(pluginRoot, action.Icon));
    await access(path.join(pluginRoot, action.PropertyInspectorPath));
  }
});

test("发布入口和属性面板依赖均已生成", async () => {
  const manifest = JSON.parse(await readFile(path.join(pluginRoot, "manifest.json"), "utf8"));
  await access(path.join(pluginRoot, manifest.CodePath));
  for (const file of [
    "libs/js/constants.js",
    "libs/js/eventEmitter.js",
    "libs/js/timers.js",
    "libs/js/utils.js",
    "libs/js/ulanziApi.js",
  ]) {
    await access(path.join(pluginRoot, file));
  }
});

