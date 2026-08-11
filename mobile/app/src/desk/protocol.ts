/** Desk Gateway State Characteristic v1 的纯 TypeScript 解码器。 */

import type {
  DeskConfig,
  DeskConfigField,
  DeskMotion,
  DeskState,
} from './types';

export const DESK_SERVICE_UUID = '7f4e0001-6d4c-4f4b-9f7a-3c1d2e5a9b10';
export const DESK_COMMAND_UUID = '7f4e0002-6d4c-4f4b-9f7a-3c1d2e5a9b10';
export const DESK_STATE_UUID = '7f4e0003-6d4c-4f4b-9f7a-3c1d2e5a9b10';
export const DESK_CONFIG_UUID = '7f4e0004-6d4c-4f4b-9f7a-3c1d2e5a9b10';
export const DESK_SYSTEM_UUID = '7f4e0005-6d4c-4f4b-9f7a-3c1d2e5a9b10';
export const DEVICE_INFORMATION_SERVICE_UUID = '180a';
export const FIRMWARE_REVISION_UUID = '2a26';
export const DESK_ADVERTISING_NAME = 'DeskGateway';

const STATE_PACKET_LENGTH = 8;
const CONFIG_PACKET_LENGTH = 4;
const CONFIG_WRITE_PACKET_LENGTH = 4;
const UNKNOWN_HEIGHT = 0xffff;

const configFieldCode: Record<DeskConfigField, number> = {
  child_lock: 0x01,
  rest_allowed: 0x02,
  bluetooth_allowed: 0x03,
  panel_allowed: 0x04,
  max_height_mm: 0x05,
};

export const DeskSystemCommand = {
  Restart: 0x01,
} as const;

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
  validatePacket(bytes, CONFIG_PACKET_LENGTH, 'config');
  const protocolVersion = bytes[0];
  if (protocolVersion !== 1) {
    throw new Error(`Unsupported Desk Gateway config version: ${protocolVersion}`);
  }
  const flags = bytes[1];
  return {
    protocolVersion,
    childLock: (flags & (1 << 0)) !== 0,
    restAllowed: (flags & (1 << 1)) !== 0,
    bluetoothAllowed: (flags & (1 << 2)) !== 0,
    panelAllowed: (flags & (1 << 3)) !== 0,
    maxHeightMm: readUint16LE(bytes, 2),
  };
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
  if (field !== 'max_height_mm' && numericValue > 1) {
    throw new Error(`Invalid boolean config value: ${numericValue}`);
  }
  const packet = [1, configFieldCode[field], 0, 0];
  packet[2] = numericValue & 0xff;
  packet[3] = numericValue >> 8;
  if (packet.length !== CONFIG_WRITE_PACKET_LENGTH) {
    throw new Error('Desk Gateway config encoder produced an invalid packet');
  }
  return packet;
}

/** System 管理命令独立于运动命令，避免重启被误当成桌体动作。 */
export function encodeDeskSystemCommand(command: number): readonly number[] {
  if (command !== DeskSystemCommand.Restart) {
    throw new Error(`Unsupported Desk Gateway system command: ${command}`);
  }
  return [command];
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
  if (bytes.some((byte) => !Number.isInteger(byte) || byte < 0 || byte > 0xff)) {
    throw new Error(`Desk Gateway ${name} contains a non-byte value`);
  }
}

function readUint16LE(bytes: readonly number[], offset: number): number {
  return bytes[offset] | (bytes[offset + 1] << 8);
}
