/**
 * ESP 番茄时钟的移动端控制页。
 *
 * 页面只显示设备快照并发送动作/配置；前后台切换和重连后都以 ESP 剩余秒数为准，
 * 不在手机上启动第二个倒计时。
 */

import Slider from '@react-native-community/slider';
import { useEffect, useState } from 'react';
import {
  Alert,
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  View,
} from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

import type { DeskClientSnapshot } from '../desk/DeskClient';
import {
  formatRemaining,
  reminderAutoAction,
  reminderDisplayedSeconds,
  reminderPhaseLabel,
  reminderPrimaryAction,
  reminderStatusHint,
} from '../desk/reminderPresentation';
import type { ReminderAction, ReminderConfig } from '../desk/types';
import type {
  ReminderConfigPatch,
  ReminderPromptId,
} from '../desk/DeskRestClient';
import { PrototypeSwitch } from '../ui/PrototypeSwitch';
import { palette, radii } from '../ui/theme';

interface PomodoroScreenProps {
  snapshot: DeskClientSnapshot;
  onBack: () => void;
  onAction: (action: ReminderAction) => Promise<void>;
  onUpdateConfig: (patch: ReminderConfigPatch) => Promise<void>;
  onPreviewAudio: (promptId: ReminderPromptId) => Promise<void>;
  onStopAudio: () => Promise<void>;
}

export function PomodoroScreen({
  snapshot,
  onBack,
  onAction,
  onUpdateConfig,
  onPreviewAudio,
  onStopAudio,
}: PomodoroScreenProps) {
  const reminder = snapshot.reminder;
  const audio = snapshot.audio;
  const [busy, setBusy] = useState(false);
  const [volume, setVolume] = useState(audio?.volumePercent ?? 60);

  useEffect(() => {
    if (audio) setVolume(audio.volumePercent);
  }, [audio?.volumePercent]);

  const perform = async (operation: () => Promise<void>) => {
    if (busy) return;
    setBusy(true);
    try {
      await operation();
    } catch (error) {
      Alert.alert(
        '番茄时钟操作失败',
        error instanceof Error ? error.message : String(error),
      );
    } finally {
      setBusy(false);
    }
  };

  if (!reminder) {
    return (
      <SafeAreaView style={styles.safeArea} edges={['top', 'bottom']}>
        <Header onBack={onBack} />
        <View style={styles.emptyState}>
          <Text style={styles.emptyTitle}>设备暂未提供番茄时钟</Text>
          <Text style={styles.emptyText}>
            请确认 Desk Gateway 已连接，并已刷入支持语音提醒的固件。
          </Text>
        </View>
      </SafeAreaView>
    );
  }

  const primary = reminderPrimaryAction(reminder);
  const auto = reminderAutoAction(reminder);
  const statusHint = reminderStatusHint(reminder);
  const config = reminder.config;
  const connected = snapshot.phase === 'ready' && reminder.available;
  const secondary: { action: ReminderAction; label: string } | null =
    reminder.state === 'waiting'
      ? { action: 'snooze', label: '稍后提醒' }
      : reminder.state === 'running' || reminder.state === 'paused'
        ? { action: 'skip', label: '跳过本轮' }
        : null;

  return (
    <SafeAreaView style={styles.safeArea} edges={['top', 'bottom']}>
      <Header onBack={onBack} />
      <ScrollView contentContainerStyle={styles.content}>
        <View style={styles.timerCard}>
          <Text style={styles.phase}>{reminderPhaseLabel(reminder)}</Text>
          <Text style={styles.timer}>
            {formatRemaining(reminderDisplayedSeconds(reminder))}
          </Text>
          <Text style={styles.progressText}>
            {statusHint || `已完成 ${reminder.completedFocusCount} 个专注时段`}
          </Text>
          <Pressable
            accessibilityRole="button"
            disabled={!connected || busy}
            onPress={() => void perform(() => onAction(primary.action))}
            style={({ pressed }) => [
              styles.primaryButton,
              (!connected || busy) && styles.disabled,
              pressed && styles.pressed,
            ]}
          >
            <Text style={styles.primaryButtonText}>{primary.label}</Text>
          </Pressable>
          {auto ? (
            <Pressable
              accessibilityRole="button"
              disabled={!connected || busy}
              onPress={() => void perform(() => onAction(auto.action))}
              style={({ pressed }) => [
                styles.primaryButton,
                styles.autoButton,
                (!connected || busy) && styles.disabled,
                pressed && styles.pressed,
              ]}
            >
              <Text style={styles.autoButtonText}>{auto.label}</Text>
            </Pressable>
          ) : null}
          <View style={styles.secondaryRow}>
            {secondary ? (
              <ActionButton
                label={secondary.label}
                disabled={!connected || busy}
                onPress={() => void perform(() => onAction(secondary.action))}
              />
            ) : null}
            {reminder.state !== 'idle' && primary.action !== 'stop' ? (
              <ActionButton
                label="结束"
                disabled={!connected || busy}
                onPress={() => void perform(() => onAction('stop'))}
              />
            ) : null}
          </View>
        </View>

        <View style={styles.sectionCard}>
          <Text style={styles.sectionTitle}>时长设置</Text>
          <StepperRow
            label="专注"
            value={config.focusMinutes}
            minimum={1}
            maximum={180}
            onChange={(focusMinutes) =>
              void perform(() => onUpdateConfig({ focusMinutes }))}
          />
          <StepperRow
            label="短休息"
            value={config.shortBreakMinutes}
            minimum={1}
            maximum={60}
            onChange={(shortBreakMinutes) =>
              void perform(() => onUpdateConfig({ shortBreakMinutes }))}
          />
          <StepperRow
            label="长休息"
            value={config.longBreakMinutes}
            minimum={1}
            maximum={120}
            onChange={(longBreakMinutes) =>
              void perform(() => onUpdateConfig({ longBreakMinutes }))}
          />
          <StepperRow
            label="每轮专注次数"
            value={config.focusesPerLongBreak}
            minimum={1}
            maximum={12}
            suffix="次"
            onChange={(focusesPerLongBreak) =>
              void perform(() => onUpdateConfig({ focusesPerLongBreak }))}
          />
          <Text style={styles.restHint}>时长设置通过局域网保存到 ESP。</Text>
        </View>

        <View style={styles.sectionCard}>
          <View style={styles.switchRow}>
            <View>
              <Text style={styles.sectionTitle}>语音提醒</Text>
              <Text style={styles.voicePack}>{audio?.voicePack || '语音包未就绪'}</Text>
            </View>
            <Pressable
              accessibilityRole="switch"
              accessibilityState={{ checked: audio?.enabled === true }}
              disabled={!audio?.available || busy}
              onPress={() => void perform(() => onUpdateConfig({
                audioEnabled: !(audio?.enabled === true),
              }))}
            >
              <PrototypeSwitch
                value={audio?.enabled === true}
                muted={!audio?.available || busy}
              />
            </Pressable>
          </View>
          <View style={styles.volumeHeader}>
            <Text style={styles.rowLabel}>音量</Text>
            <Text style={styles.rowValue}>{Math.round(volume)}%</Text>
          </View>
          <Slider
            minimumValue={0}
            maximumValue={100}
            step={1}
            value={volume}
            disabled={!audio?.available || busy}
            minimumTrackTintColor={palette.gold}
            maximumTrackTintColor={palette.line}
            thumbTintColor={palette.gold}
            onValueChange={setVolume}
            onSlidingComplete={(volumePercent) =>
              void perform(() => onUpdateConfig({ volumePercent }))}
          />
          <View style={styles.previewRow}>
            <ActionButton
              label={audio?.playing ? '停止试听' : '试听语音'}
              disabled={!audio?.available || busy}
              onPress={() => void perform(() =>
                audio?.playing ? onStopAudio() : onPreviewAudio('focus_done'),
              )}
            />
          </View>
        </View>
      </ScrollView>
    </SafeAreaView>
  );
}

function Header({ onBack }: { onBack: () => void }) {
  return (
    <View style={styles.header}>
      <Pressable accessibilityRole="button" onPress={onBack} hitSlop={10}>
        <Text style={styles.back}>‹ 返回</Text>
      </Pressable>
      <Text style={styles.title}>番茄时钟</Text>
      <View style={styles.headerSpacer} />
    </View>
  );
}

function ActionButton({
  label,
  disabled,
  onPress,
}: {
  label: string;
  disabled: boolean;
  onPress: () => void;
}) {
  return (
    <Pressable
      accessibilityRole="button"
      disabled={disabled}
      onPress={onPress}
      style={({ pressed }) => [
        styles.secondaryButton,
        disabled && styles.disabled,
        pressed && styles.pressed,
      ]}
    >
      <Text style={styles.secondaryButtonText}>{label}</Text>
    </Pressable>
  );
}

function StepperRow({
  label,
  value,
  minimum,
  maximum,
  suffix = '分钟',
  onChange,
}: {
  label: string;
  value: number;
  minimum: number;
  maximum: number;
  suffix?: string;
  onChange: (value: number) => void;
}) {
  return (
    <View style={styles.stepperRow}>
      <Text style={styles.rowLabel}>{label}</Text>
      <View style={styles.stepperControls}>
        <Pressable
          accessibilityRole="button"
          disabled={value <= minimum}
          onPress={() => onChange(value - 1)}
          style={styles.stepperButton}
        >
          <Text style={styles.stepperButtonText}>−</Text>
        </Pressable>
        <Text style={styles.rowValue}>{value} {suffix}</Text>
        <Pressable
          accessibilityRole="button"
          disabled={value >= maximum}
          onPress={() => onChange(value + 1)}
          style={styles.stepperButton}
        >
          <Text style={styles.stepperButtonText}>＋</Text>
        </Pressable>
      </View>
    </View>
  );
}

const styles = StyleSheet.create({
  safeArea: { flex: 1, backgroundColor: palette.background },
  header: { height: 58, paddingHorizontal: 22, flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between' },
  headerSpacer: { width: 58 },
  back: { width: 58, color: palette.gold, fontSize: 17, fontWeight: '600' },
  title: { color: palette.ink, fontSize: 22, fontWeight: '700' },
  content: { paddingHorizontal: 22, paddingBottom: 28, gap: 14 },
  timerCard: { alignItems: 'center', padding: 22, borderWidth: 1, borderColor: palette.goldSoft, borderRadius: radii.large, backgroundColor: palette.surface },
  phase: { color: palette.gold, fontSize: 17, fontWeight: '700' },
  timer: { marginTop: 4, color: palette.ink, fontSize: 64, fontWeight: '300', letterSpacing: -2.5, fontVariant: ['tabular-nums'] },
  progressText: { color: palette.inkMuted, fontSize: 14 },
  primaryButton: { width: '100%', minHeight: 52, marginTop: 20, alignItems: 'center', justifyContent: 'center', borderRadius: radii.medium, backgroundColor: palette.ink },
  primaryButtonText: { color: palette.white, fontSize: 17, fontWeight: '700' },
  autoButton: { marginTop: 10, backgroundColor: palette.surfaceMuted, borderWidth: 1, borderColor: palette.line },
  autoButtonText: { color: palette.ink, fontSize: 17, fontWeight: '700' },
  secondaryRow: { width: '100%', marginTop: 10, flexDirection: 'row', gap: 10 },
  secondaryButton: { flex: 1, minHeight: 44, alignItems: 'center', justifyContent: 'center', borderWidth: 1, borderColor: palette.line, borderRadius: radii.small, backgroundColor: palette.surfaceMuted },
  secondaryButtonText: { color: palette.ink, fontSize: 15, fontWeight: '600' },
  sectionCard: { padding: 17, borderWidth: 1, borderColor: palette.line, borderRadius: radii.medium, backgroundColor: palette.surface },
  sectionTitle: { color: palette.ink, fontSize: 18, fontWeight: '700' },
  stepperRow: { minHeight: 52, flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between', borderBottomWidth: StyleSheet.hairlineWidth, borderBottomColor: palette.line },
  stepperControls: { flexDirection: 'row', alignItems: 'center', gap: 8 },
  stepperButton: { width: 32, height: 32, alignItems: 'center', justifyContent: 'center', borderRadius: 16, backgroundColor: palette.surfaceMuted },
  stepperButtonText: { color: palette.ink, fontSize: 20, lineHeight: 23 },
  rowLabel: { color: palette.ink, fontSize: 15, fontWeight: '500' },
  rowValue: { minWidth: 64, color: palette.ink, fontSize: 15, fontWeight: '600', textAlign: 'center', fontVariant: ['tabular-nums'] },
  restHint: { marginTop: 10, color: palette.inkMuted, fontSize: 12 },
  switchRow: { flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between' },
  voicePack: { marginTop: 3, color: palette.inkMuted, fontSize: 12 },
  volumeHeader: { marginTop: 18, flexDirection: 'row', justifyContent: 'space-between' },
  previewRow: { marginTop: 10, flexDirection: 'row' },
  emptyState: { flex: 1, paddingHorizontal: 36, alignItems: 'center', justifyContent: 'center' },
  emptyTitle: { color: palette.ink, fontSize: 20, fontWeight: '700', textAlign: 'center' },
  emptyText: { marginTop: 10, color: palette.inkMuted, fontSize: 14, lineHeight: 21, textAlign: 'center' },
  disabled: { opacity: 0.38 },
  pressed: { opacity: 0.7 },
});
