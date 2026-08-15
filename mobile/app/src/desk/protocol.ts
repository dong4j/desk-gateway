/** Desk Gateway State Characteristic v1 的纯 TypeScript 解码器。 */

import type {
  DeskConfig,
  DeskConfigField,
  DeskMotion,
  DeskState,
  ReminderAction,
  ReminderSnapshot,
  AudioSnapshot,
} from './types';

export const DESK_SERVICE_UUID = '7f4e0001-6d4c-4f4b-9f7a-3c1d2e5a9b10';
export const DESK_COMMAND_UUID = '7f4e0002-6d4c-4f4b-9f7a-3c1d2e5a9b10';
export const DESK_STATE_UUID = '7f4e0003-6d4c-4f4b-9f7a-3c1d2e5a9b10';
export const DESK_CONFIG_UUID = '7f4e0004-6d4c-4f4b-9f7a-3c1d2e5a9b10';
export const DESK_SYSTEM_UUID = '7f4e0005-6d4c-4f4b-9f7a-3c1d2e5a9b10';
export const DESK_CLIENT_INFO_UUID = '7f4e0006-6d4c-4f4b-9f7a-3c1d2e5a9b10';
export const DESK_PRESENCE_UUID = '7f4e0007-6d4c-4f4b-9f7a-3c1d2e5a9b10';
export const DESK_REMINDER_UUID = '7f4e0008-6d4c-4f4b-9f7a-3c1d2e5a9b10';
export const DEVICE_INFORMATION_SERVICE_UUID = '180a';
export const FIRMWARE_REVISION_UUID = '2a26';
export const DESK_ADVERTISING_NAME = 'DeskGateway';

const STATE_PACKET_LENGTH = 8;
const CONFIG_V1_PACKET_LENGTH = 4;
const CONFIG_V2_PACKET_LENGTH = 8;
const CONFIG_WRITE_PACKET_LENGTH = 4;
const UNKNOWN_HEIGHT = 0xffff;
const REMINDER_PACKET_LENGTH = 20;

const reminderStateByCode = ['idle', 'running', 'paused', 'waiting', 'snoozed'] as const;
const reminderPhaseByCode = ['focus', 'short_break', 'long_break'] as const;
const reminderAlarmByCode = ['none', 'focus_done', 'break_done'] as const;
const reminderActionCode: Record<ReminderAction, number> = {
  start_focus: 0x00,
  start_break: 0x01,
  pause: 0x02,
  resume: 0x03,
  skip: 0x04,
  stop: 0x05,
  snooze: 0x06,
};

const configFieldCode: Record<DeskConfigField, number> = {
  child_lock: 0x01,
  rest_allowed: 0x02,
  bluetooth_allowed: 0x03,
  panel_allowed: 0x04,
  max_height_mm: 0x05,
  preset1_height_mm: 0x06,
  preset4_height_mm: 0x07,
};

export const DeskSystemCommand = {
  Restart: 0x01,
  ResetController: 0x02,
} as const;

/** Client Info 只上报协议版本和平台类型，不传递设备名称或系统标识。 */
export function encodeDeskClientInfo(clientKind: 0x02 | 0x03): readonly number[] {
  return [0x01, clientKind];
}

const motionByCode: Record<number, DeskMotion> = {
  0x00: 'idle',
  0x01: 'moving_up',
  0x02: 'moving_down',
  0x03: 'goto_preset',
  0x04: 'error',
};

/**
 * 解码固定 8 字节状态。
 *
 * 对长度、版本和状态码采取 fail-closed：协议漂移时宁可显示错误，也不能把错误字节
 * 当成高度或安全状态继续控制桌子。
 */
export function decodeDeskState(bytes: readonly number[]): DeskState {
  if (bytes.length !== STATE_PACKET_LENGTH) {
    throw new Error(
      `Invalid Desk Gateway state length: expected ${STATE_PACKET_LENGTH}, got ${bytes.length}`,
    );
  }
  if (bytes.some((byte) => !Number.isInteger(byte) || byte < 0 || byte > 0xff)) {
    throw new Error('Desk Gateway state contains a non-byte value');
  }

  const protocolVersion = bytes[0];
  if (protocolVersion !== 1) {
    throw new Error(`Unsupported Desk Gateway protocol version: ${protocolVersion}`);
  }

  const motion = motionByCode[bytes[1]];
  if (!motion) {
    throw new Error(`Unknown Desk Gateway motion state: ${bytes[1]}`);
  }

  const flags = bytes[2];
  const rawHeightMm = readUint16LE(bytes, 4);
  const maxHeightMm = readUint16LE(bytes, 6);
  const heightKnownFlag = (flags & (1 << 0)) !== 0;
  const heightKnown = heightKnownFlag && rawHeightMm !== UNKNOWN_HEIGHT;

  return {
    protocolVersion,
    motion,
    heightKnown,
    heightSimulated: (flags & (1 << 1)) !== 0,
    childLock: (flags & (1 << 2)) !== 0,
    bluetoothAllowed: (flags & (1 << 3)) !== 0,
    upwardBlocked: (flags & (1 << 4)) !== 0,
    controllerResetSupported: (flags & (1 << 5)) !== 0,
    controllerResetActive: (flags & (1 << 6)) !== 0,
    controllerResetRecommended: (flags & (1 << 7)) !== 0,
    heightMm: heightKnown ? rawHeightMm : null,
    maxHeightMm,
  };
}

/** Decode the standard Firmware Revision String as strict printable ASCII. */
export function decodeFirmwareRevision(bytes: readonly number[]): string {
  if (bytes.length === 0 || bytes.length > 64) {
    throw new Error(`Invalid firmware revision length: ${bytes.length}`);
  }
  if (bytes.some((byte) => !Number.isInteger(byte) || byte < 0x20 || byte > 0x7e)) {
    throw new Error('Firmware revision contains a non-printable ASCII byte');
  }
  return String.fromCharCode(...bytes);
}

/** 解码设备设置快照；状态必须来自 ESP32 回读，App 不做乐观伪更新。 */
export function decodeDeskConfig(bytes: readonly number[]): DeskConfig {
  if (bytes.length !== CONFIG_V1_PACKET_LENGTH &&
      bytes.length !== CONFIG_V2_PACKET_LENGTH) {
    throw new Error(
      `Invalid Desk Gateway config length: expected ${CONFIG_V1_PACKET_LENGTH} or ${CONFIG_V2_PACKET_LENGTH}, got ${bytes.length}`,
    );
  }
  validateBytes(bytes, 'config');
  const protocolVersion = bytes[0];
  if (protocolVersion !== 1 && protocolVersion !== 2) {
    throw new Error(`Unsupported Desk Gateway config version: ${protocolVersion}`);
  }
  if (protocolVersion === 1 && bytes.length !== CONFIG_V1_PACKET_LENGTH) {
    throw new Error('Desk Gateway config v1 must contain 4 bytes');
  }
  if (protocolVersion === 2 && bytes.length !== CONFIG_V2_PACKET_LENGTH) {
    throw new Error('Desk Gateway config v2 must contain 8 bytes');
  }
  const flags = bytes[1];
  const maxHeightMm = readUint16LE(bytes, 2);
  return {
    protocolVersion,
    childLock: (flags & (1 << 0)) !== 0,
    childLockReason: (flags & (1 << 0)) !== 0 ? 'unknown' : 'none',
    restAllowed: (flags & (1 << 1)) !== 0,
    bluetoothAllowed: (flags & (1 << 2)) !== 0,
    panelAllowed: (flags & (1 << 3)) !== 0,
    maxHeightMm,
    preset1HeightMm: protocolVersion === 2 ? readUint16LE(bytes, 4) : 640,
    preset4HeightMm:
      protocolVersion === 2 ? readUint16LE(bytes, 6) : Math.min(1020, maxHeightMm),
  };
}

/** Presence v1 使用固定 Bond ID，避免不同传输方式产生两套设备身份。 */
export function encodeDeskPresence(deviceId: string): readonly number[] {
  if (!/^bond_[0-9a-f]{12}$/.test(deviceId)) {
    throw new Error('Invalid automatic child-lock device ID');
  }
  return [0x01, ...Array.from(deviceId, (character) => character.charCodeAt(0))];
}

/**
 * Config 写入只携带一个字段，避免使用旧快照覆盖 Web 或其他客户端刚修改的设置。
 */
export function encodeDeskConfigWrite(
  field: DeskConfigField,
  value: boolean | number,
): readonly number[] {
  const numericValue = typeof value === 'boolean' ? (value ? 1 : 0) : value;
  if (
    !Number.isInteger(numericValue) ||
    numericValue < 0 ||
    numericValue > 0xffff
  ) {
    throw new Error(`Invalid Desk Gateway config value: ${numericValue}`);
  }
  const numericField = field === 'max_height_mm' ||
    field === 'preset1_height_mm' || field === 'preset4_height_mm';
  if (!numericField && numericValue > 1) {
    throw new Error(`Invalid boolean config value: ${numericValue}`);
  }
  const packet = [2, configFieldCode[field], 0, 0];
  packet[2] = numericValue & 0xff;
  packet[3] = numericValue >> 8;
  if (packet.length !== CONFIG_WRITE_PACKET_LENGTH) {
    throw new Error('Desk Gateway config encoder produced an invalid packet');
  }
  return packet;
}

/** System 管理命令独立于运动命令，避免重启被误当成桌体动作。 */
export function encodeDeskSystemCommand(command: number): readonly number[] {
  if (command !== DeskSystemCommand.Restart &&
      command !== DeskSystemCommand.ResetController) {
    throw new Error(`Unsupported Desk Gateway system command: ${command}`);
  }
  return [command];
}

/** 解码默认 ATT MTU 下可一次送达的 Reminder v1 快照。 */
export function decodeReminder(
  bytes: readonly number[],
): { reminder: ReminderSnapshot; audio: AudioSnapshot } {
  validatePacket(bytes, REMINDER_PACKET_LENGTH, 'reminder');
  if (bytes[0] !== 1) {
    throw new Error(`Unsupported Desk Gateway reminder version: ${bytes[0]}`);
  }
  const state = reminderStateByCode[bytes[1]];
  const phase = reminderPhaseByCode[bytes[2]];
  const alarmReason = reminderAlarmByCode[bytes[3]];
  if (!state || !phase || !alarmReason) {
    throw new Error('Unknown Desk Gateway reminder state');
  }
  const flags = bytes[4];
  return {
    reminder: {
      protocolVersion: 1,
      available: (flags & 0x01) !== 0,
      state,
      phase,
      alarmReason,
      remainingSec: readUint32LE(bytes, 10),
      completedFocusCount: readUint32LE(bytes, 14),
      config: {
        focusMinutes: bytes[6],
        shortBreakMinutes: bytes[7],
        longBreakMinutes: bytes[8],
        focusesPerLongBreak: bytes[9],
      },
      lastError: null,
    },
    audio: {
      available: (flags & 0x02) !== 0,
      enabled: (flags & 0x04) !== 0,
      playing: (flags & 0x08) !== 0,
      volumePercent: bytes[5],
      voicePack: 'zh-CN-default',
      currentPrompt: null,
      lastError: null,
    },
  };
}

export function encodeReminderAction(action: ReminderAction): readonly number[] {
  return [reminderActionCode[action]];
}

function validatePacket(
  bytes: readonly number[],
  expectedLength: number,
  name: string,
): void {
  if (bytes.length !== expectedLength) {
    throw new Error(
      `Invalid Desk Gateway ${name} length: expected ${expectedLength}, got ${bytes.length}`,
    );
  }
  validateBytes(bytes, name);
}

function validateBytes(bytes: readonly number[], name: string): void {
  if (bytes.some((byte) => !Number.isInteger(byte) || byte < 0 || byte > 0xff)) {
    throw new Error(`Desk Gateway ${name} contains a non-byte value`);
  }
}

function readUint16LE(bytes: readonly number[], offset: number): number {
  return bytes[offset] | (bytes[offset + 1] << 8);
}

function readUint32LE(bytes: readonly number[], offset: number): number {
  return (bytes[offset] |
    (bytes[offset + 1] << 8) |
    (bytes[offset + 2] << 16) |
    (bytes[offset + 3] << 24)) >>> 0;
}
