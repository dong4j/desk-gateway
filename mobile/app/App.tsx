/**
 * Desk Gateway 移动端入口。
 *
 * 入口负责 BLE 客户端生命周期、页面切换和仅属于 App 的本地偏好。设备运动安全仍由
 * DeskHoldController、DeskBleClient 与 ESP32 desk_core 三层共同保证，UI 不绕过任何
 * 童锁、来源权限或安全高度裁决。
 */

import AsyncStorage from '@react-native-async-storage/async-storage';
import * as Haptics from 'expo-haptics';
import { StatusBar } from 'expo-status-bar';
import { useCallback, useEffect, useRef, useState } from 'react';
import { AppState } from 'react-native';
import { SafeAreaProvider } from 'react-native-safe-area-context';

import { ReactNativeBleManagerAdapter } from './src/ble/ReactNativeBleManagerAdapter';
import { DeskCommand } from './src/desk/commands';
import {
  DeskBleClient,
  type DeskClientSnapshot,
} from './src/desk/DeskBleClient';
import { DeskHoldController } from './src/desk/DeskHoldController';
import { HomeScreen } from './src/screens/HomeScreen';
import { SettingsScreen } from './src/screens/SettingsScreen';
import { HoldHapticController } from './src/ui/HoldHapticController';

const PREFERENCES_KEY = 'desk-gateway.mobile.preferences.v1';

const initialSnapshot: DeskClientSnapshot = {
  phase: 'uninitialized',
  peripheral: null,
  deskState: null,
  deskConfig: null,
  firmwareRevision: null,
  error: null,
};

interface AppPreferences {
  autoConnect: boolean;
  hapticFeedback: boolean;
  hapticStrength: number;
}

const defaultPreferences: AppPreferences = {
  autoConnect: true,
  hapticFeedback: true,
  hapticStrength: 75,
};

export default function App() {
  const clientRef = useRef<DeskBleClient | null>(null);
  const holdRef = useRef<DeskHoldController | null>(null);
  const holdHapticRef = useRef<HoldHapticController | null>(null);
  const hapticFeedbackEnabledRef = useRef(defaultPreferences.hapticFeedback);
  const hapticStrengthRef = useRef(defaultPreferences.hapticStrength);
  const autoConnectAttemptedRef = useRef(false);
  const [snapshot, setSnapshot] = useState(initialSnapshot);
  const [screen, setScreen] = useState<'home' | 'settings'>('home');
  const [preferences, setPreferences] = useState(defaultPreferences);
  const [preferencesLoaded, setPreferencesLoaded] = useState(false);
  hapticFeedbackEnabledRef.current = preferences.hapticFeedback;
  hapticStrengthRef.current = preferences.hapticStrength;

  if (clientRef.current === null) {
    clientRef.current = new DeskBleClient(
      new ReactNativeBleManagerAdapter(),
    );
    holdRef.current = new DeskHoldController((command) =>
      clientRef.current!.sendCommand(command),
    );
  }
  if (holdHapticRef.current === null) {
    holdHapticRef.current = new HoldHapticController((event) => {
      if (!hapticFeedbackEnabledRef.current) {
        return;
      }
      const operation = event === 'end'
        ? Haptics.selectionAsync()
        : Haptics.impactAsync(
            impactStyleForStrength(hapticStrengthRef.current),
          );
      void operation.catch(() => undefined);
    });
  }

  useEffect(() => {
    holdHapticRef.current?.setPulseIntervalMs(
      pulseIntervalForStrength(preferences.hapticStrength),
    );
  }, [preferences.hapticStrength]);

  useEffect(() => {
    const client = clientRef.current!;
    const unsubscribe = client.subscribe(setSnapshot);
    const appStateSubscription = AppState.addEventListener(
      'change',
      (nextState) => {
        // App 失去前台控制权时必须停止续期；固件 750ms 租约是第二道保护。
        if (nextState !== 'active') {
          holdHapticRef.current?.cancel();
          void holdRef.current?.stop();
        }
      },
    );

    return () => {
      appStateSubscription.remove();
      unsubscribe();
      holdHapticRef.current?.cancel();
      void holdRef.current?.stop();
      client.dispose();
    };
  }, []);

  useEffect(() => {
    if (snapshot.phase !== 'ready') {
      holdHapticRef.current?.cancel();
    }
  }, [snapshot.phase]);

  useEffect(() => {
    let active = true;
    void AsyncStorage.getItem(PREFERENCES_KEY)
      .then((stored) => {
        if (!active || !stored) {
          return;
        }
        const parsed = JSON.parse(stored) as Partial<AppPreferences>;
        setPreferences({
          autoConnect:
            typeof parsed.autoConnect === 'boolean'
              ? parsed.autoConnect
              : defaultPreferences.autoConnect,
          hapticFeedback:
            typeof parsed.hapticFeedback === 'boolean'
              ? parsed.hapticFeedback
              : defaultPreferences.hapticFeedback,
          hapticStrength:
            typeof parsed.hapticStrength === 'number'
              ? normalizeHapticStrength(parsed.hapticStrength)
              : defaultPreferences.hapticStrength,
        });
      })
      .catch(() => undefined)
      .finally(() => {
        if (active) {
          setPreferencesLoaded(true);
        }
      });
    return () => {
      active = false;
    };
  }, []);

  const connect = useCallback(async () => {
    await clientRef.current!.initialize();
    await clientRef.current!.scanAndConnect();
  }, []);

  useEffect(() => {
    if (
      !preferencesLoaded ||
      !preferences.autoConnect ||
      autoConnectAttemptedRef.current ||
      snapshot.phase !== 'uninitialized'
    ) {
      return;
    }
    autoConnectAttemptedRef.current = true;
    runSafely(connect());
  }, [connect, preferences.autoConnect, preferencesLoaded, snapshot.phase]);

  const feedback = useCallback(() => {
    if (preferences.hapticFeedback) {
      void Haptics.impactAsync(
        impactStyleForStrength(preferences.hapticStrength),
      ).catch(() => undefined);
    }
  }, [preferences.hapticFeedback, preferences.hapticStrength]);

  const selectionFeedback = useCallback((force = false) => {
    if (force || preferences.hapticFeedback) {
      void Haptics.selectionAsync().catch(() => undefined);
    }
  }, [preferences.hapticFeedback]);

  const updatePreferences = useCallback(
    (patch: Partial<AppPreferences>) => {
      setPreferences((current) => {
        const next = { ...current, ...patch };
        void AsyncStorage.setItem(PREFERENCES_KEY, JSON.stringify(next)).catch(
          () => undefined,
        );
        return next;
      });
    },
    [],
  );

  const runCommand = useCallback(
    (operation: Promise<void>) => {
      feedback();
      runSafely(operation);
    },
    [feedback],
  );

  const startHold = useCallback(
    (command: typeof DeskCommand.HoldUp | typeof DeskCommand.HoldDown) => {
      holdHapticRef.current!.start();
      runSafely(
        holdRef.current!.start(command).catch((error) => {
          holdHapticRef.current?.cancel();
          throw error;
        }),
      );
    },
    [],
  );

  const endHold = useCallback(() => {
    holdHapticRef.current?.stop();
    runSafely(holdRef.current!.stop());
  }, []);

  return (
    <SafeAreaProvider>
      <StatusBar style="dark" />
      {screen === 'home' ? (
        <HomeScreen
          snapshot={snapshot}
          onConnect={() => runCommand(connect())}
          onOpenSettings={() => {
            feedback();
            setScreen('settings');
          }}
          onHoldUpStart={() => startHold(DeskCommand.HoldUp)}
          onHoldDownStart={() => startHold(DeskCommand.HoldDown)}
          onHoldEnd={endHold}
          onStop={() => {
            holdHapticRef.current?.stop();
            runCommand(clientRef.current!.sendCommand(DeskCommand.Stop));
          }}
          onPreset1={() =>
            runCommand(clientRef.current!.sendCommand(DeskCommand.Preset1))
          }
          onPreset4={() =>
            runCommand(clientRef.current!.sendCommand(DeskCommand.Preset4))
          }
          onToggleChildLock={() =>
            runCommand(
              clientRef.current!.setChildLock(
                !(snapshot.deskConfig?.childLock ?? false),
              ),
            )
          }
        />
      ) : (
        <SettingsScreen
          snapshot={snapshot}
          autoConnect={preferences.autoConnect}
          hapticFeedback={preferences.hapticFeedback}
          hapticStrength={preferences.hapticStrength}
          onBack={() => {
            feedback();
            setScreen('home');
          }}
          onToggleAutoConnect={() => {
            selectionFeedback();
            updatePreferences({ autoConnect: !preferences.autoConnect });
          }}
          onToggleHapticFeedback={() => {
            // 开启震动时旧状态仍为 false，因此强制反馈一次来确认设置已生效。
            selectionFeedback(true);
            updatePreferences({
              hapticFeedback: !preferences.hapticFeedback,
            });
          }}
          onSetHapticStrength={(hapticStrength) => {
            const normalized = normalizeHapticStrength(hapticStrength);
            updatePreferences({ hapticStrength: normalized });
            if (preferences.hapticFeedback) {
              void Haptics.impactAsync(
                impactStyleForStrength(normalized),
              ).catch(() => undefined);
            }
          }}
          onSetChildLock={(enabled) =>
            runCommand(clientRef.current!.setChildLock(enabled))
          }
          onSetSourceEnabled={(source, enabled) =>
            runCommand(clientRef.current!.setSourceEnabled(source, enabled))
          }
          onSetMaxHeightMm={(maxHeightMm) =>
            runCommand(clientRef.current!.setMaxHeightMm(maxHeightMm))
          }
          onRestart={() =>
            runCommand(clientRef.current!.restartGateway())
          }
          onDisconnect={() => {
            feedback();
            runSafely(clientRef.current!.disconnect());
          }}
        />
      )}
    </SafeAreaProvider>
  );
}

/** 客户端已经把错误写入快照；UI 仅负责避免制造未处理 Promise。 */
function runSafely(operation: Promise<void>): void {
  void operation.catch(() => undefined);
}

/** expo-haptics 只提供离散反馈等级，因此滑块在这里映射为最接近的系统等级。 */
function impactStyleForStrength(
  strength: number,
): Haptics.ImpactFeedbackStyle {
  if (strength >= 90) {
    return Haptics.ImpactFeedbackStyle.Rigid;
  }
  if (strength >= 70) {
    return Haptics.ImpactFeedbackStyle.Heavy;
  }
  if (strength >= 45) {
    return Haptics.ImpactFeedbackStyle.Medium;
  }
  if (strength >= 20) {
    return Haptics.ImpactFeedbackStyle.Light;
  }
  return Haptics.ImpactFeedbackStyle.Soft;
}

/** 强度越高，长按期间的触感脉冲越密集，但保留 160ms 下限避免持续轰振。 */
function pulseIntervalForStrength(strength: number): number {
  return Math.round(400 - normalizeHapticStrength(strength) * 2.4);
}

function normalizeHapticStrength(strength: number): number {
  return Math.max(0, Math.min(100, Math.round(strength)));
}
