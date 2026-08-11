/** Desk Gateway HOLD 命令的客户端续期与松手停止控制器。 */

import {
  DeskCommand,
  type DeskCommandValue,
} from './commands';

type CommandSender = (command: DeskCommandValue) => Promise<void>;

export class DeskHoldController {
  private timer: ReturnType<typeof setInterval> | null = null;
  private activeCommand: DeskCommandValue | null = null;
  private writeInFlight = false;

  constructor(
    private readonly send: CommandSender,
    private readonly renewIntervalMs = 300,
  ) {}

  async start(command: DeskCommandValue): Promise<void> {
    if (command !== DeskCommand.HoldUp && command !== DeskCommand.HoldDown) {
      throw new Error('DeskHoldController only accepts HOLD commands');
    }

    this.clearTimer();
    this.activeCommand = command;
    try {
      await this.renew();
    } catch (error) {
      this.activeCommand = null;
      throw error;
    }
    if (this.activeCommand !== command) {
      return;
    }

    this.timer = setInterval(() => {
      void this.renew().catch(() => this.stop());
    }, this.renewIntervalMs);
  }

  async stop(): Promise<void> {
    const wasActive = this.activeCommand !== null || this.timer !== null;
    this.activeCommand = null;
    this.clearTimer();
    if (wasActive) {
      await this.send(DeskCommand.Stop).catch(() => undefined);
    }
  }

  private async renew(): Promise<void> {
    const command = this.activeCommand;
    if (command === null || this.writeInFlight) {
      return;
    }

    this.writeInFlight = true;
    try {
      await this.send(command);
    } finally {
      this.writeInFlight = false;
    }
  }

  private clearTimer(): void {
    if (this.timer !== null) {
      clearInterval(this.timer);
      this.timer = null;
    }
  }
}
