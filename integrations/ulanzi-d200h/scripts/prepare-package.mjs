/**
 * 将固定版本的 common-html SDK 复制到插件包。
 *
 * Property Inspector 在 UlanziStudio 中独立加载，发布目录必须自带这些文件，
 * 不能依赖 SDK 仓库的相对路径。
 */
import { cp, mkdir, rm } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const pluginRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const sdkRoot = path.resolve(pluginRoot, "../..");
const libsRoot = path.join(pluginRoot, "libs");

await rm(libsRoot, { recursive: true, force: true });
await mkdir(libsRoot, { recursive: true });
await cp(path.join(sdkRoot, "common-html", "js"), path.join(libsRoot, "js"), { recursive: true });
