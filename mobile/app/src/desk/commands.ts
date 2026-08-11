/** Desk Gateway Command Characteristic 的冻结单字节协议。 */

export const DeskCommand = {
  Stop: 0x00,
  HoldUp: 0x01,
  HoldDown: 0x02,
  Preset1: 0x11,
  Preset4: 0x14,
} as const;

export type DeskCommandValue =
  (typeof DeskCommand)[keyof typeof DeskCommand];

const supportedCommands = new Set<number>(Object.values(DeskCommand));

/**
 * 统一生成 GATT Write 数据，避免 UI 或 BLE 适配层自行拼接协议字节。
 */
export function encodeDeskCommand(command: DeskCommandValue): number[] {
  if (!supportedCommands.has(command)) {
    throw new Error(`Unsupported Desk Gateway command: ${command}`);
  }
  return [command];
}
