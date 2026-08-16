/**
 * Desk Gateway 插件构建配置。
 *
 * UlanziStudio 直接加载单文件 ES module，因此将 common-node SDK 与 ws
 * 打进 dist/app.js，避免用户安装插件后还需要执行 npm install。
 */
import path from "node:path";
import { fileURLToPath } from "node:url";
import webpack from "webpack";

const pluginRoot = path.dirname(fileURLToPath(import.meta.url));

export default {
  entry: "./plugin/app.js",
  target: "node20",
  mode: "production",
  output: {
    filename: "app.js",
    path: path.resolve(pluginRoot, "dist"),
    libraryTarget: "module",
    chunkFormat: "module",
    clean: true,
  },
  resolve: {
    modules: [path.resolve(pluginRoot, "node_modules"), "node_modules"],
  },
  experiments: {
    outputModule: true,
  },
  plugins: [
    // ws 会按需探测这两个原生加速包；D200H 的本地 WebSocket 流量不需要它们，
    // 明确忽略后使用 ws 自带的纯 JavaScript 实现，发布包也无需原生二进制。
    new webpack.IgnorePlugin({ resourceRegExp: /^(bufferutil|utf-8-validate)$/ }),
  ],
  optimization: {
    minimize: true,
  },
};
