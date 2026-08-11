/** react-native-ble-manager 的平台适配实现。 */

import {
  PermissionsAndroid,
  Platform,
  type Permission,
} from 'react-native';
import BleManager, {
  BleScanMode,
  BleState,
  type Peripheral,
} from 'react-native-ble-manager';

import type {
  BleAdapter,
  BytesListener,
  DisconnectListener,
  Unsubscribe,
} from './BleAdapter';
import type { DeskPeripheral } from '../desk/types';

const BLE_READY_TIMEOUT_MS = 5_000;

export class ReactNativeBleManagerAdapter implements BleAdapter {
  private initialized = false;

  async initialize(): Promise<void> {
    if (this.initialized) {
      return;
    }

    await requestAndroidBlePermissions();
    await BleManager.start({ showAlert: false });
    await waitForBluetoothReady(BLE_READY_TIMEOUT_MS);
    this.initialized = true;
  }

  scanForPeripheral(
    serviceUuid: string,
    advertisingName: string,
    timeoutMs: number,
  ): Promise<DeskPeripheral> {
    return new Promise((resolve, reject) => {
      let settled = false;
      const finish = (peripheral?: Peripheral, error?: unknown) => {
        if (settled) {
          return;
        }
        settled = true;
        clearTimeout(timeout);
        discoverySubscription.remove();
        void BleManager.stopScan().catch(() => undefined);

        if (error) {
          reject(error);
          return;
        }
        if (!peripheral) {
          reject(new Error('DeskGateway scan ended without a peripheral'));
          return;
        }

        resolve({
          id: peripheral.id,
          name: peripheral.name ?? peripheral.advertising.localName ?? null,
          rssi: peripheral.rssi,
        });
      };

      const discoverySubscription = BleManager.onDiscoverPeripheral(
        (peripheral) => {
          const name = peripheral.name ?? peripheral.advertising.localName;
          if (name === advertisingName) {
            finish(peripheral);
          }
        },
      );

      const timeout = setTimeout(
        () => finish(undefined, new Error('DeskGateway scan timed out')),
        timeoutMs,
      );

      BleManager.scan({
        serviceUUIDs: [serviceUuid],
        seconds: Math.max(1, Math.ceil(timeoutMs / 1000)),
        allowDuplicates: false,
        scanMode: BleScanMode.Balanced,
      }).catch((error: unknown) => finish(undefined, error));
    });
  }

  async connect(peripheralId: string): Promise<void> {
    await BleManager.connect(peripheralId, { autoconnect: false });
  }

  async discoverServices(
    peripheralId: string,
    serviceUuids: string[],
  ): Promise<void> {
    await BleManager.retrieveServices(peripheralId, serviceUuids);
  }

  async ensureBond(peripheralId: string): Promise<void> {
    // iOS 由 CoreBluetooth 在访问加密特征时弹出系统配对；Android 可以显式建 bond。
    if (Platform.OS === 'android') {
      await BleManager.createBond(peripheralId);
    }
  }

  read(
    peripheralId: string,
    serviceUuid: string,
    characteristicUuid: string,
  ): Promise<number[]> {
    return BleManager.read(peripheralId, serviceUuid, characteristicUuid);
  }

  async write(
    peripheralId: string,
    serviceUuid: string,
    characteristicUuid: string,
    bytes: readonly number[],
  ): Promise<void> {
    await BleManager.write(
      peripheralId,
      serviceUuid,
      characteristicUuid,
      Array.from(bytes),
    );
  }

  async subscribe(
    peripheralId: string,
    serviceUuid: string,
    characteristicUuid: string,
    listener: BytesListener,
  ): Promise<Unsubscribe> {
    const normalizedService = normalizeUuid(serviceUuid);
    const normalizedCharacteristic = normalizeUuid(characteristicUuid);
    const subscription = BleManager.onDidUpdateValueForCharacteristic(
      (event) => {
        if (
          event.peripheral === peripheralId &&
          normalizeUuid(event.service) === normalizedService &&
          normalizeUuid(event.characteristic) === normalizedCharacteristic
        ) {
          listener(event.value);
        }
      },
    );

    try {
      await BleManager.startNotification(
        peripheralId,
        serviceUuid,
        characteristicUuid,
      );
    } catch (error) {
      subscription.remove();
      throw error;
    }

    return () => {
      subscription.remove();
      void BleManager.stopNotification(
        peripheralId,
        serviceUuid,
        characteristicUuid,
      ).catch(() => undefined);
    };
  }

  onDisconnect(listener: DisconnectListener): Unsubscribe {
    const subscription = BleManager.onDisconnectPeripheral((event) => {
      listener(event.peripheral);
    });
    return () => subscription.remove();
  }

  async disconnect(peripheralId: string): Promise<void> {
    await BleManager.disconnect(peripheralId);
  }
}

function normalizeUuid(uuid: string): string {
  return uuid.toLowerCase();
}

/**
 * 等待原生蓝牙管理器完成异步初始化。
 *
 * iOS 创建 CBCentralManager 后会先报告 unknown，再异步切换到 on。这里先订阅状态事件，
 * 再主动查询当前值，避免把正常的过渡状态误判成初始化失败，也避免错过查询期间的状态变化。
 */
function waitForBluetoothReady(timeoutMs: number): Promise<void> {
  return new Promise((resolve, reject) => {
    let settled = false;
    let lastState = BleState.Unknown;
    let timeout: ReturnType<typeof setTimeout> | null = null;
    let subscription: ReturnType<typeof BleManager.onDidUpdateState> | null =
      null;

    const finish = (error?: Error) => {
      if (settled) {
        return;
      }
      settled = true;
      if (timeout) {
        clearTimeout(timeout);
      }
      subscription?.remove();

      if (error) {
        reject(error);
      } else {
        resolve();
      }
    };

    const acceptState = (state: BleState) => {
      lastState = state;
      if (state === BleState.On) {
        finish();
        return;
      }

      // unknown/resetting 是 iOS CoreBluetooth 初始化期间的正常过渡状态。
      if (state !== BleState.Unknown && state !== BleState.Resetting) {
        finish(bluetoothStateError(state));
      }
    };

    subscription = BleManager.onDidUpdateState(({ state }) => {
      acceptState(state);
    });
    // 防御同步事件实现：如果订阅期间已经结束，需要立即清理刚创建的订阅。
    if (settled) {
      subscription.remove();
      return;
    }

    timeout = setTimeout(() => {
      finish(
        new Error(
          `Bluetooth did not become ready within ${timeoutMs} ms: ${lastState}`,
        ),
      );
    }, timeoutMs);

    void BleManager.checkState().then(
      (state) => acceptState(state),
      (error: unknown) =>
        finish(
          error instanceof Error
            ? error
            : new Error(`Unable to read Bluetooth state: ${String(error)}`),
        ),
    );
  });
}

/** 把不可恢复的蓝牙状态转换为可直接展示的诊断信息。 */
function bluetoothStateError(state: BleState): Error {
  switch (state) {
    case BleState.Off:
      return new Error('Bluetooth is turned off');
    case BleState.Unauthorized:
      return new Error('Bluetooth permission is not authorized');
    case BleState.Unsupported:
      return new Error('Bluetooth Low Energy is not supported');
    default:
      return new Error(`Bluetooth is not ready: ${state}`);
  }
}

async function requestAndroidBlePermissions(): Promise<void> {
  if (Platform.OS !== 'android') {
    return;
  }

  const permissions: Permission[] =
    Number(Platform.Version) >= 31
      ? [
          PermissionsAndroid.PERMISSIONS.BLUETOOTH_SCAN,
          PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT,
        ]
      : [PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION];

  const result = await PermissionsAndroid.requestMultiple(permissions);
  const denied = permissions.some(
    (permission) => result[permission] !== PermissionsAndroid.RESULTS.GRANTED,
  );
  if (denied) {
    throw new Error('Bluetooth permission was denied');
  }
}
