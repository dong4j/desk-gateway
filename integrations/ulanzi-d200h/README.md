# Desk Gateway Ulanzi D200H plugin

**Language:** English · [简体中文](./README.zh-CN.md)

This directory is plugin **source only**. It does not vendor the Ulanzi SDK, third-party runtimes, or build output. Do not copy this tree straight into the UlanziStudio plugins folder. Download official `UlanziDeckPlugin-SDK` before compiling.

Three keys:

- **Sit:** preset 1. Shows sit target height and a once-per-second live height.
- **Stand:** preset 4. Shows stand target height and live height.
- **Pomodoro:** starts the device-side focus phase and shows remaining time from the gateway.

All three keys share one `GET /api/v1/desk/status` poll. The countdown comes from Desk Gateway. The plugin does not run a second timer on the computer.

Parent index: [`../README.md`](../README.md).

## Build requirements

- Node.js 20 or newer
- npm
- [UlanziDeckPlugin-SDK](https://github.com/UlanziTechnology/UlanziDeckPlugin-SDK)
- UlanziStudio 2.1.4 or newer

## Place source inside the SDK

```bash
git clone https://github.com/UlanziTechnology/UlanziDeckPlugin-SDK.git
```

Copy this directory into the SDK `demo/` tree using the manifest plugin id:

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

From the desk-gateway repo root:

```bash
cp -R integrations/ulanzi-d200h \
  /path/to/UlanziDeckPlugin-SDK/demo/com.ulanzi.deskgateway.ulanziPlugin
```

If the target already exists, make sure you do not need those edits, then replace it. Do not nest the source twice.

## Build and test

```bash
cd /path/to/UlanziDeckPlugin-SDK/demo/com.ulanzi.deskgateway.ulanziPlugin
npm ci
npm run package
npm test
```

`npm run package`:

1. Copies Property Inspector runtime from SDK `common-html/js`.
2. Builds the Node.js host from SDK `common-node`.
3. Writes an installable plugin directory.

Installable output:

```text
release/com.ulanzi.deskgateway.ulanziPlugin/
```

That directory must contain `manifest.json`, `package.json`, `dist/app.js`, `libs/`, property inspectors, and icons. `package.json` is required so Node treats `dist/app.js` as ESM.

## Install into UlanziStudio

Quit UlanziStudio completely, then copy the installable directory.

macOS:

```text
~/Library/Application Support/Ulanzi/UlanziDeck/Plugins/
```

Windows:

```text
%APPDATA%\Ulanzi\UlanziDeck\Plugins\
```

Final layout:

```text
Plugins/
└── com.ulanzi.deskgateway.ulanziPlugin/
    ├── manifest.json
    ├── package.json
    ├── dist/app.js
    ├── libs/
    └── property-inspector/
```

Reopen UlanziStudio, connect the D200H, refresh plugins, and drag Sit / Stand / Pomodoro from the `Desk Gateway` category onto three keys.

## Configure Desk Gateway

On any key’s property inspector:

- Gateway URL, default `http://desk-gateway.local`
- `X-Desk-Key`: current Desk Gateway password

Settings are shared across the three keys and stored in UlanziStudio global settings. They are not written into source or logs. If `.local` does not resolve, use a LAN IP such as `http://192.168.1.100`.

The computer, the D200H’s UlanziStudio host, and Desk Gateway must be able to reach each other. UlanziStudio must stay running.

## API mapping

- Sit always calls `POST /api/v1/desk/preset/1/goto`.
- Stand always calls `POST /api/v1/desk/preset/4/goto`.
- Pomodoro always calls `POST /api/v1/reminder/action` with `{"action":"start_focus"}`.
- Sit/stand targets come from `preset1_height_mm` / `preset4_height_mm`; live height from `height_mm`, shown as one decimal centimetre.
- Offline keys show “离线” and do not keep a stale height.
