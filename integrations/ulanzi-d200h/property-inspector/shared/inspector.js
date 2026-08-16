/** 三个 Action 共用一份 UlanziStudio global settings。 */
const DEFAULT_GATEWAY_URL = "http://desk-gateway.local";
let form;
let statusElement;

// SDK 的调试输出可能包含 didReceiveGlobalSettings；关闭后避免密钥进入 WebView 日志。
Utils.log = () => {};

function settingsFromMessage(message) {
  return message?.settings || message?.param || {};
}

function renderSettings(settings) {
  form.gateway_url.value = settings.gateway_url || DEFAULT_GATEWAY_URL;
  form.api_key.value = settings.api_key || "";
}

function showStatus(text, isError = false) {
  statusElement.textContent = text;
  statusElement.dataset.error = String(isError);
}

function currentSettings() {
  return {
    gateway_url: form.gateway_url.value.trim().replace(/\/+$/, ""),
    api_key: form.api_key.value.trim(),
  };
}

function validate(settings) {
  try {
    const url = new URL(settings.gateway_url);
    if (!(["http:", "https:"].includes(url.protocol))) return "网关地址只支持 HTTP 或 HTTPS";
  } catch {
    return "请输入有效的网关地址";
  }
  if (!settings.api_key) return "请输入 X-Desk-Key";
  return "";
}

$UD.connect(window.DESK_GATEWAY_ACTION_UUID);

$UD.onConnected(() => {
  form = document.querySelector("#desk-gateway-settings");
  statusElement = document.querySelector("#save-status");
  document.querySelector(".panel").classList.remove("hidden");
  renderSettings({});
  $UD.getGlobalSettings();

  form.addEventListener(
    "input",
    Utils.debounce(() => {
      const settings = currentSettings();
      const validationError = validate(settings);
      if (validationError) {
        showStatus(validationError, true);
        return;
      }
      $UD.setGlobalSettings(settings);
      showStatus("配置已保存，三个按钮将共用此设置");
    }, 400),
  );
});

$UD.onDidReceiveGlobalSettings((message) => {
  if (!form) return;
  renderSettings(settingsFromMessage(message));
});
