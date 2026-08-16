/**
 * Desk Gateway REST 客户端。
 *
 * 所有路径都是插件内固定白名单，用户配置只能影响网关 origin 与鉴权密钥，
 * 防止按键动作退化成任意 HTTP 请求器。
 */
import { isConfigReady, normalizeConfig } from "./core.js";

export class DeskGatewayError extends Error {
  constructor(message, { code = "REQUEST_FAILED", status = 0, payload = null } = {}) {
    super(message);
    this.name = "DeskGatewayError";
    this.code = code;
    this.status = status;
    this.payload = payload;
  }
}

export class DeskGatewayClient {
  constructor({ fetchImpl = globalThis.fetch, timeoutMs = 850 } = {}) {
    if (typeof fetchImpl !== "function") throw new TypeError("fetchImpl is required");
    this.fetchImpl = fetchImpl;
    this.timeoutMs = timeoutMs;
    this.config = normalizeConfig();
  }

  configure(value) {
    this.config = normalizeConfig(value);
  }

  async getStatus() {
    return this.request("/api/v1/desk/status");
  }

  async gotoSitting() {
    return this.request("/api/v1/desk/preset/1/goto", { method: "POST" });
  }

  async gotoStanding() {
    return this.request("/api/v1/desk/preset/4/goto", { method: "POST" });
  }

  async startFocus() {
    return this.request("/api/v1/reminder/action", {
      method: "POST",
      body: { action: "start_focus" },
    });
  }

  async request(path, { method = "GET", body } = {}) {
    if (!isConfigReady(this.config)) {
      throw new DeskGatewayError("Desk Gateway 未配置", { code: "CONFIG_REQUIRED" });
    }

    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), this.timeoutMs);
    try {
      const response = await this.fetchImpl(`${this.config.gateway_url}${path}`, {
        method,
        cache: "no-store",
        signal: controller.signal,
        headers: {
          "X-Desk-Key": this.config.api_key,
          ...(body ? { "Content-Type": "application/json" } : {}),
        },
        body: body ? JSON.stringify(body) : undefined,
      });
      const payload = await response.json().catch(() => null);
      if (!response.ok) {
        throw new DeskGatewayError("Desk Gateway 请求失败", {
          code: "HTTP_ERROR",
          status: response.status,
          payload,
        });
      }
      return payload;
    } catch (error) {
      if (error instanceof DeskGatewayError) throw error;
      if (error?.name === "AbortError") {
        throw new DeskGatewayError("Desk Gateway 请求超时", { code: "TIMEOUT" });
      }
      throw new DeskGatewayError("无法连接 Desk Gateway", { code: "OFFLINE" });
    } finally {
      clearTimeout(timeout);
    }
  }
}

/** 将协议错误转换成不泄露地址和密钥的用户提示。 */
export function errorMessage(error) {
  if (error?.code === "CONFIG_REQUIRED") return "请先填写 Desk Gateway 地址和密钥";
  if (error?.status === 401) return "Desk Gateway 密钥错误";
  if (error?.status === 403 && error?.payload?.reason === "child_lock") return "童锁已开启，动作被拒绝";
  if (error?.status === 403) return "REST 控制未启用";
  if (error?.status === 409) return "当前番茄状态不允许此动作";
  if (error?.code === "TIMEOUT") return "Desk Gateway 请求超时";
  return "无法连接 Desk Gateway";
}

