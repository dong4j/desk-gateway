/**
 * Desk Gateway 设置页。
 *
 * 页面按确认原型呈现完整的信息架构。设备设置只展示 ESP32 Config 回读值，
 * 写入过程中不做本地乐观切换，避免界面显示与实际安全策略不一致。
 */

import { useEffect, useState, type ReactNode } from 'react';
import {
  type GestureResponderEvent,
  type LayoutChangeEvent,
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  TextInput,
  View,
} from 'react-native';
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
  const firmwareBuildTime = formatFirmwareBuildTime(snapshot.firmwareRevision);

  useEffect(() => {
    setMaxHeightDraft(String(maxHeightCm));
    setMaxHeightError(null);
  }, [maxHeightCm]);

  const saveMaxHeight = () => {
    const centimetres = Number(maxHeightDraft);
    if (!Number.isFinite(centimetres) || centimetres < 64 || centimetres > 129) {
      setMaxHeightError('请输入 64–129 cm');
      return;
    }
    setMaxHeightError(null);
    onSetMaxHeightMm(Math.round(centimetres * 10));
  };

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
            <HeightTrack value={maxHeightCm} />
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
          <HapticStrengthSlider
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

/**
 * 不引入原生 Slider 依赖的轻量强度滑块。松手后才写入偏好，避免拖动时连续写存储。
 */
function HapticStrengthSlider({
  value,
  disabled,
  onChange,
}: {
  value: number;
  disabled: boolean;
  onChange: (value: number) => void;
}) {
  const [trackWidth, setTrackWidth] = useState(1);
  const [draft, setDraft] = useState(value);

  useEffect(() => setDraft(value), [value]);

  const valueFromEvent = (event: GestureResponderEvent): number =>
    Math.max(
      0,
      Math.min(100, Math.round((event.nativeEvent.locationX / trackWidth) * 100)),
    );
  const updateDraft = (event: GestureResponderEvent) => {
    setDraft(valueFromEvent(event));
  };
  const commitDraft = (event: GestureResponderEvent) => {
    const next = valueFromEvent(event);
    setDraft(next);
    onChange(next);
  };
  const adjust = (delta: number) => {
    const next = Math.max(0, Math.min(100, draft + delta));
    setDraft(next);
    onChange(next);
  };
  const onTrackLayout = (event: LayoutChangeEvent) => {
    setTrackWidth(Math.max(1, event.nativeEvent.layout.width));
  };

  return (
    <View style={[styles.hapticStrength, disabled && styles.disabled]}>
      <View style={styles.rowBetween}>
        <Text style={styles.hapticStrengthLabel}>震动强度</Text>
        <Text style={styles.hapticStrengthValue}>{draft}%</Text>
      </View>
      <View
        accessible
        accessibilityRole="adjustable"
        accessibilityLabel="震动强度"
        accessibilityState={{ disabled }}
        accessibilityValue={{ min: 0, max: 100, now: draft, text: `${draft}%` }}
        accessibilityActions={[
          { name: 'increment', label: '增加震动强度' },
          { name: 'decrement', label: '降低震动强度' },
        ]}
        onAccessibilityAction={(event) =>
          adjust(event.nativeEvent.actionName === 'increment' ? 10 : -10)
        }
        onLayout={onTrackLayout}
        onStartShouldSetResponder={() => !disabled}
        onMoveShouldSetResponder={() => !disabled}
        onResponderGrant={updateDraft}
        onResponderMove={updateDraft}
        onResponderRelease={commitDraft}
        style={styles.hapticSliderTrackTouch}
      >
        <View style={styles.hapticSliderTrack}>
          <View style={[styles.hapticSliderFill, { width: `${draft}%` }]} />
          <View style={[styles.hapticSliderThumb, { left: `${draft}%` }]} />
        </View>
      </View>
      <View style={styles.hapticStrengthEdges}>
        <Text style={styles.hapticStrengthEdge}>轻</Text>
        <Text style={styles.hapticStrengthEdge}>强</Text>
      </View>
    </View>
  );
}

function SectionTitle({ children }: { children: ReactNode }) {
  return <Text style={styles.sectionTitle}>{children}</Text>;
}

function HeightTrack({ value }: { value: number }) {
  const percent = Math.max(0, Math.min(100, ((value - 64) / (129 - 64)) * 100));
  return (
    <View style={styles.sliderRow}>
      <Text style={styles.sliderEdge}>64</Text>
      <View style={styles.sliderTrack}>
        <View style={[styles.sliderFill, { width: `${percent}%` }]} />
        <View style={[styles.sliderThumb, { left: `${percent}%` }]} />
      </View>
      <Text style={styles.sliderEdge}>129</Text>
    </View>
  );
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
  sliderTrack: { flex: 1, height: 5, borderRadius: radii.pill, backgroundColor: palette.surfaceMuted },
  sliderFill: { height: 5, borderRadius: radii.pill, backgroundColor: palette.goldSoft },
  sliderThumb: { position: 'absolute', top: -7, width: 19, height: 19, marginLeft: -9, borderRadius: 10, backgroundColor: palette.gold },
  heightEditor: { marginTop: 16, flexDirection: 'row', alignItems: 'center', gap: 8 },
  heightInput: { width: 82, height: 40, paddingHorizontal: 12, borderWidth: 1, borderColor: palette.line, borderRadius: radii.small, backgroundColor: palette.white, color: palette.ink, fontSize: 16 },
  heightUnit: { color: palette.inkMuted, fontSize: 15 },
  heightSave: { minWidth: 66, height: 40, marginLeft: 'auto', alignItems: 'center', justifyContent: 'center', borderRadius: radii.pill, backgroundColor: palette.ink },
  heightSaveText: { color: palette.white, fontSize: 15, fontWeight: '600' },
  heightError: { marginTop: 7, color: palette.danger, fontSize: 12 },
  divider: { height: 1, backgroundColor: palette.line },
  securityRow: { minHeight: 78, paddingHorizontal: 18, flexDirection: 'row', alignItems: 'center', gap: 15 },
  settingCopy: { flex: 1 },
  settingDescription: { marginTop: 3, color: palette.inkMuted, fontSize: 13 },
  settingRow: { minHeight: 64, paddingHorizontal: 17, flexDirection: 'row', alignItems: 'center' },
  settingIcon: { width: 42, alignItems: 'flex-start' },
  settingRowTitle: { color: palette.ink, fontSize: 17, fontWeight: '500' },
  settingSpacer: { flex: 1 },
  hapticStrength: { paddingHorizontal: 18, paddingTop: 15, paddingBottom: 14 },
  hapticStrengthLabel: { color: palette.ink, fontSize: 15, fontWeight: '500' },
  hapticStrengthValue: { color: palette.gold, fontSize: 15, fontWeight: '600' },
  hapticSliderTrackTouch: { height: 34, marginTop: 4, justifyContent: 'center' },
  hapticSliderTrack: { height: 5, borderRadius: radii.pill, backgroundColor: palette.surfaceMuted },
  hapticSliderFill: { height: 5, borderRadius: radii.pill, backgroundColor: palette.goldSoft },
  hapticSliderThumb: { position: 'absolute', top: -7, width: 19, height: 19, marginLeft: -9, borderRadius: 10, borderWidth: 2, borderColor: palette.surface, backgroundColor: palette.gold },
  hapticStrengthEdges: { marginTop: -3, flexDirection: 'row', justifyContent: 'space-between' },
  hapticStrengthEdge: { color: palette.inkFaint, fontSize: 12 },
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
