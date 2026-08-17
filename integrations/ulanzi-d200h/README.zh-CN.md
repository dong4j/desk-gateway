# Desk Gateway Ulanzi D200H 插件

**语言：** [English](README.md) · 简体中文

该目录只保存插件源码，不包含 Ulanzi SDK、第三方运行库和构建产物。源码不能直接复制到
UlanziStudio 安装目录；编译前需要自行下载官方 `UlanziDeckPlugin-SDK`。

插件为 D200H 提供三个按键：

- **请坐**：调用 Desk Gateway 预设 1，显示坐姿目标高度和每秒刷新的当前高度。
- **站立**：调用 Desk Gateway 预设 4，显示站姿目标高度和每秒刷新的当前高度。
- **番茄时刻**：启动设备端专注阶段，显示设备端剩余倒计时。

三个按键共享一次 `GET /api/v1/desk/status` 轮询。倒计时来源是 Desk Gateway，插件不会
在电脑端创建第二套计时状态。

## 编译要求

- Node.js 20 或更新版本
- npm
- [UlanziDeckPlugin-SDK](https://github.com/UlanziTechnology/UlanziDeckPlugin-SDK)
- UlanziStudio 2.1.4 或更新版本

## 下载 SDK 并放入插件源码

先下载官方 SDK：

```bash
git clone https://github.com/UlanziTechnology/UlanziDeckPlugin-SDK.git
```

把本目录完整复制到 SDK 的 `demo/` 目录，并使用 manifest 对应的插件目录名：

```text
UlanziDeckPlugin-SDK/
├── common-html/
├── common-node/
└── demo/
    └── com.ulanzi.deskgateway.ulanziPlugin/
        ├── assets/
        ├── plugin/
        ├── property-inspector/
        ├── scripts/
        ├── tests/
        ├── manifest.json
        └── package.json
```

在 macOS 或 Linux 中，可以在 `desk-gateway` 仓库根目录执行：

```bash
cp -R integrations/ulanzi-d200h \
  /path/to/UlanziDeckPlugin-SDK/demo/com.ulanzi.deskgateway.ulanziPlugin
```

如果目标目录已经存在，应先确认其中没有需要保留的修改，再删除旧目录或换一个全新的 SDK
工作区。不要把源码复制成两层嵌套目录。

## 编译和测试

进入 SDK 中的插件目录：

```bash
cd /path/to/UlanziDeckPlugin-SDK/demo/com.ulanzi.deskgateway.ulanziPlugin
npm ci
npm run package
npm test
```

`npm run package` 会执行以下工作：

1. 从 SDK 的 `common-html/js` 复制 Property Inspector 运行库；
2. 使用 SDK 的 `common-node` 构建 Node.js 主服务；
3. 生成可直接安装的插件目录。

构建完成后，可安装目录位于：

```text
release/com.ulanzi.deskgateway.ulanziPlugin/
```

该目录应包含 `manifest.json`、`package.json`、`dist/app.js`、`libs/`、属性面板和图标资源。
其中 `package.json` 提供 Node.js 识别 `dist/app.js` 所需的 ES Module 声明，安装时不能省略。

## 安装到 UlanziStudio

先完全退出 UlanziStudio，再把上面的可安装目录复制到插件目录。

macOS：

```text
~/Library/Application Support/Ulanzi/UlanziDeck/Plugins/
```

Windows：

```text
%APPDATA%\Ulanzi\UlanziDeck\Plugins\
```

最终目录结构应为：

```text
Plugins/
└── com.ulanzi.deskgateway.ulanziPlugin/
    ├── manifest.json
    ├── package.json
    ├── dist/app.js
    ├── libs/
    └── property-inspector/
```

重新打开 UlanziStudio 并连接 D200H。刷新插件列表后，在 `Desk Gateway` 分类中把“请坐”、
“站立”和“番茄时刻”拖到三个按键。

## 配置 Desk Gateway

选择任意一个按键，在属性面板中填写：

- 网关地址：默认 `http://desk-gateway.local`
- `X-Desk-Key`：Desk Gateway 的访问密钥

配置由三个按键共享。密钥保存在 UlanziStudio global settings，不会写入插件源码或日志。
如果 `.local` 域名无法解析，可改填 Desk Gateway 的局域网 IP，例如
`http://192.168.1.100`。

电脑、D200H 所连接的 UlanziStudio 与 Desk Gateway 必须处于可互通网络，且 UlanziStudio
需要保持运行。

## 接口映射

- 请坐固定调用 `POST /api/v1/desk/preset/1/goto`。
- 站立固定调用 `POST /api/v1/desk/preset/4/goto`。
- 番茄时刻固定调用 `POST /api/v1/reminder/action`，请求体为
  `{"action":"start_focus"}`。
- 坐姿和站姿目标高度分别来自 `preset1_height_mm`、`preset4_height_mm`，当前高度来自
  `height_mm`，均转换为一位小数厘米显示。
- 网关离线时按键显示“离线”，不会继续显示陈旧高度。
