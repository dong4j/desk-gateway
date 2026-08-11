/**
 * 与移动端原型一致的跨平台开关外观。
 *
 * 点击与无障碍语义由整行父级 Pressable 统一承接，避免嵌套 Pressable 抢走触摸事件。
 */

import { StyleSheet, View } from 'react-native';

import { palette, radii } from './theme';

interface PrototypeSwitchProps {
  value: boolean;
  muted?: boolean;
}

export function PrototypeSwitch({
  value,
  muted = false,
}: PrototypeSwitchProps) {
  return (
    <View
      importantForAccessibility="no-hide-descendants"
      style={[
        styles.track,
        value && styles.trackOn,
        muted && styles.trackMuted,
      ]}
    >
      <View style={[styles.thumb, value && styles.thumbOn]} />
    </View>
  );
}

const styles = StyleSheet.create({
  track: {
    width: 52,
    height: 31,
    padding: 2,
    justifyContent: 'center',
    borderRadius: radii.pill,
    backgroundColor: palette.disabled,
  },
  trackOn: {
    backgroundColor: palette.gold,
  },
  trackMuted: {
    opacity: 0.68,
  },
  thumb: {
    width: 27,
    height: 27,
    borderRadius: radii.pill,
    backgroundColor: palette.white,
    shadowColor: palette.ink,
    shadowOffset: { width: 0, height: 1 },
    shadowOpacity: 0.18,
    shadowRadius: 2,
    elevation: 2,
  },
  thumbOn: {
    alignSelf: 'flex-end',
  },
});
