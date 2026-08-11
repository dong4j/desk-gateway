/**
 * Desk Gateway 正式控制首页。
 *
 * 页面只负责呈现 BLE 快照和转发用户动作；HOLD 续期、STOP 兜底及写入串行化仍由
 * DeskHoldController 和 DeskBleClient 负责，避免视觉重构改变安全语义。
 */

import { Pressable, ScrollView, StyleSheet, Text, View } from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

import type { DeskClientSnapshot } from '../desk/DeskBleClient';
import { formatFirmwareBuildTime } from '../desk/formatFirmwareBuildTime';
import { DeskScene } from '../ui/DeskScene';
import {
  ChairIcon,
  ChevronIcon,
  GearIcon,
  LockIcon,
  StandingIcon,
  StopIcon,
} from '../ui/Icons';
import { PrototypeSwitch } from '../ui/PrototypeSwitch';
import { palette, radii, shadows } from '../ui/theme';

interface HomeScreenProps {
  snapshot: DeskClientSnapshot;
  onConnect: () => void;
  onOpenSettings: () => void;
  onHoldUpStart: () => void;
  onHoldDownStart: () => void;
  onHoldEnd: () => void;
  onStop: () => void;
  onPreset1: () => void;
  onPreset4: () => void;
  onToggleChildLock: () => void;
}

export function HomeScreen({
  snapshot,
  onConnect,
  onOpenSettings,
  onHoldUpStart,
  onHoldDownStart,
  onHoldEnd,
  onStop,
  onPreset1,
  onPreset4,
  onToggleChildLock,
}: HomeScreenProps) {
  const state = snapshot.deskState;
  const config = snapshot.deskConfig;
  const connected = snapshot.phase === 'ready';
  const connecting = isConnecting(snapshot.phase);
  const motionBlocked =
    !connected || state?.childLock === true || state?.bluetoothAllowed === false;
  const heightCm = state?.heightKnown && state.heightMm !== null
    ? (state.heightMm / 10).toFixed(1)
    : '—';
  const maxHeightCm = state?.maxHeightMm
    ? (state.maxHeightMm / 10).toFixed(0)
    : '—';
  const firmwareBuildTime = formatFirmwareBuildTime(snapshot.firmwareRevision);
  const preset1HeightCm = ((config?.preset1HeightMm ?? 640) / 10).toFixed(0);
  const preset4HeightCm = ((config?.preset4HeightMm ?? 1020) / 10).toFixed(0);

  return (
    <SafeAreaView style={styles.safeArea} edges={['top', 'bottom']}>
      <ScrollView
        showsVerticalScrollIndicator={false}
        contentContainerStyle={styles.content}
      >
        <View style={styles.header}>
          <Text style={styles.title}>Desk Gateway</Text>
          <View style={styles.headerActions}>
            <ConnectionBadge
              connected={connected}
              connecting={connecting}
              onPress={connected || connecting ? undefined : onConnect}
            />
            <Pressable
              accessibilityRole="button"
              accessibilityLabel="打开设置"
              onPress={onOpenSettings}
              hitSlop={10}
              style={({ pressed }) => [styles.iconButton, pressed && styles.pressed]}
            >
              <GearIcon size={30} />
            </Pressable>
          </View>
        </View>

        {snapshot.error ? (
          <Pressable onPress={onConnect} style={styles.errorBanner}>
            <Text style={styles.errorTitle}>连接失败，点击重试</Text>
            <Text style={styles.errorText}>{friendlyError(snapshot.error)}</Text>
          </Pressable>
        ) : null}

        <DeskScene
          heightMm={state?.heightKnown ? state.heightMm : null}
          maxHeightMm={1290}
        />

        <View style={styles.heightBlock}>
          <Text style={styles.heightLabel}>当前高度</Text>
          <View style={styles.heightLine}>
            <Text style={styles.heightValue}>{heightCm}</Text>
            <Text style={styles.heightUnit}>cm</Text>
          </View>
          <View style={styles.limitPill}>
            <Text style={styles.limitText}>上限 {maxHeightCm} cm</Text>
          </View>
        </View>

        <View style={styles.controlsRow}>
          <HoldControl
            variant="primary"
            label="按住升"
            direction="up"
            disabled={motionBlocked}
            onPressIn={onHoldUpStart}
            onPressOut={onHoldEnd}
          />
          <Pressable
            accessibilityRole="button"
            accessibilityLabel="停止"
            disabled={!connected}
            onPress={onStop}
            style={({ pressed }) => [
              styles.stopButton,
              !connected && styles.disabled,
              pressed && connected && styles.pressed,
            ]}
          >
            <StopIcon size={29} color={palette.white} />
          </Pressable>
          <HoldControl
            variant="outline"
            label="按住降"
            direction="down"
            disabled={motionBlocked}
            onPressIn={onHoldDownStart}
            onPressOut={onHoldEnd}
          />
        </View>

        <View style={styles.presetRow}>
          <PresetCard
            icon={<ChairIcon size={34} color={palette.gold} />}
            label="请坐"
            height={preset1HeightCm}
            disabled={motionBlocked}
            onPress={onPreset1}
          />
          <PresetCard
            icon={<StandingIcon size={34} color={palette.gold} />}
            label="起立"
            height={preset4HeightCm}
            disabled={motionBlocked}
            onPress={onPreset4}
          />
        </View>

        <Pressable
          accessibilityRole="switch"
          accessibilityLabel="童锁"
          accessibilityState={{
            checked: config?.childLock ?? state?.childLock ?? false,
            disabled: !connected || !config,
          }}
          disabled={!connected || !config}
          onPress={onToggleChildLock}
          style={({ pressed }) => [
            styles.lockCard,
            (!connected || !config) && styles.disabled,
            pressed && connected && config && styles.pressed,
          ]}
        >
          <View style={styles.lockLabel}>
            <LockIcon size={24} color={palette.gold} />
            <Text style={styles.lockText}>童锁</Text>
          </View>
          <PrototypeSwitch
            value={config?.childLock ?? state?.childLock ?? false}
            muted={!connected || !config}
          />
        </Pressable>

        <Text style={styles.footer} numberOfLines={1}>
          BLE · 固件 {firmwareBuildTime ?? '构建信息不可用'}
        </Text>
      </ScrollView>
    </SafeAreaView>
  );
}

function ConnectionBadge({
  connected,
  connecting,
  onPress,
}: {
  connected: boolean;
  connecting: boolean;
  onPress?: () => void;
}) {
  const label = connected ? '已连接' : connecting ? '连接中' : '点击连接';
  return (
    <Pressable
      accessibilityRole="button"
      accessibilityLabel={label}
      onPress={onPress}
      style={({ pressed }) => [styles.connectionBadge, pressed && onPress && styles.pressed]}
    >
      <View style={[styles.connectionDot, !connected && styles.connectionDotOff]} />
      <Text style={[styles.connectionText, !connected && styles.connectionTextOff]}>{label}</Text>
    </Pressable>
  );
}

function HoldControl({
  variant,
  label,
  direction,
  disabled,
  onPressIn,
  onPressOut,
}: {
  variant: 'primary' | 'outline';
  label: string;
  direction: 'up' | 'down';
  disabled: boolean;
  onPressIn: () => void;
  onPressOut: () => void;
}) {
  const primary = variant === 'primary';
  return (
    <Pressable
      accessibilityRole="button"
      accessibilityHint="持续按住移动，松手立即停止"
      disabled={disabled}
      onPressIn={onPressIn}
      onPressOut={onPressOut}
      style={({ pressed }) => [
        styles.holdButton,
        primary ? styles.holdPrimary : styles.holdOutline,
        disabled && styles.disabled,
        pressed && !disabled && styles.holdPressed,
      ]}
    >
      <ChevronIcon
        direction={direction}
        size={27}
        color={primary ? palette.white : palette.ink}
        strokeWidth={2.2}
      />
      <Text style={[styles.holdText, primary && styles.holdTextPrimary]}>{label}</Text>
    </Pressable>
  );
}

function PresetCard({
  icon,
  label,
  height,
  disabled,
  onPress,
}: {
  icon: React.ReactNode;
  label: string;
  height: string;
  disabled: boolean;
  onPress: () => void;
}) {
  return (
    <Pressable
      accessibilityRole="button"
      disabled={disabled}
      onPress={onPress}
      style={({ pressed }) => [
        styles.presetCard,
        disabled && styles.disabled,
        pressed && !disabled && styles.pressed,
      ]}
    >
      {icon}
      <View>
        <Text style={styles.presetLabel}>{label}</Text>
        <View style={styles.presetHeightLine}>
          <Text style={styles.presetHeight}>{height}</Text>
          <Text style={styles.presetUnit}>cm</Text>
        </View>
      </View>
    </Pressable>
  );
}

function isConnecting(phase: DeskClientSnapshot['phase']): boolean {
  return phase === 'scanning' || phase === 'connecting' || phase === 'pairing';
}

function friendlyError(error: string): string {
  const lower = error.toLowerCase();
  if (lower.includes('already') || lower.includes('connect')) {
    return '请确认 LightBlue 已断开该设备，然后重试。';
  }
  return error;
}

const styles = StyleSheet.create({
  safeArea: { flex: 1, backgroundColor: palette.background },
  content: { flexGrow: 1, paddingHorizontal: 22, paddingTop: 10, paddingBottom: 18 },
  header: { minHeight: 58, flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between' },
  title: { color: palette.ink, fontSize: 31, fontWeight: '700', letterSpacing: -1.2 },
  headerActions: { flexDirection: 'row', alignItems: 'center', gap: 12 },
  iconButton: { width: 38, height: 38, alignItems: 'center', justifyContent: 'center' },
  connectionBadge: { height: 38, paddingHorizontal: 15, flexDirection: 'row', alignItems: 'center', gap: 9, borderRadius: radii.pill, backgroundColor: palette.greenSurface },
  connectionDot: { width: 10, height: 10, borderRadius: 5, backgroundColor: palette.green },
  connectionDotOff: { backgroundColor: palette.inkFaint },
  connectionText: { color: palette.greenInk, fontSize: 16, fontWeight: '500' },
  connectionTextOff: { color: palette.inkMuted },
  errorBanner: { marginTop: 10, padding: 13, borderWidth: 1, borderColor: '#E6B7AF', borderRadius: radii.small, backgroundColor: '#FFF1EE' },
  errorTitle: { color: '#9D382D', fontSize: 14, fontWeight: '700' },
  errorText: { marginTop: 3, color: '#9D382D', fontSize: 12, lineHeight: 17 },
  heightBlock: { alignItems: 'center', marginTop: -4 },
  heightLabel: { color: palette.inkMuted, fontSize: 18, fontWeight: '500' },
  heightLine: { flexDirection: 'row', alignItems: 'baseline', marginTop: 2 },
  heightValue: { color: palette.ink, fontSize: 72, fontWeight: '300', letterSpacing: -3.5 },
  heightUnit: { marginLeft: 8, color: palette.ink, fontSize: 21, fontWeight: '500' },
  limitPill: { marginTop: 3, paddingHorizontal: 17, paddingVertical: 6, borderWidth: 1, borderColor: palette.gold, borderRadius: radii.pill },
  limitText: { color: palette.gold, fontSize: 16 },
  controlsRow: { marginTop: 18, flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', gap: 11 },
  holdButton: { flex: 1, height: 76, alignItems: 'center', justifyContent: 'center', gap: 3, borderRadius: 22 },
  holdPrimary: { backgroundColor: palette.ink, ...shadows.floating },
  holdOutline: { borderWidth: 1.5, borderColor: palette.ink, backgroundColor: palette.surface },
  holdPressed: { transform: [{ scale: 0.98 }], backgroundColor: palette.gold },
  holdText: { color: palette.ink, fontSize: 16, fontWeight: '600' },
  holdTextPrimary: { color: palette.white },
  stopButton: { width: 60, height: 60, alignItems: 'center', justifyContent: 'center', borderWidth: 1, borderColor: palette.danger, borderRadius: 30, backgroundColor: palette.danger, ...shadows.floating },
  presetRow: { marginTop: 12, flexDirection: 'row', gap: 11 },
  presetCard: { flex: 1, minHeight: 70, paddingHorizontal: 12, flexDirection: 'row', alignItems: 'center', justifyContent: 'center', gap: 8, borderWidth: 1, borderColor: palette.line, borderRadius: 16, backgroundColor: palette.surface },
  presetLabel: { color: palette.ink, fontSize: 15, fontWeight: '600' },
  presetHeightLine: { flexDirection: 'row', alignItems: 'baseline', marginTop: 1 },
  presetHeight: { color: palette.ink, fontSize: 24, fontWeight: '500' },
  presetUnit: { marginLeft: 4, color: palette.ink, fontSize: 13 },
  lockCard: { minHeight: 56, marginTop: 10, paddingHorizontal: 16, flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', borderWidth: 1, borderColor: palette.line, borderRadius: 16, backgroundColor: palette.surface },
  lockLabel: { flexDirection: 'row', alignItems: 'center', gap: 12 },
  lockText: { color: palette.ink, fontSize: 17, fontWeight: '500' },
  footer: { marginTop: 14, color: palette.inkMuted, fontSize: 12, textAlign: 'center' },
  pressed: { opacity: 0.7 },
  disabled: { opacity: 0.38 },
});
