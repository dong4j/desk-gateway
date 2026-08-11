/** 按住升降期间的震动节奏控制器。 */

export type HoldHapticEvent = 'start' | 'pulse' | 'end';

type HapticEmitter = (event: HoldHapticEvent) => void;

/**
 * 将“开始、周期脉冲、结束”生命周期与平台震动 API 解耦。
 *
 * 页面退到后台或 BLE 断开时使用 cancel，避免在失去控制权后继续震动；正常松手使用
 * stop，让用户得到一次明确的结束反馈。
 */
export class HoldHapticController {
  private timer: ReturnType<typeof setInterval> | null = null;
  private active = false;
  private pulseIntervalMs: number;

  constructor(
    private readonly emit: HapticEmitter,
    pulseIntervalMs = 300,
  ) {
    this.pulseIntervalMs = pulseIntervalMs;
  }

  /** 下一次长按开始时使用新的节奏；不打断正在进行的桌面运动。 */
  setPulseIntervalMs(pulseIntervalMs: number): void {
    this.pulseIntervalMs = Math.max(120, Math.round(pulseIntervalMs));
  }

  start(): void {
    this.cancel();
    this.active = true;
    this.emit('start');
    this.timer = setInterval(() => this.emit('pulse'), this.pulseIntervalMs);
  }

  stop(): void {
    if (!this.active) {
      return;
    }
    this.active = false;
    this.clearTimer();
    this.emit('end');
  }

  cancel(): void {
    this.active = false;
    this.clearTimer();
  }

  private clearTimer(): void {
    if (this.timer !== null) {
      clearInterval(this.timer);
      this.timer = null;
    }
  }
}
