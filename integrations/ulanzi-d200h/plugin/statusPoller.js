/**
 * 所有 Action 共用的状态轮询器。
 *
 * pollOnce 使用 in-flight 门禁，网关响应超过一秒时跳过下一轮，避免请求堆积。
 */
export class SharedStatusPoller {
  constructor({ client, intervalMs = 1000, onStatus, onError }) {
    this.client = client;
    this.intervalMs = intervalMs;
    this.onStatus = onStatus;
    this.onError = onError;
    this.timer = null;
    this.inFlight = false;
  }

  start() {
    if (this.timer) return;
    void this.pollOnce();
    this.timer = setInterval(() => void this.pollOnce(), this.intervalMs);
  }

  stop() {
    if (!this.timer) return;
    clearInterval(this.timer);
    this.timer = null;
  }

  async pollOnce() {
    if (this.inFlight) return false;
    this.inFlight = true;
    try {
      const status = await this.client.getStatus();
      this.onStatus(status);
    } catch (error) {
      this.onError(error);
    } finally {
      this.inFlight = false;
    }
    return true;
  }
}

