/**
 * Ulanzi D200H Desk Gateway 插件入口。
 *
 * 三个 Action 共享网关配置和状态快照；按键实例只负责动作分发与绘制，
 * 高度及番茄倒计时始终以设备端 `/api/v1/desk/status` 为准。
 */
import UlanziApi, { Utils } from "../../../common-node/index.js";
import { ACTION_KIND_BY_UUID, PLUGIN_UUID } from "./constants.js";
import { createKeyView, isConfigReady, normalizeConfig, renderKeySvg, svgDataUrl } from "./core.js";
import { DeskGatewayClient, errorMessage } from "./deskGatewayClient.js";
import { SharedStatusPoller } from "./statusPoller.js";

// common-node 默认把完整入站事件写入日志，其中可能包含 global settings。
// 本插件关闭 SDK 调试日志并覆写 send，确保 X-Desk-Key 不进入控制台。
Utils.log = () => {};
class SecureUlanziApi extends UlanziApi {
  send(cmd, params) {
    if (this.websocket?.readyState !== 1) return;
    this.websocket.send(JSON.stringify({
      cmd,
      uuid: this.uuid,
      key: this.key,
      actionid: this.actionid,
      ...params,
    }));
  }
}

const $UD = new SecureUlanziApi();
const client = new DeskGatewayClient();
const instances = new Map();
const runningActions = new Set();

let config = normalizeConfig();
let statusState = { configured: false, online: null, snapshot: null };

const poller = new SharedStatusPoller({
  client,
  intervalMs: 1000,
  onStatus(snapshot) {
    statusState = { configured: true, online: true, snapshot };
    renderAll();
  },
  onError() {
    statusState = { ...statusState, configured: isConfigReady(config), online: false };
    renderAll();
  },
});

function actionKind(message) {
  const uuid = message?.uuid || (message?.context ? $UD.decodeContext(message.context).uuid : "");
  return ACTION_KIND_BY_UUID[uuid];
}

function renderInstance(context, instance) {
  if (!instance.active) return;
  const view = createKeyView(instance.kind, statusState);
  $UD.setBaseDataIcon(context, svgDataUrl(renderKeySvg(view)));
}

function renderAll() {
  for (const [context, instance] of instances) renderInstance(context, instance);
}

function syncPolling() {
  const hasVisibleKey = [...instances.values()].some((instance) => instance.active);
  if (hasVisibleKey && isConfigReady(config)) poller.start();
  else poller.stop();
}

function applyConfig(value) {
  config = normalizeConfig(value);
  client.configure(config);
  statusState = {
    configured: isConfigReady(config),
    online: null,
    snapshot: null,
  };
  renderAll();
  syncPolling();
}

async function runAction(message) {
  const context = message.context;
  const instance = instances.get(context);
  if (!instance || runningActions.has(context)) return;
  if (!isConfigReady(config)) {
    $UD.toast("请先填写 Desk Gateway 地址和密钥");
    return;
  }
  const reminderState = statusState.snapshot?.reminder?.state;
  if (instance.kind === "pomodoro" && reminderState && reminderState !== "idle") {
    $UD.toast("番茄时刻正在进行");
    return;
  }

  runningActions.add(context);
  try {
    if (instance.kind === "sit") await client.gotoSitting();
    else if (instance.kind === "stand") await client.gotoStanding();
    else await client.startFocus();
    $UD.toast(instance.kind === "sit" ? "正在前往坐姿" : instance.kind === "stand" ? "正在前往站姿" : "番茄时刻已开始");
    await poller.pollOnce();
  } catch (error) {
    $UD.toast(errorMessage(error));
  } finally {
    runningActions.delete(context);
  }
}

$UD.connect(PLUGIN_UUID);

$UD.onAdd((message) => {
  const kind = actionKind(message);
  if (!kind) return;
  instances.set(message.context, { kind, active: true });
  renderInstance(message.context, instances.get(message.context));
  // SDK 模拟器 39069 端口的 getGlobalSettings handler 存在未定义变量缺陷；
  // 模拟器由属性面板实时下发配置，真实 UlanziStudio 仍主动读取持久化设置。
  if (Number($UD.port) !== 39069) $UD.getGlobalSettings(message.context);
  syncPolling();
});

$UD.onSetActive((message) => {
  const instance = instances.get(message.context);
  if (!instance) return;
  instance.active = String(message.active) === "true" || message.active === true;
  renderInstance(message.context, instance);
  syncPolling();
});

$UD.onRun((message) => void runAction(message));

$UD.onClear((message) => {
  for (const item of message.param || []) {
    instances.delete(item.context);
    runningActions.delete(item.context);
  }
  syncPolling();
});

$UD.onDidReceiveGlobalSettings((message) => {
  applyConfig(message.settings || message.param || {});
});
