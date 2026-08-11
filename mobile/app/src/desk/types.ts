/** Desk Gateway GATT v1 对外状态类型。 */

export type DeskMotion =
  | 'idle'
  | 'moving_up'
  | 'moving_down'
  | 'goto_preset'
  | 'error';

export interface DeskState {
  protocolVersion: number;
  motion: DeskMotion;
  heightKnown: boolean;
  heightSimulated: boolean;
  childLock: boolean;
  bluetoothAllowed: boolean;
  upwardBlocked: boolean;
  heightMm: number | null;
  maxHeightMm: number;
}

/** Config characteristic 返回的设备持久化设置。 */
export interface DeskConfig {
  protocolVersion: number;
  childLock: boolean;
  restAllowed: boolean;
  bluetoothAllowed: boolean;
  panelAllowed: boolean;
  maxHeightMm: number;
  preset1HeightMm: number;
  preset4HeightMm: number;
}

export type DeskConfigField =
  | 'child_lock'
  | 'rest_allowed'
  | 'bluetooth_allowed'
  | 'panel_allowed'
  | 'max_height_mm'
  | 'preset1_height_mm'
  | 'preset4_height_mm';

export interface DeskPeripheral {
  id: string;
  name: string | null;
  rssi: number;
}
