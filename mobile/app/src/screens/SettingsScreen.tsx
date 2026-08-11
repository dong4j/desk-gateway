/**
 * Desk Gateway 设置页。
 *
 * 页面按确认原型呈现完整的信息架构。设备设置只展示 ESP32 Config 回读值，
 * 写入过程中不做本地乐观切换，避免界面显示与实际安全策略不一致。
 */

import { useEffect, useState, type ReactNode } from 'react';
import {
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  TextInput,
  View,
} from 'react-native';
import Slider from '@react-native-community/slider';
import { SafeAreaView } from 'react-native-safe-area-context';

import type { DeskClientSnapshot } from '../desk/DeskBleClient';
import { formatFirmwareBuildTime } from '../desk/formatFirmwareBuildTime';
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

interface SettingsScreenProps {
  snapshot: DeskClientSnapshot;
  autoConnect: boolean;
  hapticFeedback: boolean;
  hapticStrength: number;
  onBack: () => void;
  onToggleAutoConnect: () => void;
  onToggleHapticFeedback: () => void;
  onSetHapticStrength: (strength: number) => void;
  onSetChildLock: (enabled: boolean) => void;
  onSetSourceEnabled: (
    source: 'rest' | 'bluetooth' | 'panel',
    enabled: boolean,
  ) => void;
  onSetMaxHeightMm: (maxHeightMm: number) => void;
  onSetPresetHeightsMm: (
    preset1HeightMm: number,
    preset4HeightMm: number,
  ) => void;
  onRestart: () => void;
  onDisconnect: () => void;
}

export function SettingsScreen({
  snapshot,
  autoConnect,
  hapticFeedback,
  hapticStrength,
  onBack,
  onToggleAutoConnect,
  onToggleHapticFeedback,
  onSetHapticStrength,
  onSetChildLock,
  onSetSourceEnabled,
  onSetMaxHeightMm,
  onSetPresetHeightsMm,
  onRestart,
  onDisconnect,
}: SettingsScreenProps) {
  const connected = snapshot.phase === 'ready';
  const config = snapshot.deskConfig;
  const deviceSettingsAvailable = connected && config !== null;
  const maxHeightMm = config?.maxHeightMm ?? snapshot.deskState?.maxHeightMm;
  const maxHeightCm = maxHeightMm ? maxHeightMm / 10 : 110;
  const [maxHeightDraft, setMaxHeightDraft] = useState(String(maxHeightCm));
  const [maxHeightError, setMaxHeightError] = useState<string | null>(null);
  const preset1HeightCm = (config?.preset1HeightMm ?? 640) / 10;
  const preset4HeightCm = (config?.preset4HeightMm ?? 1020) / 10;
  const [preset1Draft, setPreset1Draft] = useState(String(preset1HeightCm));
  const [preset4Draft, setPreset4Draft] = useState(String(preset4HeightCm));
  const [presetHeightError, setPresetHeightError] = useState<string | null>(null);
  const firmwareBuildTime = formatFirmwareBuildTime(snapshot.firmwareRevision);

  useEffect(() => {
    setMaxHeightDraft(String(maxHeightCm));
    setMaxHeightError(null);
  }, [maxHeightCm]);

  useEffect(() => {
    setPreset1Draft(String(preset1HeightCm));
    setPreset4Draft(String(preset4HeightCm));
    setPresetHeightError(null);
  }, [preset1HeightCm, preset4HeightCm]);

  const saveMaxHeight = () => {
    const centimetres = Number(maxHeightDraft);
    if (!Number.isFinite(centimetres) || centimetres < 64 || centimetres > 129) {
      setMaxHeightError('请输入 64–129 cm');
      return;
    }
    if (centimetres < preset4HeightCm) {
      setMaxHeightError(`最高安全高度不能低于站立档位 ${preset4HeightCm} cm`);
      return;
    }
    setMaxHeightError(null);
    onSetMaxHeightMm(Math.round(centimetres * 10));
  };

  const savePresetHeights = () => {
    const preset1Mm = Math.round(Number(preset1Draft) * 10);
    const preset4Mm = Math.round(Number(preset4Draft) * 10);
    if (!Number.isInteger(preset1Mm) || !Number.isInteger(preset4Mm) ||
        preset1Mm < 640 || preset1Mm >= preset4Mm ||
        preset4Mm > maxHeightCm * 10) {
      setPresetHeightError('请坐需低于站立，且站立不得超过最高安全高度');
      return;
    }
    setPresetHeightError(null);
    onSetPresetHeightsMm(preset1Mm, preset4Mm);
  };
  const parsedMaxHeightDraft = Number(maxHeightDraft);
  const maxHeightSliderValue = Number.isFinite(parsedMaxHeightDraft)
    ? Math.max(64, Math.min(129, parsedMaxHeightDraft))
    : maxHeightCm;

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
          <InfoRow label="连接方式" value="BLE" />
          <InfoRow
            label="固件构建时间"
            value={firmwareBuildTime ?? '不可用'}
            last
          />
        </View>

        <SectionTitle>安全</SectionTitle>
        <View style={styles.card}>
          <View style={styles.heightSetting}>
            <View style={styles.rowBetween}>
              <Text style={styles.settingTitle}>最高安全高度</Text>
              <Text style={styles.settingValue}>{maxHeightCm.toFixed(1)} cm</Text>
            </View>
            <View style={styles.sliderRow}>
              <Text style={styles.sliderEdge}>64</Text>
              <Slider
                accessibilityLabel="最高安全高度"
                disabled={!deviceSettingsAvailable}
                minimumValue={64}
                maximumValue={129}
                step={1}
                value={maxHeightSliderValue}
                onValueChange={(value) => setMaxHeightDraft(value.toFixed(0))}
                minimumTrackTintColor={palette.goldSoft}
                maximumTrackTintColor={palette.surfaceMuted}
                thumbTintColor={palette.gold}
                style={styles.nativeSlider}
              />
              <Text style={styles.sliderEdge}>129</Text>
            </View>
            <View style={styles.heightEditor}>
              <TextInput
                accessibilityLabel="最高安全高度，单位厘米"
                editable={deviceSettingsAvailable}
                keyboardType="decimal-pad"
                value={maxHeightDraft}
                onChangeText={setMaxHeightDraft}
                onSubmitEditing={saveMaxHeight}
                style={styles.heightInput}
              />
              <Text style={styles.heightUnit}>cm</Text>
              <Pressable
                accessibilityRole="button"
                disabled={!deviceSettingsAvailable}
                onPress={saveMaxHeight}
                style={({ pressed }) => [
                  styles.heightSave,
                  !deviceSettingsAvailable && styles.disabled,
                  pressed && deviceSettingsAvailable && styles.pressed,
                ]}
              >
                <Text style={styles.heightSaveText}>保存</Text>
              </Pressable>
            </View>
            {maxHeightError ? (
              <Text style={styles.heightError}>{maxHeightError}</Text>
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
                    editable={deviceSettingsAvailable}
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
                    editable={deviceSettingsAvailable}
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
                disabled={!deviceSettingsAvailable}
                onPress={savePresetHeights}
                style={({ pressed }) => [
                  styles.heightSave,
                  styles.presetSave,
                  !deviceSettingsAvailable && styles.disabled,
                  pressed && deviceSettingsAvailable && styles.pressed,
                ]}
              >
                <Text style={styles.heightSaveText}>保存</Text>
              </Pressable>
            </View>
            {presetHeightError ? (
              <Text style={styles.heightError}>{presetHeightError}</Text>
            ) : null}
          </View>
          <View style={styles.divider} />
          <Pressable
            accessibilityRole="switch"
            accessibilityLabel="童锁"
            accessibilityState={{
              checked: config?.childLock ?? false,
              disabled: !deviceSettingsAvailable,
            }}
            disabled={!deviceSettingsAvailable}
            onPress={() => onSetChildLock(!(config?.childLock ?? false))}
            style={({ pressed }) => [
              styles.securityRow,
              !deviceSettingsAvailable && styles.disabled,
              pressed && deviceSettingsAvailable && styles.pressed,
            ]}
          >
            <LockIcon size={30} color={palette.gold} />
            <View style={styles.settingCopy}>
              <Text style={styles.settingTitle}>童锁</Text>
              <Text style={styles.settingDescription}>开启后禁止所有入口控制</Text>
            </View>
            <PrototypeSwitch
              value={config?.childLock ?? false}
              muted={!deviceSettingsAvailable}
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
            disabled={!deviceSettingsAvailable}
            onPress={() =>
              onSetSourceEnabled('rest', !(config?.restAllowed ?? false))
            }
          />
          <SettingRow
            icon={<BluetoothIcon size={27} />}
            title="蓝牙"
            value={config?.bluetoothAllowed ?? false}
            disabled={!deviceSettingsAvailable}
            onPress={() =>
              onSetSourceEnabled(
                'bluetooth',
                !(config?.bluetoothAllowed ?? false),
              )
            }
          />
          <SettingRow
            icon={<PanelIcon size={27} />}
            title="原厂控制面板"
            badge="待硬件验证"
            value={config?.panelAllowed ?? false}
            disabled={!deviceSettingsAvailable}
            onPress={() =>
              onSetSourceEnabled('panel', !(config?.panelAllowed ?? false))
            }
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

        <Pressable
          accessibilityRole="button"
          disabled={!deviceSettingsAvailable}
          onPress={onRestart}
          style={({ pressed }) => [
            styles.actionButton,
            styles.restartButton,
            !deviceSettingsAvailable && styles.disabled,
            pressed && deviceSettingsAvailable && styles.pressed,
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
            : '当前固件不支持 BLE Config，请升级并重新连接'}
        </Text>
      </ScrollView>
    </SafeAreaView>
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
  heightSetting: { paddingHorizontal: 18, paddingTop: 18, paddingBottom: 17 },
  rowBetween: { flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between' },
  settingTitle: { color: palette.ink, fontSize: 17, fontWeight: '500' },
  settingValue: { color: palette.ink, fontSize: 17 },
  sliderRow: { marginTop: 20, flexDirection: 'row', alignItems: 'center', gap: 13 },
  sliderEdge: { width: 28, color: palette.inkMuted, fontSize: 14 },
  nativeSlider: { flex: 1, height: 32 },
  heightEditor: { marginTop: 16, flexDirection: 'row', alignItems: 'center', gap: 8 },
  heightInput: { width: 82, height: 40, paddingHorizontal: 12, borderWidth: 1, borderColor: palette.line, borderRadius: radii.small, backgroundColor: palette.white, color: palette.ink, fontSize: 16 },
  heightUnit: { color: palette.inkMuted, fontSize: 15 },
  heightSave: { minWidth: 66, height: 40, marginLeft: 'auto', alignItems: 'center', justifyContent: 'center', borderRadius: radii.pill, backgroundColor: palette.ink },
  heightSaveText: { color: palette.white, fontSize: 15, fontWeight: '600' },
  heightError: { marginTop: 7, color: palette.danger, fontSize: 12 },
  presetSetting: { paddingHorizontal: 18, paddingTop: 17, paddingBottom: 17 },
  presetEditorRow: { marginTop: 14, flexDirection: 'row', alignItems: 'flex-end', gap: 10 },
  presetInputGroup: { flex: 1 },
  presetInputLabel: { marginBottom: 6, color: palette.inkMuted, fontSize: 13 },
  presetInputLine: { flexDirection: 'row', alignItems: 'center', gap: 5 },
  presetInput: { flex: 1, minWidth: 0, height: 40, paddingHorizontal: 10, borderWidth: 1, borderColor: palette.line, borderRadius: radii.small, backgroundColor: palette.white, color: palette.ink, fontSize: 16 },
  presetSave: { minWidth: 62 },
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
  restartButton: { marginTop: 26, borderColor: palette.gold },
  restartText: { color: palette.gold, fontSize: 17, fontWeight: '500' },
  disconnectText: { color: palette.danger, fontSize: 17, fontWeight: '500' },
  footer: { marginTop: 17, color: palette.inkFaint, fontSize: 12, lineHeight: 18, textAlign: 'center' },
  pressed: { opacity: 0.68 },
  disabled: { opacity: 0.4 },
});
