/**
 * Desk Gateway 正式控制首页。
 *
 * 页面只负责呈现统一设备快照和转发用户动作；HOLD 续期、STOP 兜底及通道切换仍由
 * 控制层负责，避免视觉重构改变安全语义。
 */

import { useEffect, useRef, useState } from 'react';
import {
  Animated,
  Alert,
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  View,
} from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

import type { DeskClientSnapshot } from '../desk/DeskClient';
import type { DeskHeightPreset } from '../desk/DeskRestClient';
import { formatFirmwareBuildTime } from '../desk/formatFirmwareBuildTime';
import {
  DESK_DEFAULT_SIT_HEIGHT_MM,
  DESK_DEFAULT_STAND_HEIGHT_MM,
  DESK_MAX_HEIGHT_MM,
  describeDeskStatus,
} from '../desk/heightPresentation';
import { formatRemaining, reminderPhaseLabel } from '../desk/reminderPresentation';
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
  customPresets: DeskHeightPreset[];
  onConnect: () => void;
  onOpenSettings: () => void;
  onOpenPomodoro: () => void;
  onHoldUpStart: () => void;
  onHoldDownStart: () => void;
  onHoldEnd: () => void;
  onStop: () => void;
  onPreset1: () => void;
  onPreset4: () => void;
  onCustomPreset: (id: string) => void;
  onResetController: () => void;
  onToggleChildLock: () => void;
}

interface ErrorToastState {
  title: string;
  detail: string | null;
  retryable: boolean;
}

const ERROR_TOAST_VISIBLE_MS = 2_500;
const ERROR_TOAST_ANIMATION_MS = 180;

export function HomeScreen({
  snapshot,
  customPresets,
  onConnect,
  onOpenSettings,
  onOpenPomodoro,
  onHoldUpStart,
  onHoldDownStart,
  onHoldEnd,
  onStop,
  onPreset1,
  onPreset4,
  onCustomPreset,
  onResetController,
  onToggleChildLock,
}: HomeScreenProps) {
  const [errorToast, setErrorToast] = useState<ErrorToastState | null>(null);
  const errorToastProgress = useRef(new Animated.Value(0)).current;
  const errorToastTimer = useRef<ReturnType<typeof setTimeout> | null>(null);
  const resetPromptShown = useRef(false);
  const state = snapshot.deskState;
  const config = snapshot.deskConfig;
  const connected = snapshot.phase === 'ready';
  const connecting = isConnecting(snapshot.phase);
  const activeSourceAllowed = snapshot.transport === 'wifi'
    ? config?.restAllowed !== false
    : state?.bluetoothAllowed !== false;
  const motionBlocked =
    !connected || state?.childLock === true || !activeSourceAllowed ||
    state?.controllerResetActive === true;
  const upwardBlocked = state?.upwardBlocked === true;
  const heightUnknown = state?.heightKnown !== true || state.heightMm === null;
  const heightCm = state?.heightKnown && state.heightMm !== null
    ? (state.heightMm / 10).toFixed(1)
    : '—';
  const firmwareBuildTime = formatFirmwareBuildTime(snapshot.firmwareRevision);
  const maxHeightMm = state?.maxHeightMm ?? DESK_MAX_HEIGHT_MM;
  const maxHeightCm = (maxHeightMm / 10).toFixed(1);
  const preset1HeightMm = config?.preset1HeightMm ?? DESK_DEFAULT_SIT_HEIGHT_MM;
  const preset4HeightMm = config?.preset4HeightMm ??
    DESK_DEFAULT_STAND_HEIGHT_MM;
  const preset1HeightCm = (preset1HeightMm / 10).toFixed(0);
  const preset4HeightCm = (preset4HeightMm / 10).toFixed(0);
  const preset1MovesUp = state?.heightKnown === true && state.heightMm !== null &&
    state.heightMm < preset1HeightMm;
  const preset4MovesUp = state?.heightKnown === true && state.heightMm !== null &&
    state.heightMm < preset4HeightMm;
  const customPresetBlocked = !connected || state?.childLock === true ||
    config?.restAllowed === false || state?.controllerResetActive === true;
  const statusDescription = describeDeskStatus({
    connected,
    childLock: state?.childLock === true,
    activeSourceAllowed,
    controllerResetActive: state?.controllerResetActive === true,
    heightKnown: state?.heightKnown === true,
    heightMm: state?.heightMm ?? null,
    maxHeightMm,
    upwardBlocked,
    motion: state?.motion ?? null,
  });

  useEffect(() => {
    if (state?.controllerResetRecommended !== true) {
      resetPromptShown.current = false;
      return;
    }
    if (resetPromptShown.current) {
      return;
    }
    resetPromptShown.current = true;
    const actions = state.controllerResetSupported
      ? [
          { text: '稍后处理', style: 'cancel' as const },
          { text: '立即重置', onPress: onResetController },
        ]
      : [{ text: '知道了', style: 'cancel' as const }];
    Alert.alert(
      '桌子可能需要重置',
      '升降指令发出后高度没有正常变化，可能是控制盒 B12 错误。请确认桌子周围无障碍物后执行重置。',
      actions,
    );
  }, [onResetController, state?.controllerResetRecommended,
    state?.controllerResetSupported]);

  // Transport 可能在下一帧清空 error；Toast 使用独立状态，确保用户能看清提示。
  useEffect(() => {
    if (!snapshot.error) {
      return;
    }

    if (errorToastTimer.current !== null) {
      clearTimeout(errorToastTimer.current);
    }
    errorToastProgress.stopAnimation();
    errorToastProgress.setValue(0);

    const retryable = snapshot.phase !== 'ready';
    setErrorToast({
      title: retryable ? '连接失败，点击重试' : '操作失败',
      detail: retryable ? friendlyError(snapshot.error) : null,
      retryable,
    });

    Animated.timing(errorToastProgress, {
      toValue: 1,
      duration: ERROR_TOAST_ANIMATION_MS,
      useNativeDriver: true,
    }).start();

    errorToastTimer.current = setTimeout(() => {
      Animated.timing(errorToastProgress, {
        toValue: 0,
        duration: ERROR_TOAST_ANIMATION_MS,
        useNativeDriver: true,
      }).start(({ finished }) => {
        if (finished) {
          setErrorToast(null);
        }
      });
      errorToastTimer.current = null;
    }, ERROR_TOAST_VISIBLE_MS);
  }, [errorToastProgress, snapshot.error, snapshot.phase]);

  useEffect(() => () => {
    if (errorToastTimer.current !== null) {
      clearTimeout(errorToastTimer.current);
    }
    errorToastProgress.stopAnimation();
  }, [errorToastProgress]);

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

        <DeskScene
          heightMm={state?.heightKnown ? state.heightMm : null}
          safetyMaxHeightMm={maxHeightMm}
        />

        <View style={styles.heightBlock}>
          <Text style={styles.heightLabel}>当前高度</Text>
          <View style={styles.heightLine}>
            <Text style={styles.heightValue}>{heightCm}</Text>
            <Text style={styles.heightUnit}>cm</Text>
          </View>
          <View style={styles.limitPill}>
            <Text style={styles.limitText}>最高 {maxHeightCm} cm</Text>
          </View>
          <Text accessibilityLiveRegion="polite" style={styles.stateHint}>
            {statusDescription}
          </Text>
        </View>

        <View style={styles.controlsRow}>
          <HoldControl
            label="按住升高"
            direction="up"
            disabled={motionBlocked || upwardBlocked || heightUnknown}
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
            label="按住降低"
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
            disabled={motionBlocked || heightUnknown ||
              (upwardBlocked && preset1MovesUp)}
            onPress={onPreset1}
          />
          <PresetCard
            icon={<StandingIcon size={34} color={palette.gold} />}
            label="起立"
            height={preset4HeightCm}
            disabled={motionBlocked || heightUnknown ||
              (upwardBlocked && preset4MovesUp)}
            onPress={onPreset4}
          />
        </View>
        {customPresets.length > 0 ? (
          <View style={styles.customPresetGrid}>
            {customPresets.map((preset) => {
              const movesUp = state?.heightKnown === true &&
                state.heightMm !== null && state.heightMm < preset.height_mm;
              return (
                <View key={preset.id} style={styles.customPresetSlot}>
                  <PresetCard
                    icon={null}
                    label={preset.name}
                    height={(preset.height_mm / 10).toFixed(1)}
                    disabled={customPresetBlocked || heightUnknown ||
                      (upwardBlocked && movesUp)}
                    onPress={() => onCustomPreset(preset.id)}
                  />
                </View>
              );
            })}
          </View>
        ) : null}

        <Pressable
          accessibilityRole="button"
          accessibilityLabel="打开番茄时钟"
          onPress={onOpenPomodoro}
          style={({ pressed }) => [styles.pomodoroCard, pressed && styles.pressed]}
        >
          <View>
            <Text style={styles.pomodoroTitle}>番茄时钟</Text>
            <Text style={styles.pomodoroStatus}>
              {snapshot.reminder
                ? `${reminderPhaseLabel(snapshot.reminder)} · ${formatRemaining(snapshot.reminder.remainingSec)}`
                : '等待设备状态'}
            </Text>
          </View>
          <Text style={styles.pomodoroChevron}>›</Text>
        </Pressable>

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
          {snapshot.transport === 'wifi' ? 'Wi-Fi · REST' : 'BLE'} · 固件{' '}
          {firmwareBuildTime ?? '构建信息不可用'}
        </Text>
      </ScrollView>

      {errorToast ? (
        <Animated.View
          accessibilityLiveRegion="assertive"
          pointerEvents={errorToast.retryable ? 'auto' : 'none'}
          style={[
            styles.errorToastLayer,
            {
              opacity: errorToastProgress,
              transform: [{
                translateY: errorToastProgress.interpolate({
                  inputRange: [0, 1],
                  outputRange: [14, 0],
                }),
              }],
            },
          ]}
        >
          <Pressable
            accessibilityRole={errorToast.retryable ? 'button' : undefined}
            accessibilityLabel={errorToast.title}
            disabled={!errorToast.retryable}
            onPress={errorToast.retryable ? onConnect : undefined}
            style={({ pressed }) => [
              styles.errorToast,
              pressed && errorToast.retryable && styles.pressed,
            ]}
          >
            <Text style={styles.errorTitle}>{errorToast.title}</Text>
            {errorToast.detail ? (
              <Text style={styles.errorText}>{errorToast.detail}</Text>
            ) : null}
          </Pressable>
        </Animated.View>
      ) : null}
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
  label,
  direction,
  disabled,
  onPressIn,
  onPressOut,
}: {
  label: string;
  direction: 'up' | 'down';
  disabled: boolean;
  onPressIn: () => void;
  onPressOut: () => void;
}) {
  return (
    <Pressable
      accessibilityRole="button"
      accessibilityHint="持续按住移动，松手立即停止"
      disabled={disabled}
      onPressIn={onPressIn}
      onPressOut={onPressOut}
      style={({ pressed }) => [
        styles.holdButton,
        styles.holdOutline,
        disabled && styles.disabled,
        pressed && !disabled && styles.holdPressed,
      ]}
    >
      <ChevronIcon
        direction={direction}
        size={27}
        color={palette.ink}
        strokeWidth={2.2}
      />
      <Text style={styles.holdText}>{label}</Text>
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
  errorToastLayer: { position: 'absolute', left: 22, right: 22, bottom: 18, zIndex: 10 },
  errorToast: { paddingHorizontal: 16, paddingVertical: 13, borderWidth: 1, borderColor: '#E6B7AF', borderRadius: radii.small, backgroundColor: '#FFF1EE', ...shadows.floating },
  errorTitle: { color: '#9D382D', fontSize: 14, fontWeight: '700' },
  errorText: { marginTop: 3, color: '#9D382D', fontSize: 12, lineHeight: 17 },
  heightBlock: { alignItems: 'center', marginTop: -4 },
  heightLabel: { color: palette.inkMuted, fontSize: 18, fontWeight: '500' },
  heightLine: { flexDirection: 'row', alignItems: 'baseline', marginTop: 2 },
  heightValue: { color: palette.ink, fontSize: 72, fontWeight: '300', letterSpacing: -3.5 },
  heightUnit: { marginLeft: 8, color: palette.ink, fontSize: 21, fontWeight: '500' },
  limitPill: { marginTop: 3, paddingHorizontal: 17, paddingVertical: 6, borderWidth: 1, borderColor: palette.gold, borderRadius: radii.pill },
  limitText: { color: palette.gold, fontSize: 16 },
  stateHint: { minHeight: 18, marginTop: 9, color: palette.inkMuted, fontSize: 13, lineHeight: 18, textAlign: 'center' },
  controlsRow: { marginTop: 12, flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', gap: 11 },
  holdButton: { flex: 1, height: 76, alignItems: 'center', justifyContent: 'center', gap: 3, borderRadius: 22 },
  holdOutline: { borderWidth: 1.5, borderColor: palette.ink, backgroundColor: palette.surface },
  holdPressed: { transform: [{ scale: 0.98 }], backgroundColor: palette.gold },
  holdText: { color: palette.ink, fontSize: 16, fontWeight: '600' },
  stopButton: { width: 60, height: 60, alignItems: 'center', justifyContent: 'center', borderWidth: 1, borderColor: palette.danger, borderRadius: 30, backgroundColor: palette.danger, ...shadows.floating },
  presetRow: { marginTop: 12, flexDirection: 'row', gap: 11 },
  customPresetGrid: { marginTop: 10, flexDirection: 'row', flexWrap: 'wrap', gap: 10 },
  customPresetSlot: { width: '48%', minHeight: 70 },
  presetCard: { flex: 1, minHeight: 70, paddingHorizontal: 12, flexDirection: 'row', alignItems: 'center', justifyContent: 'center', gap: 8, borderWidth: 1, borderColor: palette.line, borderRadius: 16, backgroundColor: palette.surface },
  presetLabel: { color: palette.ink, fontSize: 15, fontWeight: '600' },
  presetHeightLine: { flexDirection: 'row', alignItems: 'baseline', marginTop: 1 },
  presetHeight: { color: palette.ink, fontSize: 24, fontWeight: '500' },
  presetUnit: { marginLeft: 4, color: palette.ink, fontSize: 13 },
  lockCard: { minHeight: 56, marginTop: 10, paddingHorizontal: 16, flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', borderWidth: 1, borderColor: palette.line, borderRadius: 16, backgroundColor: palette.surface },
  pomodoroCard: { minHeight: 66, marginTop: 10, paddingHorizontal: 16, flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', borderWidth: 1, borderColor: palette.goldSoft, borderRadius: 16, backgroundColor: palette.surface },
  pomodoroTitle: { color: palette.ink, fontSize: 17, fontWeight: '700' },
  pomodoroStatus: { marginTop: 3, color: palette.inkMuted, fontSize: 13 },
  pomodoroChevron: { color: palette.gold, fontSize: 30, fontWeight: '300' },
  lockLabel: { flexDirection: 'row', alignItems: 'center', gap: 12 },
  lockText: { color: palette.ink, fontSize: 17, fontWeight: '500' },
  footer: { marginTop: 14, color: palette.inkMuted, fontSize: 12, textAlign: 'center' },
  pressed: { opacity: 0.7 },
  disabled: { opacity: 0.38 },
});
