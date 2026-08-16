/**
 * Desk Gateway 按键展示模型。
 *
 * 这里保持为无副作用函数，便于在没有 UlanziStudio 和 D200H 的环境中验证
 * 高度、倒计时、离线状态与 SVG 输出。
 */
import { ACTIONS, DEFAULT_CONFIG } from "./constants.js";

const PHASE_LABELS = Object.freeze({
  focus: "专注",
  short_break: "短休息",
  long_break: "长休息",
});

/** 清理用户配置，禁止把任意协议交给请求层。 */
export function normalizeConfig(value = {}) {
  const rawUrl = String(value.gateway_url || DEFAULT_CONFIG.gateway_url).trim();
  let gatewayUrl = rawUrl.replace(/\/+$/, "");
  try {
    const parsed = new URL(gatewayUrl);
    if (!(["http:", "https:"].includes(parsed.protocol))) gatewayUrl = "";
  } catch {
    gatewayUrl = "";
  }
  return {
    gateway_url: gatewayUrl,
    api_key: String(value.api_key || "").trim(),
  };
}

/** REST 鉴权要求地址与密钥同时存在，避免持续发送必然失败的轮询。 */
export function isConfigReady(config) {
  return Boolean(config?.gateway_url && config?.api_key);
}

/** 固件以毫米上报，D200H 上使用一位小数的厘米更容易扫读。 */
export function formatHeight(heightMm) {
  return Number.isFinite(heightMm) ? `${(heightMm / 10).toFixed(1)} cm` : "-- cm";
}

/** 倒计时始终截断到非负整数，避免异常快照显示负时间。 */
export function formatCountdown(remainingSec) {
  const total = Math.max(0, Math.floor(Number(remainingSec) || 0));
  const minutes = Math.floor(total / 60);
  const seconds = total % 60;
  return `${String(minutes).padStart(2, "0")}:${String(seconds).padStart(2, "0")}`;
}

function unavailableView(kind, value, detail) {
  return {
    label: ACTIONS[kind].label,
    value,
    detail,
    accent: ACTIONS[kind].accent,
    muted: true,
  };
}

/** 将一次共享状态快照投影成某个 Action 的按键内容。 */
export function createKeyView(kind, state) {
  if (!state.configured) return unavailableView(kind, "未配置", "请填写网关与密钥");
  if (state.online === false) return unavailableView(kind, "离线", "检查局域网连接");
  if (!state.snapshot) return unavailableView(kind, "连接中", "正在读取状态");

  if (kind === "sit" || kind === "stand") {
    const known = state.snapshot.height_known && Number.isFinite(state.snapshot.height_mm);
    return {
      label: ACTIONS[kind].label,
      value: formatHeight(known ? state.snapshot.height_mm : Number.NaN),
      detail: known ? "当前高度" : "高度未知",
      accent: ACTIONS[kind].accent,
      muted: !known,
    };
  }

  const reminder = state.snapshot.reminder;
  if (!reminder?.available) return unavailableView(kind, "不可用", "设备未启用提醒");
  if (reminder.state === "idle") {
    const focusMinutes = Number.isFinite(reminder.focus_minutes) ? reminder.focus_minutes : 25;
    return {
      label: ACTIONS[kind].label,
      value: "开始",
      detail: `${focusMinutes} 分钟专注`,
      accent: ACTIONS[kind].accent,
      muted: false,
    };
  }

  const paused = reminder.state === "paused";
  const phase = PHASE_LABELS[reminder.phase] || "番茄时刻";
  return {
    label: paused ? `${phase} · 已暂停` : phase,
    value: formatCountdown(reminder.remaining_sec),
    detail: reminder.state === "waiting"
      ? "等待处理"
      : reminder.state === "snoozed" ? "稍后提醒" : "设备端倒计时",
    accent: ACTIONS[kind].accent,
    muted: paused,
  };
}

function escapeXml(value) {
  return String(value)
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&apos;");
}

/** 生成 200×200 SVG；宿主缩放到 D200H 按键分辨率时仍保持清晰。 */
export function renderKeySvg(view) {
  const accent = view.muted ? "#697386" : view.accent;
  return `<svg xmlns="http://www.w3.org/2000/svg" width="200" height="200" viewBox="0 0 200 200">
  <rect width="200" height="200" rx="30" fill="#10141D"/>
  <rect x="14" y="14" width="172" height="172" rx="24" fill="#171D29" stroke="${accent}" stroke-width="4"/>
  <circle cx="100" cy="42" r="7" fill="${accent}"/>
  <text x="100" y="72" text-anchor="middle" fill="#F5F7FA" font-family="-apple-system,BlinkMacSystemFont,'PingFang SC','Microsoft YaHei',sans-serif" font-size="24" font-weight="700">${escapeXml(view.label)}</text>
  <text x="100" y="123" text-anchor="middle" fill="${accent}" font-family="-apple-system,BlinkMacSystemFont,'PingFang SC','Microsoft YaHei',sans-serif" font-size="31" font-weight="800">${escapeXml(view.value)}</text>
  <text x="100" y="158" text-anchor="middle" fill="#9BA7B8" font-family="-apple-system,BlinkMacSystemFont,'PingFang SC','Microsoft YaHei',sans-serif" font-size="15">${escapeXml(view.detail)}</text>
</svg>`;
}

export function svgDataUrl(svg) {
  return `data:image/svg+xml;base64,${Buffer.from(svg, "utf8").toString("base64")}`;
}
