/**
 * Desk Gateway 设置页。
 *
 * 页面按确认原型呈现完整的信息架构。设备设置只展示 ESP32 Config 回读值，
 * 写入过程中不做本地乐观切换，避免界面显示与实际安全策略不一致。
 */

import { useCallback, useEffect, useRef, useState, type ReactNode } from 'react';
import {
  Alert,
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  TextInput,
  View,
} from 'react-native';
import Slider from '@react-native-community/slider';
import { SafeAreaView } from 'react-native-safe-area-context';

import type {
  DeskClientSnapshot,
  DeskConnectionMode,
  DeskConnectionSettings,
} from '../desk/DeskClient';
import { formatFirmwareBuildTime } from '../desk/formatFirmwareBuildTime';
import {
  DESK_DEFAULT_SIT_HEIGHT_MM,
  DESK_DEFAULT_STAND_HEIGHT_MM,
  DESK_MAX_HEIGHT_MM,
  DESK_MIN_HEIGHT_MM,
} from '../desk/heightPresentation';
import {
  heightPresetErrorMessage,
  heightPresetMmFromCm,
  normalizeHeightPresetName,
} from '../desk/HeightPresets';
import {
  bondErrorMessage,
  bondPollIntervalMs,
  bondStatusText,
  hasBondDeleteConflict,
  isBondManagementConfigured,
  isBondPairingCapacityBlocked,
  normalizeBondAlias,
  recoverBluetoothConnection,
} from '../desk/BondManagement';
import type {
  DeskBondDevice,
  DeskBondSnapshot,
  DeskHeightPresetSnapshot,
} from '../desk/DeskRestClient';
import {
  BluetoothIcon,
  ChevronIcon,
  GlobeIcon,
  HapticIcon,
  LinkIcon,
  LockIcon,
  PanelIcon,
} from '../ui/Icons';
import { PrototypeSwitch } from '../ui/PrototypeSwitch';
import { palette, radii } from '../ui/theme';

const MIN_DESK_HEIGHT_CM = DESK_MIN_HEIGHT_MM / 10;
const MAX_DESK_HEIGHT_CM = DESK_MAX_HEIGHT_MM / 10;
const MAX_HEIGHT_TICKS = [
  56, 60, 65, 70, 75, 80, 85, 90, 94,
] as const;
const MAX_HEIGHT_LABELS = [56, 65, 75, 85, 94] as const;

interface SettingsScreenProps {
  snapshot: DeskClientSnapshot;
  heightPresets: DeskHeightPresetSnapshot | null;
  autoConnect: boolean;
  hapticFeedback: boolean;
  hapticStrength: number;
  connectionMode: DeskConnectionMode;
  restHost: string;
  restKey: string;
  autoLockDeviceId: string;
  onBack: () => void;
  onToggleAutoConnect: () => void;
  onToggleHapticFeedback: () => void;
  onSetHapticStrength: (strength: number) => void;
  onMaxHeightStep: () => void;
  onSetConnectionSettings: (settings: DeskConnectionSettings) => void;
  onGetBluetoothBonds: () => Promise<DeskBondSnapshot>;
  onSetBluetoothPairingWindow: (open: boolean) => Promise<void>;
  onDeleteBluetoothBond: (id: string) => Promise<void>;
  onDeleteAllBluetoothBonds: () => Promise<void>;
  onRenameBluetoothBond: (id: string, alias: string) => Promise<void>;
  onSetAutoChildLock: (deviceId: string, enabled: boolean) => Promise<void>;
  onCreateHeightPreset: (name: string, heightMm: number) => Promise<void>;
  onUpdateHeightPreset: (
    id: string,
    name: string,
    heightMm: number,
  ) => Promise<void>;
  onDeleteHeightPreset: (id: string) => Promise<void>;
  onSetChildLock: (enabled: boolean) => Promise<void>;
  onSetSourceEnabled: (
    source: 'rest' | 'bluetooth' | 'panel',
    enabled: boolean,
  ) => Promise<void>;
  onSetMaxHeightMm: (maxHeightMm: number) => Promise<void>;
  onSetPresetHeightsMm: (
    preset1HeightMm: number,
    preset4HeightMm: number,
  ) => Promise<void>;
  onResetController: () => Promise<void>;
  onRestart: () => Promise<void>;
  onDisconnect: () => void;
}

export function SettingsScreen({
  snapshot,
  heightPresets,
  autoConnect,
  hapticFeedback,
  hapticStrength,
  connectionMode,
  restHost,
  restKey,
  autoLockDeviceId,
  onBack,
  onToggleAutoConnect,
  onToggleHapticFeedback,
  onSetHapticStrength,
  onMaxHeightStep,
  onSetConnectionSettings,
  onGetBluetoothBonds,
  onSetBluetoothPairingWindow,
  onDeleteBluetoothBond,
  onDeleteAllBluetoothBonds,
  onRenameBluetoothBond,
  onSetAutoChildLock,
  onCreateHeightPreset,
  onUpdateHeightPreset,
  onDeleteHeightPreset,
  onSetChildLock,
  onSetSourceEnabled,
  onSetMaxHeightMm,
  onSetPresetHeightsMm,
  onResetController,
  onRestart,
  onDisconnect,
}: SettingsScreenProps) {
  const connected = snapshot.phase === 'ready';
  const config = snapshot.deskConfig;
  const deviceSettingsAvailable = connected && config !== null;
  const maxHeightMm = config?.maxHeightMm ?? snapshot.deskState?.maxHeightMm;
  const maxHeightCm = maxHeightMm ? maxHeightMm / 10 : MAX_DESK_HEIGHT_CM;
  const [maxHeightDraft, setMaxHeightDraft] = useState(String(maxHeightCm));
  const [maxHeightError, setMaxHeightError] = useState<string | null>(null);
  const [maxHeightMessage, setMaxHeightMessage] = useState<string | null>(null);
  const maxHeightTickRef = useRef(Math.round(maxHeightCm));
  const preset1HeightCm = (config?.preset1HeightMm ??
    DESK_DEFAULT_SIT_HEIGHT_MM) / 10;
  const preset4HeightCm = (config?.preset4HeightMm ??
    DESK_DEFAULT_STAND_HEIGHT_MM) / 10;
  const [preset1Draft, setPreset1Draft] = useState(String(preset1HeightCm));
  const [preset4Draft, setPreset4Draft] = useState(String(preset4HeightCm));
  const [presetHeightError, setPresetHeightError] = useState<string | null>(null);
  const [presetHeightMessage, setPresetHeightMessage] = useState<string | null>(null);
  const [deviceOperationBusy, setDeviceOperationBusy] = useState<string | null>(null);
  const [connectionModeDraft, setConnectionModeDraft] = useState(connectionMode);
  const [restHostDraft, setRestHostDraft] = useState(restHost);
  const [restKeyDraft, setRestKeyDraft] = useState(restKey);
  const [connectionError, setConnectionError] = useState<string | null>(null);
  const [bondSnapshot, setBondSnapshot] = useState<DeskBondSnapshot | null>(null);
  const [bondLoading, setBondLoading] = useState(false);
  const [bondBusy, setBondBusy] = useState<string | null>(null);
  const [bondMessage, setBondMessage] = useState<string | null>(null);
  const [recoveryDeviceLabel, setRecoveryDeviceLabel] = useState<string | null>(null);
  const [editingBondId, setEditingBondId] = useState<string | null>(null);
  const [bondAliasDraft, setBondAliasDraft] = useState('');
  const [customPresetNameDraft, setCustomPresetNameDraft] = useState('');
  const [customPresetHeightDraft, setCustomPresetHeightDraft] = useState('');
  const [editingHeightPresetId, setEditingHeightPresetId] =
    useState<string | null>(null);
  const [editingHeightPresetName, setEditingHeightPresetName] = useState('');
  const [editingHeightPresetHeight, setEditingHeightPresetHeight] = useState('');
  const [heightPresetBusy, setHeightPresetBusy] = useState(false);
  const [heightPresetMessage, setHeightPresetMessage] = useState<string | null>(null);
  const firmwareBuildTime = formatFirmwareBuildTime(snapshot.firmwareRevision);
  const bondManagementAvailable = isBondManagementConfigured(restHost, restKey);
  const bondPairingDisabled = !bondManagementAvailable || bondBusy !== null ||
    isBondPairingCapacityBlocked(bondSnapshot);
  const bondDeleteAllDisabled = !bondManagementAvailable || bondBusy !== null ||
    !bondSnapshot?.devices.length || hasBondDeleteConflict(bondSnapshot);
  const selectedAutoLockDeviceId =
    bondSnapshot?.auto_child_lock?.device_id ?? '';
  const autoChildLockEnabled =
    bondSnapshot?.auto_child_lock?.enabled ?? false;
  const selectedAutoLockDevice = bondSnapshot?.devices.find(
    (device) => device.id === selectedAutoLockDeviceId,
  );

  useEffect(() => {
    setMaxHeightDraft(String(maxHeightCm));
    setMaxHeightError(null);
    maxHeightTickRef.current = Math.round(maxHeightCm);
  }, [maxHeightCm]);

  useEffect(() => {
    setPreset1Draft(String(preset1HeightCm));
    setPreset4Draft(String(preset4HeightCm));
    setPresetHeightError(null);
  }, [preset1HeightCm, preset4HeightCm]);

  useEffect(() => {
    setConnectionModeDraft(connectionMode);
    setRestHostDraft(restHost);
    setRestKeyDraft(restKey);
    setConnectionError(null);
  }, [connectionMode, restHost, restKey]);

  const refreshBonds = useCallback(async (): Promise<DeskBondSnapshot | null> => {
    if (!bondManagementAvailable) {
      setBondSnapshot(null);
      return null;
    }
    setBondLoading(true);
    try {
      const next = await onGetBluetoothBonds();
      setBondSnapshot(next);
      return next;
    } catch (error) {
      setBondMessage(bondErrorMessage(error));
      return null;
    } finally {
      setBondLoading(false);
    }
  }, [bondManagementAvailable, onGetBluetoothBonds]);

  useEffect(() => {
    if (!bondManagementAvailable) {
      setBondSnapshot(null);
      setBondMessage(null);
      return;
    }
    let active = true;
    let timer: ReturnType<typeof setTimeout> | null = null;
    const poll = async () => {
      const next = await onGetBluetoothBonds().catch((error) => {
        if (active) {
          setBondMessage(bondErrorMessage(error));
        }
        return null;
      });
      if (!active) {
        return;
      }
      if (next) {
        setBondSnapshot(next);
        setBondMessage(null);
      }
      timer = setTimeout(
        poll,
        next ? bondPollIntervalMs(next) : 5_000,
      );
    };
    void poll();
    return () => {
      active = false;
      if (timer !== null) {
        clearTimeout(timer);
      }
    };
  }, [bondManagementAvailable, onGetBluetoothBonds]);

  const runBondOperation = async (
    key: string,
    operation: () => Promise<void>,
    successMessage: string,
  ) => {
    setBondBusy(key);
    setBondMessage(null);
    try {
      await operation();
      setBondMessage(successMessage);
    } catch (error) {
      setBondMessage(bondErrorMessage(error));
    } finally {
      await refreshBonds();
      setBondBusy(null);
    }
  };

  const confirmDeleteBond = (device: DeskBondDevice) => {
    Alert.alert(
      `删除 ${device.label}？`,
      '在线设备会立即断开；如果这是本机，重新连接前需要先打开配对窗口。',
      [
        { text: '取消', style: 'cancel' },
        {
          text: device.delete_state === 'failed' ? '重试删除' : '删除',
          style: 'destructive',
          onPress: () => void runBondOperation(
            device.id,
            () => onDeleteBluetoothBond(device.id),
            '删除请求已受理，正在刷新设备状态',
          ),
        },
      ],
    );
  };

  const confirmRecoverBond = (device: DeskBondDevice) => {
    const detectorWarning = selectedAutoLockDeviceId === device.id
      ? '\n\n该设备是自动童锁检测设备，恢复后需要重新选择。'
      : '';
    Alert.alert(
      `恢复 ${device.label} 的连接？`,
      `将删除这条旧配对记录并开放 120 秒配对窗口。设备别名需要重新设置。${detectorWarning}`,
      [
        { text: '取消', style: 'cancel' },
        {
          text: '开始恢复',
          onPress: () => void runBondOperation(
            `recover-${device.id}`,
            async () => {
              await recoverBluetoothConnection(
                () => onDeleteBluetoothBond(device.id),
                () => onSetBluetoothPairingWindow(true),
              );
              setRecoveryDeviceLabel(device.label);
            },
            '旧配对信息已清除，已开放 120 秒配对窗口',
          ),
        },
      ],
    );
  };

  const reconnectBluetooth = () => {
    setConnectionModeDraft('ble');
    setBondMessage('正在重新连接蓝牙…');
    onSetConnectionSettings({ mode: 'ble', restHost, restKey });
  };

  const confirmDeleteAllBonds = () => {
    Alert.alert(
      '删除全部蓝牙配对设备？',
      '桌子会先停止，所有在线设备会立即断开。',
      [
        { text: '取消', style: 'cancel' },
        {
          text: '全部删除',
          style: 'destructive',
          onPress: () => void runBondOperation(
            'all',
            onDeleteAllBluetoothBonds,
            '全部删除请求已受理，正在等待设备断开',
          ),
        },
      ],
    );
  };

  const commitBondAlias = (device: DeskBondDevice) => {
    let alias: string;
    try {
      alias = normalizeBondAlias(bondAliasDraft);
    } catch (error) {
      setBondMessage(error instanceof Error ? error.message : String(error));
      return;
    }
    setEditingBondId(null);
    void runBondOperation(
      `rename-${device.id}`,
      () => onRenameBluetoothBond(device.id, alias),
      alias ? '设备别名已更新' : '已恢复默认名称',
    );
  };

  const runDeviceOperation = async (
    key: string,
    operation: () => Promise<void>,
    failureTitle: string,
  ): Promise<boolean> => {
    if (deviceOperationBusy !== null) return false;
    setDeviceOperationBusy(key);
    try {
      await operation();
      return true;
    } catch (error) {
      Alert.alert(
        failureTitle,
        error instanceof Error ? error.message : String(error),
      );
      return false;
    } finally {
      setDeviceOperationBusy(null);
    }
  };

  const commitMaxHeight = (centimetres: number) => {
    if (!Number.isFinite(centimetres) ||
        centimetres < MIN_DESK_HEIGHT_CM ||
        centimetres > MAX_DESK_HEIGHT_CM) {
      setMaxHeightError('请输入 56–94 cm');
      setMaxHeightMessage(null);
      return;
    }
    setMaxHeightError(null);
    setMaxHeightDraft(String(Math.round(centimetres)));
    setMaxHeightMessage('正在保存…');
    void runDeviceOperation(
      'max-height',
      () => onSetMaxHeightMm(Math.round(centimetres * 10)),
      '最高安全高度保存失败',
    ).then((succeeded) => {
      setMaxHeightMessage(succeeded ? '已保存到设备' : null);
    });
  };
  const saveMaxHeight = () => commitMaxHeight(Number(maxHeightDraft));

  const savePresetHeights = () => {
    const preset1Mm = Math.round(Number(preset1Draft) * 10);
    const preset4Mm = Math.round(Number(preset4Draft) * 10);
    if (!Number.isInteger(preset1Mm) || !Number.isInteger(preset4Mm) ||
        preset1Mm < DESK_MIN_HEIGHT_MM || preset1Mm >= preset4Mm ||
        preset4Mm > MAX_DESK_HEIGHT_CM * 10) {
      setPresetHeightError('请坐需低于站立，档位高度范围为 56–94 cm');
      setPresetHeightMessage(null);
      return;
    }
    setPresetHeightError(null);
    setPresetHeightMessage('正在保存…');
    void runDeviceOperation(
      'built-in-presets',
      () => onSetPresetHeightsMm(preset1Mm, preset4Mm),
      '档位高度保存失败',
    ).then((succeeded) => {
      setPresetHeightMessage(succeeded ? '已保存到设备' : null);
    });
  };

  const confirmControllerReset = () => {
    Alert.alert(
      '重置控制盒',
      '仅在控制盒显示 B12 或升降指令发出后高度不变化时使用。请先确认桌子周围没有障碍物。',
      [
        { text: '取消', style: 'cancel' },
        {
          text: '开始重置',
          style: 'destructive',
          onPress: () => void runDeviceOperation(
            'controller-reset',
            onResetController,
            '控制盒重置失败',
          ),
        },
      ],
    );
  };

  const runHeightPresetOperation = async (
    operation: () => Promise<void>,
    successMessage: string,
  ): Promise<boolean> => {
    setHeightPresetBusy(true);
    setHeightPresetMessage(null);
    try {
      await operation();
      setHeightPresetMessage(successMessage);
      return true;
    } catch (error) {
      setHeightPresetMessage(heightPresetErrorMessage(error));
      return false;
    } finally {
      setHeightPresetBusy(false);
    }
  };

  const createCustomHeightPreset = () => {
    let name: string;
    let heightMm: number;
    try {
      name = normalizeHeightPresetName(customPresetNameDraft);
      heightMm = heightPresetMmFromCm(customPresetHeightDraft);
    } catch (error) {
      setHeightPresetMessage(error instanceof Error ? error.message : String(error));
      return;
    }
    void runHeightPresetOperation(
      () => onCreateHeightPreset(name, heightMm),
      '自定义档位已新增',
    ).then((succeeded) => {
      if (succeeded) {
        setCustomPresetNameDraft('');
        setCustomPresetHeightDraft('');
      }
    });
  };

  const saveCustomHeightPreset = (id: string) => {
    let name: string;
    let heightMm: number;
    try {
      name = normalizeHeightPresetName(editingHeightPresetName);
      heightMm = heightPresetMmFromCm(editingHeightPresetHeight);
    } catch (error) {
      setHeightPresetMessage(error instanceof Error ? error.message : String(error));
      return;
    }
    void runHeightPresetOperation(
      () => onUpdateHeightPreset(id, name, heightMm),
      '自定义档位已更新',
    ).then((succeeded) => {
      if (succeeded) setEditingHeightPresetId(null);
    });
  };

  const confirmDeleteHeightPreset = (id: string, name: string) => {
    Alert.alert('删除自定义档位？', `“${name}”删除后不能恢复。`, [
      { text: '取消', style: 'cancel' },
      {
        text: '删除',
        style: 'destructive',
        onPress: () => void runHeightPresetOperation(
          () => onDeleteHeightPreset(id),
          '自定义档位已删除',
        ),
      },
    ]);
  };
  const parsedMaxHeightDraft = Number(maxHeightDraft);
  const minimumAllowedMaxHeightCm = MIN_DESK_HEIGHT_CM;
  const maxHeightSliderValue = Number.isFinite(parsedMaxHeightDraft)
    ? Math.max(
        minimumAllowedMaxHeightCm,
        Math.min(MAX_DESK_HEIGHT_CM, parsedMaxHeightDraft),
      )
    : maxHeightCm;
  const saveConnectionSettings = () => {
    const host = restHostDraft.trim();
    if (connectionModeDraft !== 'ble' && (!host || !restKeyDraft)) {
      setConnectionError('使用自动或局域网模式时，请填写网关地址和 REST 密码');
      return;
    }
    setConnectionError(null);
    onSetConnectionSettings({
      mode: connectionModeDraft,
      restHost: host,
      restKey: restKeyDraft,
    });
  };
  const connectionLabel = snapshot.transport === 'wifi'
    ? 'Wi-Fi · REST'
    : snapshot.transport === 'ble'
      ? 'BLE'
      : '未连接';

  return (
    <SafeAreaView style={styles.safeArea} edges={['top', 'bottom']}>
      <ScrollView
        showsVerticalScrollIndicator={false}
        contentContainerStyle={styles.content}
      >
        <View style={styles.navigation}>
          <Pressable
            accessibilityRole="button"
            accessibilityLabel="返回控制页"
            onPress={onBack}
            hitSlop={10}
            style={({ pressed }) => [styles.navButton, pressed && styles.pressed]}
          >
            <ChevronIcon direction="left" size={29} />
          </Pressable>
          <Text style={styles.navigationTitle}>设置</Text>
          <Pressable
            accessibilityRole="button"
            onPress={onBack}
            hitSlop={10}
            style={({ pressed }) => [styles.doneButton, pressed && styles.pressed]}
          >
            <Text style={styles.doneText}>完成</Text>
          </Pressable>
        </View>

        <View style={styles.deviceCard}>
          <View style={styles.deviceHeader}>
            <Text style={styles.deviceName}>Desk Gateway</Text>
            <View style={[styles.connectionBadge, !connected && styles.connectionBadgeOff]}>
              <View style={[styles.connectionDot, !connected && styles.connectionDotOff]} />
              <Text style={[styles.connectionText, !connected && styles.connectionTextOff]}>
                {connected ? '已连接' : '未连接'}
              </Text>
            </View>
          </View>
          <InfoRow label="当前连接" value={connectionLabel} />
          <InfoRow
            label="固件信息"
            value={firmwareBuildTime ?? '不可用'}
            last
          />
        </View>

        <SectionTitle>连接</SectionTitle>
        <View style={styles.card}>
          <View style={styles.connectionSetting}>
            <Text style={styles.settingTitle}>连接策略</Text>
            <ConnectionModeSelector
              value={connectionModeDraft}
              onChange={setConnectionModeDraft}
            />
            <Text style={styles.connectionHint}>
              自动模式优先使用 BLE；连接失败或超出范围时回退到局域网。
            </Text>
            <Text style={styles.connectionInputLabel}>网关地址</Text>
            <TextInput
              accessibilityLabel="局域网网关地址"
              autoCapitalize="none"
              autoCorrect={false}
              editable={connectionModeDraft !== 'ble'}
              placeholder="desk-gateway.local"
              placeholderTextColor={palette.inkFaint}
              value={restHostDraft}
              onChangeText={setRestHostDraft}
              style={[
                styles.connectionInput,
                connectionModeDraft === 'ble' && styles.disabled,
              ]}
            />
            <Text style={styles.connectionInputLabel}>REST 密码</Text>
            <View style={styles.connectionKeyRow}>
              <TextInput
                accessibilityLabel="REST 登录密码"
                autoCapitalize="none"
                autoCorrect={false}
                editable={connectionModeDraft !== 'ble'}
                secureTextEntry
                value={restKeyDraft}
                onChangeText={setRestKeyDraft}
                onSubmitEditing={saveConnectionSettings}
                style={[
                  styles.connectionInput,
                  styles.connectionKeyInput,
                  connectionModeDraft === 'ble' && styles.disabled,
                ]}
              />
              <Pressable
                accessibilityRole="button"
                onPress={saveConnectionSettings}
                style={({ pressed }) => [
                  styles.connectionSave,
                  pressed && styles.pressed,
                ]}
              >
                <Text style={styles.heightSaveText}>保存并连接</Text>
              </Pressable>
            </View>
            {connectionError ? (
              <Text style={styles.heightError}>{connectionError}</Text>
            ) : null}
          </View>
        </View>

        <View style={[styles.card, styles.bondCard]}>
          <View style={styles.bondHeader}>
            <View>
              <Text style={styles.settingTitle}>蓝牙配对设备</Text>
              <Text style={styles.settingDescription}>
                {bondManagementAvailable
                  ? `${bondSnapshot?.devices.length ?? 0} / ${bondSnapshot?.capacity ?? 3}`
                  : '需先配置局域网管理'}
              </Text>
            </View>
            <Pressable
              accessibilityRole="button"
              disabled={bondPairingDisabled}
              onPress={() => void runBondOperation(
                'pairing',
                () => onSetBluetoothPairingWindow(
                  !(bondSnapshot?.pairing_window.open ?? false),
                ),
                bondSnapshot?.pairing_window.open
                  ? '配对窗口已关闭'
                  : '已开放 120 秒配对窗口',
              )}
              style={({ pressed }) => [
                styles.bondPairingButton,
                bondPairingDisabled && styles.disabled,
                pressed && !bondPairingDisabled && styles.pressed,
              ]}
            >
              <Text style={styles.bondPairingText}>
                {bondSnapshot?.pairing_window.open
                  ? `关闭（${bondSnapshot.pairing_window.remaining_seconds}s）`
                  : '允许新设备配对'}
              </Text>
            </Pressable>
          </View>
          <View style={styles.bondRecoveryIntro}>
            <Text style={styles.bondRecoveryTitle}>连接不上？</Text>
            <Text style={styles.bondRecoveryCopy}>
              找到这部手机的旧记录并点击“恢复”，网关会清除旧密钥并开放
              120 秒配对窗口。
            </Text>
          </View>
          {bondSnapshot?.devices.map((device) => (
            <View key={device.id} style={styles.bondDeviceRow}>
              <View style={styles.bondDeviceCopy}>
                {editingBondId === device.id ? (
                  <TextInput
                    accessibilityLabel="蓝牙设备别名"
                    autoFocus
                    maxLength={48}
                    onChangeText={setBondAliasDraft}
                    onSubmitEditing={() => commitBondAlias(device)}
                    placeholder="留空恢复默认名称"
                    value={bondAliasDraft}
                    style={styles.bondAliasInput}
                  />
                ) : (
                  <Text style={styles.bondDeviceLabel}>{device.label}</Text>
                )}
                <Text style={[
                  styles.bondDeviceStatus,
                  device.delete_state === 'failed' && styles.bondDeviceError,
                ]}>
                  {bondStatusText(device)}
                  {device.delete_error ? ` · ${device.delete_error}` : ''}
                </Text>
                <Pressable
                  accessibilityRole="radio"
                  accessibilityState={{
                    checked: selectedAutoLockDeviceId === device.id,
                    disabled: device.delete_state !== 'idle' || bondBusy !== null,
                  }}
                  disabled={device.delete_state !== 'idle' || bondBusy !== null}
                  onPress={() => void runBondOperation(
                    `auto-lock-${device.id}`,
                    () => onSetAutoChildLock(device.id, autoChildLockEnabled),
                    '已设置为自动童锁检测设备',
                  )}
                  style={({ pressed }) => [
                    styles.autoLockDeviceChoice,
                    pressed && styles.pressed,
                  ]}
                >
                  <View style={[
                    styles.radioOuter,
                    selectedAutoLockDeviceId === device.id && styles.radioOuterActive,
                  ]}>
                    {selectedAutoLockDeviceId === device.id ? (
                      <View style={styles.radioInner} />
                    ) : null}
                  </View>
                  <Text style={styles.autoLockDeviceChoiceText}>
                    {selectedAutoLockDeviceId === device.id
                      ? (autoLockDeviceId === device.id
                          ? '本机检测设备'
                          : '自动童锁检测设备')
                      : '设为检测设备'}
                  </Text>
                </Pressable>
              </View>
              <View style={styles.bondDeviceActions}>
                <Pressable
                  accessibilityRole="button"
                  disabled={device.delete_state !== 'idle' || bondBusy !== null}
                  onPress={() => {
                    if (editingBondId === device.id) {
                      commitBondAlias(device);
                    } else {
                      setEditingBondId(device.id);
                      setBondAliasDraft(device.alias);
                    }
                  }}
                  style={({ pressed }) => [
                    styles.bondRenameButton,
                    (device.delete_state !== 'idle' || bondBusy !== null) &&
                      styles.disabled,
                    pressed && styles.pressed,
                  ]}
                >
                  <Text style={styles.bondRenameText}>
                    {editingBondId === device.id ? '保存' : '重命名'}
                  </Text>
                </Pressable>
                <Pressable
                  accessibilityRole="button"
                  disabled={device.delete_state !== 'idle' || bondBusy !== null}
                  onPress={() => confirmRecoverBond(device)}
                  style={({ pressed }) => [
                    styles.bondRecoveryButton,
                    (device.delete_state !== 'idle' || bondBusy !== null) &&
                      styles.disabled,
                    pressed && styles.pressed,
                  ]}
                >
                  <Text style={styles.bondRecoveryButtonText}>恢复</Text>
                </Pressable>
                <Pressable
                  accessibilityRole="button"
                  disabled={device.delete_state === 'pending' || bondBusy !== null}
                  onPress={() => {
                    if (editingBondId === device.id) {
                      setEditingBondId(null);
                    } else {
                      confirmDeleteBond(device);
                    }
                  }}
                  style={({ pressed }) => [
                    styles.bondDeleteButton,
                    (device.delete_state === 'pending' || bondBusy !== null) &&
                      styles.disabled,
                    pressed && styles.pressed,
                  ]}
                >
                  <Text style={styles.bondDeleteText}>
                    {editingBondId === device.id
                      ? '取消'
                      : device.delete_state === 'failed' ? '重试' : '删除'}
                  </Text>
                </Pressable>
              </View>
            </View>
          ))}
          {recoveryDeviceLabel ? (
            <View style={styles.bondRecoveryReady}>
              <Text style={styles.bondRecoveryTitle}>连接恢复已准备好</Text>
              <Text style={styles.bondRecoveryCopy}>
                1. 在手机“设置 &gt; 蓝牙”中忽略 DeskGateway。{`\n`}
                2. 回到这里，在配对窗口结束前重新连接。{`\n`}
                3. 重新设置 {recoveryDeviceLabel} 的别名和自动童锁检测设备。
              </Text>
              <Text style={styles.bondRecoveryWindow}>
                {bondSnapshot?.pairing_window.open
                  ? `配对窗口剩余 ${bondSnapshot.pairing_window.remaining_seconds} 秒`
                  : '配对窗口已关闭，请先点击“允许新设备配对”'}
              </Text>
              <Pressable
                accessibilityRole="button"
                disabled={!bondSnapshot?.pairing_window.open || bondBusy !== null}
                onPress={reconnectBluetooth}
                style={({ pressed }) => [
                  styles.bondReconnectButton,
                  (!bondSnapshot?.pairing_window.open || bondBusy !== null) &&
                    styles.disabled,
                  pressed && styles.pressed,
                ]}
              >
                <Text style={styles.bondReconnectButtonText}>
                  我已忽略，重新连接
                </Text>
              </Pressable>
            </View>
          ) : null}
          {bondManagementAvailable && !bondLoading &&
              bondSnapshot?.devices.length === 0 ? (
            <Text style={styles.bondEmpty}>暂无蓝牙配对设备</Text>
          ) : null}
          {bondLoading && !bondSnapshot ? (
            <Text style={styles.bondEmpty}>正在加载…</Text>
          ) : null}
          <Pressable
            accessibilityRole="button"
            disabled={bondDeleteAllDisabled}
            onPress={confirmDeleteAllBonds}
            style={({ pressed }) => [
              styles.bondDeleteAll,
              bondDeleteAllDisabled && styles.disabled,
              pressed && !bondDeleteAllDisabled && styles.pressed,
            ]}
          >
            <Text style={styles.bondDeleteAllText}>删除全部配对设备</Text>
          </Pressable>
          {bondMessage ? (
            <Text style={styles.bondMessage}>{bondMessage}</Text>
          ) : null}
          <Text style={styles.bondDeviceHint}>
            请在常带手机上选择其对应设备；只能选择一部。
          </Text>
        </View>

        <SectionTitle>安全</SectionTitle>
        <View style={styles.card}>
          <View style={styles.heightSetting}>
            <View style={styles.rowBetween}>
              <Text style={styles.settingTitle}>最高安全高度</Text>
              <Text style={styles.settingValue}>{maxHeightCm.toFixed(1)} cm</Text>
            </View>
            <Text style={styles.settingDescription}>
              使用 TOF400C 高度，到达该位置后设备会立即停止上升。
            </Text>
            <View style={styles.sliderControl}>
              <Slider
                accessibilityLabel="最高安全高度"
                disabled={!deviceSettingsAvailable || deviceOperationBusy !== null}
                minimumValue={MIN_DESK_HEIGHT_CM}
                maximumValue={MAX_DESK_HEIGHT_CM}
                step={1}
                value={maxHeightSliderValue}
                onValueChange={(value) => {
                  const clamped = Math.max(minimumAllowedMaxHeightCm, value);
                  const nextTick = Math.round(clamped);
                  // Slider 会高频回调；只在实际跨过 1 cm 刻度时反馈一次。
                  if (nextTick !== maxHeightTickRef.current) {
                    maxHeightTickRef.current = nextTick;
                    onMaxHeightStep();
                  }
                  setMaxHeightDraft(String(nextTick));
                }}
                onSlidingComplete={(value) => {
                  commitMaxHeight(Math.max(minimumAllowedMaxHeightCm, value));
                }}
                minimumTrackTintColor={palette.goldSoft}
                maximumTrackTintColor={palette.surfaceMuted}
                thumbTintColor={palette.gold}
                style={styles.nativeSlider}
              />
              <View pointerEvents="none" style={styles.sliderTicks}>
                {MAX_HEIGHT_TICKS.map((value) => (
                  <View
                    key={value}
                    style={[
                      styles.sliderTick,
                      value <= maxHeightSliderValue && styles.sliderTickActive,
                      value < minimumAllowedMaxHeightCm && styles.sliderTickUnavailable,
                      { left: maxHeightTickPosition(value) },
                    ]}
                  />
                ))}
              </View>
              <View pointerEvents="none" style={styles.sliderLabels}>
                {MAX_HEIGHT_LABELS.map((value) => (
                  <Text
                    key={value}
                    style={[
                      styles.sliderLabel,
                      value < minimumAllowedMaxHeightCm && styles.sliderLabelUnavailable,
                      { left: maxHeightTickPosition(value) },
                    ]}
                  >
                    {value}
                  </Text>
                ))}
              </View>
            </View>
            <View style={styles.heightEditor}>
              <TextInput
                accessibilityLabel="最高安全高度，单位厘米"
                editable={deviceSettingsAvailable && deviceOperationBusy === null}
                keyboardType="decimal-pad"
                value={maxHeightDraft}
                onChangeText={setMaxHeightDraft}
                onSubmitEditing={saveMaxHeight}
                style={styles.heightInput}
              />
              <Text style={styles.heightUnit}>cm</Text>
              <Pressable
                accessibilityRole="button"
                disabled={!deviceSettingsAvailable || deviceOperationBusy !== null}
                onPress={saveMaxHeight}
                style={({ pressed }) => [
                  styles.heightSave,
                  (!deviceSettingsAvailable || deviceOperationBusy !== null) &&
                    styles.disabled,
                  pressed && deviceSettingsAvailable &&
                    deviceOperationBusy === null && styles.pressed,
                ]}
              >
                <Text style={styles.heightSaveText}>保存</Text>
              </Pressable>
            </View>
            {maxHeightError ? (
              <Text style={styles.heightError}>{maxHeightError}</Text>
            ) : null}
            {maxHeightMessage ? (
              <Text style={styles.settingFeedback}>{maxHeightMessage}</Text>
            ) : null}
          </View>
          <View style={styles.divider} />
          <View style={styles.presetSetting}>
            <View style={styles.rowBetween}>
              <Text style={styles.settingTitle}>档位高度</Text>
              <Text style={styles.settingDescription}>设备保存，多端同步</Text>
            </View>
            <View style={styles.presetEditorRow}>
              <View style={styles.presetInputGroup}>
                <Text style={styles.presetInputLabel}>请坐</Text>
                <View style={styles.presetInputLine}>
                  <TextInput
                    accessibilityLabel="请坐高度，单位厘米"
                    editable={deviceSettingsAvailable &&
                      deviceOperationBusy === null}
                    keyboardType="number-pad"
                    value={preset1Draft}
                    onChangeText={setPreset1Draft}
                    style={styles.presetInput}
                  />
                  <Text style={styles.heightUnit}>cm</Text>
                </View>
              </View>
              <View style={styles.presetInputGroup}>
                <Text style={styles.presetInputLabel}>站立</Text>
                <View style={styles.presetInputLine}>
                  <TextInput
                    accessibilityLabel="站立高度，单位厘米"
                    editable={deviceSettingsAvailable &&
                      deviceOperationBusy === null}
                    keyboardType="number-pad"
                    value={preset4Draft}
                    onChangeText={setPreset4Draft}
                    onSubmitEditing={savePresetHeights}
                    style={styles.presetInput}
                  />
                  <Text style={styles.heightUnit}>cm</Text>
                </View>
              </View>
              <Pressable
                accessibilityRole="button"
                disabled={!deviceSettingsAvailable || deviceOperationBusy !== null}
                onPress={savePresetHeights}
                style={({ pressed }) => [
                  styles.heightSave,
                  styles.presetSave,
                  (!deviceSettingsAvailable || deviceOperationBusy !== null) &&
                    styles.disabled,
                  pressed && deviceSettingsAvailable &&
                    deviceOperationBusy === null && styles.pressed,
                ]}
              >
                <Text style={styles.heightSaveText}>保存</Text>
              </Pressable>
            </View>
            {presetHeightError ? (
              <Text style={styles.heightError}>{presetHeightError}</Text>
            ) : null}
            {presetHeightMessage ? (
              <Text style={styles.settingFeedback}>{presetHeightMessage}</Text>
            ) : null}
            <Text style={styles.presetBuiltInNote}>内置档位，可以修改高度但不能删除。</Text>
          </View>
          <View style={styles.divider} />
          <View style={styles.customPresetSetting}>
            <View style={styles.rowBetween}>
              <Text style={styles.settingTitle}>自定义档位</Text>
              <Text style={styles.settingDescription}>
                {heightPresets
                  ? `${heightPresets.custom_count} / ${heightPresets.custom_capacity}`
                  : '需局域网管理'}
              </Text>
            </View>
            <View style={styles.customPresetCreateRow}>
              <TextInput
                accessibilityLabel="新档位名称"
                editable={!heightPresetBusy}
                maxLength={48}
                placeholder="例如：午休"
                value={customPresetNameDraft}
                onChangeText={setCustomPresetNameDraft}
                style={[styles.presetInput, styles.customPresetNameInput]}
              />
              <TextInput
                accessibilityLabel="新档位高度，单位厘米"
                editable={!heightPresetBusy}
                keyboardType="decimal-pad"
                placeholder="cm"
                value={customPresetHeightDraft}
                onChangeText={setCustomPresetHeightDraft}
                onSubmitEditing={createCustomHeightPreset}
                style={[styles.presetInput, styles.customPresetHeightInput]}
              />
              <Pressable
                accessibilityRole="button"
                disabled={heightPresetBusy || !heightPresets ||
                  heightPresets.custom_count >= heightPresets.custom_capacity}
                onPress={createCustomHeightPreset}
                style={({ pressed }) => [
                  styles.heightSave,
                  (heightPresetBusy || !heightPresets ||
                    heightPresets.custom_count >= heightPresets.custom_capacity) &&
                    styles.disabled,
                  pressed && styles.pressed,
                ]}
              >
                <Text style={styles.heightSaveText}>新增</Text>
              </Pressable>
            </View>
            {heightPresets?.presets.filter((preset) => !preset.built_in)
              .map((preset) => (
                <View key={preset.id} style={styles.customPresetRow}>
                  {editingHeightPresetId === preset.id ? (
                    <View style={styles.customPresetEditFields}>
                      <TextInput
                        accessibilityLabel="修改档位名称"
                        maxLength={48}
                        value={editingHeightPresetName}
                        onChangeText={setEditingHeightPresetName}
                        style={[styles.presetInput, styles.customPresetNameInput]}
                      />
                      <TextInput
                        accessibilityLabel="修改档位高度，单位厘米"
                        keyboardType="decimal-pad"
                        value={editingHeightPresetHeight}
                        onChangeText={setEditingHeightPresetHeight}
                        style={[styles.presetInput, styles.customPresetHeightInput]}
                      />
                    </View>
                  ) : (
                    <View style={styles.settingCopy}>
                      <Text style={styles.settingTitle}>{preset.name}</Text>
                      <Text style={styles.settingDescription}>
                        {(preset.height_mm / 10).toFixed(1)} cm
                      </Text>
                    </View>
                  )}
                  <View style={styles.customPresetActions}>
                    <Pressable
                      accessibilityRole="button"
                      disabled={heightPresetBusy}
                      onPress={() => {
                        if (editingHeightPresetId === preset.id) {
                          saveCustomHeightPreset(preset.id);
                        } else {
                          setEditingHeightPresetId(preset.id);
                          setEditingHeightPresetName(preset.name);
                          setEditingHeightPresetHeight(
                            (preset.height_mm / 10).toFixed(1),
                          );
                        }
                      }}
                      style={({ pressed }) => [
                        styles.bondRenameButton,
                        heightPresetBusy && styles.disabled,
                        pressed && styles.pressed,
                      ]}
                    >
                      <Text style={styles.bondRenameText}>
                        {editingHeightPresetId === preset.id ? '保存' : '修改'}
                      </Text>
                    </Pressable>
                    <Pressable
                      accessibilityRole="button"
                      disabled={heightPresetBusy}
                      onPress={() => {
                        if (editingHeightPresetId === preset.id) {
                          setEditingHeightPresetId(null);
                        } else {
                          confirmDeleteHeightPreset(preset.id, preset.name);
                        }
                      }}
                      style={({ pressed }) => [
                        styles.bondDeleteButton,
                        heightPresetBusy && styles.disabled,
                        pressed && styles.pressed,
                      ]}
                    >
                      <Text style={styles.bondDeleteText}>
                        {editingHeightPresetId === preset.id ? '取消' : '删除'}
                      </Text>
                    </Pressable>
                  </View>
                </View>
              ))}
            {heightPresets && heightPresets.custom_count === 0 ? (
              <Text style={styles.customPresetEmpty}>暂无自定义档位</Text>
            ) : null}
            {heightPresetMessage ? (
              <Text style={styles.heightError}>{heightPresetMessage}</Text>
            ) : null}
          </View>
          <View style={styles.divider} />
          <Pressable
            accessibilityRole="switch"
            accessibilityLabel="童锁"
            accessibilityState={{
              checked: config?.childLock ?? false,
              disabled: !deviceSettingsAvailable || deviceOperationBusy !== null,
            }}
            disabled={!deviceSettingsAvailable || deviceOperationBusy !== null}
            onPress={() => void runDeviceOperation(
              'child-lock',
              () => onSetChildLock(!(config?.childLock ?? false)),
              '童锁设置失败',
            )}
            style={({ pressed }) => [
              styles.securityRow,
              (!deviceSettingsAvailable || deviceOperationBusy !== null) &&
                styles.disabled,
              pressed && deviceSettingsAvailable &&
                deviceOperationBusy === null && styles.pressed,
            ]}
          >
            <LockIcon size={30} color={palette.gold} />
            <View style={styles.settingCopy}>
              <Text style={styles.settingTitle}>童锁</Text>
              <Text style={styles.settingDescription}>开启后禁止所有入口控制</Text>
            </View>
            <PrototypeSwitch
              value={config?.childLock ?? false}
              muted={!deviceSettingsAvailable || deviceOperationBusy !== null}
            />
          </Pressable>
          <View style={styles.divider} />
          <Pressable
            accessibilityRole="switch"
            accessibilityLabel="自动童锁"
            accessibilityState={{
              checked: autoChildLockEnabled,
              disabled: !selectedAutoLockDevice,
            }}
            disabled={!selectedAutoLockDevice || bondBusy !== null}
            onPress={() => {
              if (!selectedAutoLockDevice) return;
              void runBondOperation(
                'auto-lock-toggle',
                () => onSetAutoChildLock(
                  selectedAutoLockDevice.id,
                  !autoChildLockEnabled,
                ),
                autoChildLockEnabled ? '自动童锁已关闭' : '自动童锁已开启',
              );
            }}
            style={({ pressed }) => [
              styles.securityRow,
              (!selectedAutoLockDevice || bondBusy !== null) && styles.disabled,
              pressed && selectedAutoLockDevice && styles.pressed,
            ]}
          >
            <LockIcon size={30} color={palette.gold} />
            <View style={styles.settingCopy}>
              <Text style={styles.settingTitle}>自动童锁</Text>
              <Text style={styles.settingDescription}>
                {selectedAutoLockDevice
                  ? `${selectedAutoLockDevice.label} 离线 3 分钟后锁定`
                  : '请先选择一部检测设备'}
              </Text>
            </View>
            <PrototypeSwitch
              value={autoChildLockEnabled}
              muted={!selectedAutoLockDevice || bondBusy !== null}
            />
          </Pressable>
        </View>

        <SectionTitle>控制入口</SectionTitle>
        <View style={styles.card}>
          <SettingRow
            icon={<GlobeIcon size={27} />}
            title="REST 接口"
            badge={config ? undefined : '需升级固件'}
            value={config?.restAllowed ?? false}
            disabled={!deviceSettingsAvailable || deviceOperationBusy !== null}
            onPress={() => void runDeviceOperation(
              'source-rest',
              () => onSetSourceEnabled('rest', !(config?.restAllowed ?? false)),
              'REST 入口设置失败',
            )}
          />
          <SettingRow
            icon={<BluetoothIcon size={27} />}
            title="蓝牙"
            value={config?.bluetoothAllowed ?? false}
            disabled={!deviceSettingsAvailable || deviceOperationBusy !== null}
            onPress={() => void runDeviceOperation(
              'source-bluetooth',
              () => onSetSourceEnabled(
                'bluetooth',
                !(config?.bluetoothAllowed ?? false),
              ),
              '蓝牙入口设置失败',
            )}
          />
          <SettingRow
            icon={<PanelIcon size={27} />}
            title="原厂控制面板"
            value={config?.panelAllowed ?? false}
            disabled={!deviceSettingsAvailable || deviceOperationBusy !== null}
            onPress={() => void runDeviceOperation(
              'source-panel',
              () => onSetSourceEnabled('panel', !(config?.panelAllowed ?? false)),
              '原厂面板入口设置失败',
            )}
            last
          />
        </View>

        <SectionTitle>App</SectionTitle>
        <View style={styles.card}>
          <SettingRow
            icon={<LinkIcon size={27} />}
            title="自动连接"
            value={autoConnect}
            onPress={onToggleAutoConnect}
          />
          <SettingRow
            icon={<HapticIcon size={27} />}
            title="触感反馈"
            value={hapticFeedback}
            onPress={onToggleHapticFeedback}
          />
          <HapticLevelSelector
            value={hapticStrength}
            disabled={!hapticFeedback}
            onChange={onSetHapticStrength}
          />
        </View>

        <SectionTitle>设备维护</SectionTitle>
        <Pressable
          accessibilityRole="button"
          disabled={!deviceSettingsAvailable ||
            snapshot.deskState?.controllerResetSupported !== true ||
            snapshot.deskState?.controllerResetActive === true ||
            deviceOperationBusy !== null}
          onPress={confirmControllerReset}
          style={({ pressed }) => [
            styles.actionButton,
            styles.controllerResetButton,
            (!deviceSettingsAvailable ||
              snapshot.deskState?.controllerResetSupported !== true ||
              snapshot.deskState?.controllerResetActive === true ||
              deviceOperationBusy !== null) && styles.disabled,
            pressed && deviceSettingsAvailable &&
              snapshot.deskState?.controllerResetSupported === true &&
              snapshot.deskState?.controllerResetActive !== true &&
              deviceOperationBusy === null && styles.pressed,
          ]}
        >
          <Text style={styles.controllerResetText}>
            {snapshot.deskState?.controllerResetActive
              ? '控制盒正在重置…'
              : '重置控制盒'}
          </Text>
        </Pressable>
        <Text style={styles.maintenanceHint}>
          仅在控制盒显示 B12 或升降指令发出后高度不变化时使用。
        </Text>
        <Pressable
          accessibilityRole="button"
          disabled={!deviceSettingsAvailable || deviceOperationBusy !== null}
          onPress={() => void runDeviceOperation(
            'restart',
            onRestart,
            '网关重启失败',
          )}
          style={({ pressed }) => [
            styles.actionButton,
            styles.restartButton,
            (!deviceSettingsAvailable || deviceOperationBusy !== null) &&
              styles.disabled,
            pressed && deviceSettingsAvailable &&
              deviceOperationBusy === null && styles.pressed,
          ]}
        >
          <Text style={styles.restartText}>重启网关</Text>
        </Pressable>
        <Pressable
          accessibilityRole="button"
          disabled={!connected}
          onPress={onDisconnect}
          style={({ pressed }) => [
            styles.actionButton,
            !connected && styles.disabled,
            pressed && connected && styles.pressed,
          ]}
        >
          <Text style={styles.disconnectText}>断开连接</Text>
        </Pressable>
        <Text style={styles.footer}>
          {config
            ? '设备设置以 ESP32 回读结果为准'
            : '当前连接暂未返回设备配置，请重新连接'}
        </Text>
      </ScrollView>
    </SafeAreaView>
  );
}

/** 三种模式是连接策略而不是瞬时状态，当前实际通道在设备卡片中单独显示。 */
function ConnectionModeSelector({
  value,
  onChange,
}: {
  value: DeskConnectionMode;
  onChange: (value: DeskConnectionMode) => void;
}) {
  const modes = [
    { label: '自动', value: 'auto' },
    { label: 'BLE', value: 'ble' },
    { label: '局域网', value: 'wifi' },
  ] as const;
  return (
    <View style={styles.connectionModes}>
      {modes.map((mode) => {
        const active = mode.value === value;
        return (
          <Pressable
            key={mode.value}
            accessibilityRole="radio"
            accessibilityState={{ checked: active }}
            onPress={() => onChange(mode.value)}
            style={({ pressed }) => [
              styles.connectionMode,
              active && styles.connectionModeActive,
              pressed && styles.pressed,
            ]}
          >
            <Text style={[
              styles.connectionModeText,
              active && styles.connectionModeTextActive,
            ]}>
              {mode.label}
            </Text>
          </Pressable>
        );
      })}
    </View>
  );
}

function InfoRow({
  label,
  value,
  last = false,
}: {
  label: string;
  value: string;
  last?: boolean;
}) {
  return (
    <View style={[styles.infoRow, !last && styles.rowBorder]}>
      <Text style={styles.infoLabel}>{label}</Text>
      <View style={styles.infoValueGroup}>
        <Text style={styles.infoValue} numberOfLines={1}>{value}</Text>
      </View>
    </View>
  );
}

/** 三档触感比伪连续滑块更符合系统实际提供的离散震动等级。 */
function HapticLevelSelector({
  value,
  disabled,
  onChange,
}: {
  value: number;
  disabled: boolean;
  onChange: (value: number) => void;
}) {
  const levels = [
    { label: '轻', value: 30 },
    { label: '中', value: 70 },
    { label: '强', value: 100 },
  ] as const;
  const selected = value < 50 ? 30 : value < 85 ? 70 : 100;

  return (
    <View style={[styles.hapticLevelSetting, disabled && styles.disabled]}>
      <Text style={styles.hapticLevelTitle}>震动强度</Text>
      <View style={styles.hapticSegments}>
        {levels.map((level) => {
          const active = selected === level.value;
          return (
            <Pressable
              key={level.value}
              accessibilityRole="radio"
              accessibilityState={{ checked: active, disabled }}
              disabled={disabled}
              onPress={() => onChange(level.value)}
              style={({ pressed }) => [
                styles.hapticSegment,
                active && styles.hapticSegmentActive,
                pressed && !disabled && styles.pressed,
              ]}
            >
              <Text style={[
                styles.hapticSegmentText,
                active && styles.hapticSegmentTextActive,
              ]}>
                {level.label}
              </Text>
            </Pressable>
          );
        })}
      </View>
    </View>
  );
}

/** Map a centimetre mark onto the fixed 56–94 cm raw-ToF scale. */
function maxHeightTickPosition(value: number): `${number}%` {
  const progress =
    (value - MIN_DESK_HEIGHT_CM) /
    (MAX_DESK_HEIGHT_CM - MIN_DESK_HEIGHT_CM);
  return `${progress * 100}%`;
}

function SectionTitle({ children }: { children: ReactNode }) {
  return <Text style={styles.sectionTitle}>{children}</Text>;
}

function SettingRow({
  icon,
  title,
  badge,
  value,
  disabled = false,
  onPress,
  last = false,
}: {
  icon: ReactNode;
  title: string;
  badge?: string;
  value: boolean;
  disabled?: boolean;
  onPress: () => void;
  last?: boolean;
}) {
  return (
    <Pressable
      accessibilityRole="switch"
      accessibilityLabel={title}
      accessibilityState={{ checked: value, disabled }}
      disabled={disabled}
      onPress={onPress}
      style={({ pressed }) => [
        styles.settingRow,
        !last && styles.rowBorder,
        disabled && styles.disabled,
        pressed && !disabled && styles.pressed,
      ]}
    >
      <View style={styles.settingIcon}>{icon}</View>
      <Text style={styles.settingRowTitle}>{title}</Text>
      {badge ? (
        <View style={styles.badge}>
          <Text style={styles.badgeText}>{badge}</Text>
        </View>
      ) : null}
      <View style={styles.settingSpacer} />
      <PrototypeSwitch
        value={value}
        muted={disabled}
      />
    </Pressable>
  );
}

const styles = StyleSheet.create({
  safeArea: { flex: 1, backgroundColor: palette.background },
  content: { flexGrow: 1, paddingHorizontal: 22, paddingTop: 4, paddingBottom: 24 },
  navigation: { height: 58, flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between' },
  navigationTitle: { color: palette.ink, fontSize: 22, fontWeight: '700' },
  navButton: { width: 42, height: 42, justifyContent: 'center' },
  doneButton: { minWidth: 52, height: 42, alignItems: 'flex-end', justifyContent: 'center' },
  doneText: { color: palette.gold, fontSize: 18, fontWeight: '600' },
  deviceCard: { marginTop: 13, overflow: 'hidden', borderWidth: 1, borderColor: palette.line, borderRadius: radii.medium, backgroundColor: palette.surface },
  deviceHeader: { minHeight: 72, paddingHorizontal: 18, flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', borderBottomWidth: 1, borderBottomColor: palette.line },
  deviceName: { color: palette.ink, fontSize: 22, fontWeight: '700' },
  connectionBadge: { height: 36, paddingHorizontal: 13, flexDirection: 'row', alignItems: 'center', gap: 8, borderRadius: radii.pill, backgroundColor: palette.greenSurface },
  connectionBadgeOff: { backgroundColor: palette.surfaceMuted },
  connectionDot: { width: 9, height: 9, borderRadius: 5, backgroundColor: palette.green },
  connectionDotOff: { backgroundColor: palette.inkFaint },
  connectionText: { color: palette.greenInk, fontSize: 16, fontWeight: '500' },
  connectionTextOff: { color: palette.inkMuted },
  infoRow: { minHeight: 58, paddingHorizontal: 18, flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between' },
  rowBorder: { borderBottomWidth: 1, borderBottomColor: palette.line },
  infoLabel: { color: palette.ink, fontSize: 17 },
  infoValueGroup: { flex: 1, marginLeft: 18, flexDirection: 'row', alignItems: 'center', justifyContent: 'flex-end', gap: 8 },
  infoValue: { flexShrink: 1, color: palette.inkMuted, fontSize: 15, lineHeight: 20, textAlign: 'right' },
  sectionTitle: { marginTop: 24, marginBottom: 8, marginLeft: 4, color: palette.inkMuted, fontSize: 19, fontWeight: '600' },
  card: { overflow: 'hidden', borderWidth: 1, borderColor: palette.line, borderRadius: radii.medium, backgroundColor: palette.surface },
  connectionSetting: { padding: 18 },
  connectionModes: { marginTop: 14, flexDirection: 'row', padding: 3, borderRadius: radii.pill, backgroundColor: palette.surfaceMuted },
  connectionMode: { flex: 1, height: 36, alignItems: 'center', justifyContent: 'center', borderRadius: radii.pill },
  connectionModeActive: { backgroundColor: palette.ink },
  connectionModeText: { color: palette.inkMuted, fontSize: 14, fontWeight: '600' },
  connectionModeTextActive: { color: palette.white },
  connectionHint: { marginTop: 11, color: palette.inkMuted, fontSize: 12, lineHeight: 18 },
  connectionInputLabel: { marginTop: 14, marginBottom: 6, color: palette.inkMuted, fontSize: 13 },
  connectionInput: { height: 42, paddingHorizontal: 12, borderWidth: 1, borderColor: palette.line, borderRadius: radii.small, backgroundColor: palette.white, color: palette.ink, fontSize: 15 },
  connectionKeyRow: { flexDirection: 'row', gap: 9 },
  connectionKeyInput: { flex: 1 },
  connectionSave: { minWidth: 108, height: 42, paddingHorizontal: 14, alignItems: 'center', justifyContent: 'center', borderRadius: radii.pill, backgroundColor: palette.ink },
  bondCard: { marginTop: 12 },
  bondHeader: { minHeight: 78, paddingHorizontal: 18, paddingVertical: 15, flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', gap: 12 },
  bondPairingButton: { minHeight: 36, paddingHorizontal: 12, alignItems: 'center', justifyContent: 'center', borderRadius: radii.pill, backgroundColor: palette.ink },
  bondPairingText: { color: palette.white, fontSize: 12, fontWeight: '600' },
  bondDeviceRow: { minHeight: 64, paddingHorizontal: 18, paddingVertical: 10, flexDirection: 'row', alignItems: 'center', borderTopWidth: 1, borderTopColor: palette.line },
  bondDeviceCopy: { flex: 1, paddingRight: 12 },
  bondDeviceLabel: { color: palette.ink, fontSize: 16, fontWeight: '500' },
  bondAliasInput: { minHeight: 36, paddingHorizontal: 10, borderWidth: 1, borderColor: palette.line, borderRadius: radii.small, color: palette.ink, backgroundColor: palette.surface },
  bondDeviceStatus: { marginTop: 3, color: palette.inkMuted, fontSize: 12 },
  autoLockDeviceChoice: { marginTop: 7, flexDirection: 'row', alignItems: 'center', gap: 7 },
  autoLockDeviceChoiceText: { color: palette.inkMuted, fontSize: 12, fontWeight: '600' },
  radioOuter: { width: 16, height: 16, alignItems: 'center', justifyContent: 'center', borderWidth: 1, borderColor: palette.inkMuted, borderRadius: 8 },
  radioOuterActive: { borderColor: palette.gold },
  radioInner: { width: 8, height: 8, borderRadius: 4, backgroundColor: palette.gold },
  bondDeviceError: { color: palette.danger },
  bondDeviceActions: { maxWidth: 190, flexDirection: 'row', flexWrap: 'wrap', justifyContent: 'flex-end', gap: 6 },
  bondRenameButton: { minWidth: 58, minHeight: 34, alignItems: 'center', justifyContent: 'center', borderWidth: 1, borderColor: palette.ink, borderRadius: radii.pill },
  bondRenameText: { color: palette.ink, fontSize: 13, fontWeight: '600' },
  bondDeleteButton: { minWidth: 58, minHeight: 34, alignItems: 'center', justifyContent: 'center', borderWidth: 1, borderColor: palette.danger, borderRadius: radii.pill },
  bondDeleteText: { color: palette.danger, fontSize: 13, fontWeight: '600' },
  bondRecoveryIntro: { marginHorizontal: 18, marginBottom: 8, padding: 12, borderWidth: 1, borderColor: palette.line, borderRadius: radii.small, backgroundColor: palette.surfaceMuted },
  bondRecoveryReady: { marginHorizontal: 18, marginTop: 8, padding: 14, borderWidth: 1, borderColor: palette.gold, borderRadius: radii.small, backgroundColor: palette.surfaceMuted },
  bondRecoveryTitle: { color: palette.ink, fontSize: 14, fontWeight: '700' },
  bondRecoveryCopy: { marginTop: 4, color: palette.inkMuted, fontSize: 12, lineHeight: 19 },
  bondRecoveryWindow: { marginTop: 8, color: palette.ink, fontSize: 12, fontWeight: '600' },
  bondRecoveryButton: { minWidth: 58, minHeight: 34, alignItems: 'center', justifyContent: 'center', borderWidth: 1, borderColor: palette.gold, borderRadius: radii.pill },
  bondRecoveryButtonText: { color: palette.ink, fontSize: 13, fontWeight: '600' },
  bondReconnectButton: { minHeight: 38, marginTop: 10, alignItems: 'center', justifyContent: 'center', borderRadius: radii.pill, backgroundColor: palette.ink },
  bondReconnectButtonText: { color: palette.white, fontSize: 13, fontWeight: '600' },
  bondEmpty: { paddingHorizontal: 18, paddingVertical: 18, borderTopWidth: 1, borderTopColor: palette.line, color: palette.inkMuted, fontSize: 13, textAlign: 'center' },
  bondDeleteAll: { minHeight: 48, marginHorizontal: 18, marginTop: 8, alignItems: 'center', justifyContent: 'center', borderTopWidth: 1, borderTopColor: palette.line },
  bondDeleteAllText: { color: palette.danger, fontSize: 15, fontWeight: '600' },
  bondMessage: { paddingHorizontal: 18, paddingBottom: 14, color: palette.inkMuted, fontSize: 12, lineHeight: 18, textAlign: 'center' },
  bondDeviceHint: { paddingHorizontal: 18, paddingBottom: 14, color: palette.inkFaint, fontSize: 12, lineHeight: 18, textAlign: 'center' },
  heightSetting: { paddingHorizontal: 18, paddingTop: 18, paddingBottom: 17 },
  rowBetween: { flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between' },
  settingTitle: { color: palette.ink, fontSize: 17, fontWeight: '500' },
  settingValue: { color: palette.ink, fontSize: 17 },
  sliderControl: { height: 66, marginTop: 13, marginHorizontal: 12 },
  nativeSlider: { width: '100%', height: 32 },
  sliderTicks: { position: 'absolute', left: 15, right: 15, top: 31, height: 10 },
  sliderTick: { position: 'absolute', width: 1, height: 5, backgroundColor: palette.line },
  sliderTickActive: { height: 8, backgroundColor: palette.goldSoft },
  sliderTickUnavailable: { height: 4, backgroundColor: palette.disabled },
  sliderLabels: { position: 'absolute', left: 15, right: 15, top: 43, height: 20 },
  sliderLabel: { position: 'absolute', width: 32, marginLeft: -16, color: palette.inkMuted, fontSize: 11, textAlign: 'center' },
  sliderLabelUnavailable: { color: palette.inkFaint },
  heightEditor: { marginTop: 16, flexDirection: 'row', alignItems: 'center', gap: 8 },
  heightInput: { width: 82, height: 40, paddingHorizontal: 12, borderWidth: 1, borderColor: palette.line, borderRadius: radii.small, backgroundColor: palette.white, color: palette.ink, fontSize: 16, textAlign: 'center' },
  heightUnit: { color: palette.inkMuted, fontSize: 15 },
  heightSave: { minWidth: 66, height: 40, marginLeft: 'auto', alignItems: 'center', justifyContent: 'center', borderRadius: radii.pill, backgroundColor: palette.ink },
  heightSaveText: { color: palette.white, fontSize: 15, fontWeight: '600' },
  heightError: { marginTop: 7, color: palette.danger, fontSize: 12 },
  settingFeedback: { marginTop: 7, color: palette.greenInk, fontSize: 12 },
  presetSetting: { paddingHorizontal: 18, paddingTop: 17, paddingBottom: 17 },
  presetEditorRow: { marginTop: 14, flexDirection: 'row', alignItems: 'flex-end', gap: 10 },
  presetInputGroup: { flex: 1 },
  presetInputLabel: { marginBottom: 6, color: palette.inkMuted, fontSize: 13 },
  presetInputLine: { flexDirection: 'row', alignItems: 'center', gap: 5 },
  presetInput: { flex: 1, minWidth: 0, height: 40, paddingHorizontal: 10, borderWidth: 1, borderColor: palette.line, borderRadius: radii.small, backgroundColor: palette.white, color: palette.ink, fontSize: 16, textAlign: 'center' },
  presetSave: { minWidth: 62 },
  presetBuiltInNote: { marginTop: 9, color: palette.inkMuted, fontSize: 12 },
  customPresetSetting: { paddingHorizontal: 18, paddingTop: 17, paddingBottom: 17 },
  customPresetCreateRow: { marginTop: 14, flexDirection: 'row', alignItems: 'center', gap: 8 },
  customPresetNameInput: { flex: 1, textAlign: 'left' },
  customPresetHeightInput: { width: 76, flex: 0 },
  customPresetRow: { minHeight: 62, paddingVertical: 10, flexDirection: 'row', alignItems: 'center', borderTopWidth: 1, borderTopColor: palette.line },
  customPresetEditFields: { flex: 1, paddingRight: 8, flexDirection: 'row', gap: 7 },
  customPresetActions: { flexDirection: 'row', gap: 6 },
  customPresetEmpty: { paddingTop: 14, color: palette.inkMuted, fontSize: 13, textAlign: 'center' },
  divider: { height: 1, backgroundColor: palette.line },
  securityRow: { minHeight: 78, paddingHorizontal: 18, flexDirection: 'row', alignItems: 'center', gap: 15 },
  settingCopy: { flex: 1 },
  settingDescription: { marginTop: 3, color: palette.inkMuted, fontSize: 13 },
  settingRow: { minHeight: 64, paddingHorizontal: 17, flexDirection: 'row', alignItems: 'center' },
  settingIcon: { width: 42, alignItems: 'flex-start' },
  settingRowTitle: { color: palette.ink, fontSize: 17, fontWeight: '500' },
  settingSpacer: { flex: 1 },
  hapticLevelSetting: { paddingHorizontal: 18, paddingTop: 14, paddingBottom: 16 },
  hapticLevelTitle: { marginBottom: 10, color: palette.inkMuted, fontSize: 13 },
  hapticSegments: { flexDirection: 'row', padding: 3, borderRadius: radii.pill, backgroundColor: palette.surfaceMuted },
  hapticSegment: { flex: 1, height: 34, alignItems: 'center', justifyContent: 'center', borderRadius: radii.pill },
  hapticSegmentActive: { backgroundColor: palette.ink },
  hapticSegmentText: { color: palette.inkMuted, fontSize: 14, fontWeight: '600' },
  hapticSegmentTextActive: { color: palette.white },
  badge: { marginLeft: 9, paddingHorizontal: 8, paddingVertical: 4, borderWidth: 1, borderColor: palette.goldSoft, borderRadius: radii.pill },
  badgeText: { color: palette.gold, fontSize: 11 },
  actionButton: { minHeight: 54, marginTop: 13, alignItems: 'center', justifyContent: 'center', borderWidth: 1, borderColor: palette.line, borderRadius: radii.medium, backgroundColor: palette.surface },
  controllerResetButton: { borderColor: palette.danger },
  controllerResetText: { color: palette.danger, fontSize: 17, fontWeight: '500' },
  maintenanceHint: { marginTop: 7, color: palette.inkMuted, fontSize: 12, lineHeight: 18, textAlign: 'center' },
  restartButton: { borderColor: palette.gold },
  restartText: { color: palette.gold, fontSize: 17, fontWeight: '500' },
  disconnectText: { color: palette.danger, fontSize: 17, fontWeight: '500' },
  footer: { marginTop: 17, color: palette.inkFaint, fontSize: 12, lineHeight: 18, textAlign: 'center' },
  pressed: { opacity: 0.68 },
  disabled: { opacity: 0.4 },
});
