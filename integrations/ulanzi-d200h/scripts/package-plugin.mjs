/** 生成不包含源码依赖和 node_modules 的可安装插件目录。 */
import { cp, mkdir, rm } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const pluginRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const releaseRoot = path.join(pluginRoot, "release", "com.ulanzi.deskgateway.ulanziPlugin");
const entries = [
  "assets",
  "dist",
  "libs",
  "property-inspector",
  "manifest.json",
  // dist/app.js 使用 ES Module，运行目录必须保留 type=module 声明。
  "package.json",
  "zh_CN.json",
  "en.json",
  "README.md",
  "README.zh-CN.md",
];

await rm(path.dirname(releaseRoot), { recursive: true, force: true });
await mkdir(releaseRoot, { recursive: true });
for (const entry of entries) {
  await cp(path.join(pluginRoot, entry), path.join(releaseRoot, entry), { recursive: true });
}
