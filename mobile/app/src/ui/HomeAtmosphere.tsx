/**
 * 首页全屏昼夜氛围层。
 *
 * 昼夜状态由本地时间决定，并在应用回到前台时刷新；背景只负责视觉表达，始终忽略
 * 触摸事件，避免覆盖升降桌的安全控制入口。
 */

import { useEffect, useRef, useState } from 'react';
import { AccessibilityInfo, Animated, AppState, StyleSheet, View } from 'react-native';
import Svg, { Circle, Defs, Ellipse, G, LinearGradient, Rect, Stop } from 'react-native-svg';

export type HomePeriod = 'day' | 'night';

const NIGHT_STARS = [
  [28, 78, 1.4], [63, 128, 1], [104, 54, 1.3], [148, 105, 0.8],
  [191, 68, 1.1], [232, 135, 1.3], [274, 48, 0.9], [315, 92, 1.2],
  [356, 61, 0.8], [378, 151, 1.1], [42, 236, 0.8], [337, 226, 1],
] as const;

const TWINKLE_STARS = [
  [46, 48, 1.8], [125, 151, 1.4], [215, 42, 1.7], [291, 164, 1.5], [366, 114, 1.7],
] as const;

/** Keeps the page theme aligned with local time without requiring a page reload. */
export function useHomePeriod(): HomePeriod {
  const [period, setPeriod] = useState<HomePeriod>(() => currentHomePeriod());

  useEffect(() => {
    const refresh = () => setPeriod(currentHomePeriod());
    const timer = setInterval(refresh, 60_000);
    const appStateSubscription = AppState.addEventListener('change', (state) => {
      if (state === 'active') {
        refresh();
      }
    });
    return () => {
      clearInterval(timer);
      appStateSubscription.remove();
    };
  }, []);

  return period;
}

/** Renders a single full-page gradient plus one restrained ambient animation. */
export function HomeAtmosphere({ period }: { period: HomePeriod }) {
  const [reduceMotion, setReduceMotion] = useState(false);
  const motion = useRef(new Animated.Value(0)).current;

  useEffect(() => {
    void AccessibilityInfo.isReduceMotionEnabled().then(setReduceMotion);
    const subscription = AccessibilityInfo.addEventListener(
      'reduceMotionChanged',
      setReduceMotion,
    );
    return () => subscription.remove();
  }, []);

  useEffect(() => {
    motion.stopAnimation();
    if (reduceMotion) {
      motion.setValue(period === 'night' ? 0.65 : 0);
      return;
    }

    motion.setValue(0);
    const loop = Animated.loop(
      Animated.sequence([
        Animated.timing(motion, {
          toValue: 1,
          duration: period === 'night' ? 2400 : 14_000,
          useNativeDriver: true,
        }),
        Animated.timing(motion, {
          toValue: period === 'night' ? 0.2 : 0,
          duration: period === 'night' ? 3000 : 14_000,
          useNativeDriver: true,
        }),
      ]),
    );
    loop.start();
    return () => loop.stop();
  }, [motion, period, reduceMotion]);

  const night = period === 'night';
  const animatedStyle = night
    ? { opacity: motion }
    : {
        transform: [{
          translateX: motion.interpolate({
            inputRange: [0, 1],
            outputRange: [-18, 16],
          }),
        }],
      };

  return (
    <View pointerEvents="none" style={StyleSheet.absoluteFill}>
      <Svg width="100%" height="100%" viewBox="0 0 390 844" preserveAspectRatio="xMidYMid slice">
        <Defs>
          <LinearGradient id="homeSky" x1="0" y1="0" x2="0" y2="1">
            <Stop offset="0" stopColor={night ? '#101C32' : '#DDECEF'} />
            <Stop offset="0.48" stopColor={night ? '#1D2F4B' : '#F0EBDD'} />
            <Stop offset="1" stopColor={night ? '#304057' : '#FAF4E9'} />
          </LinearGradient>
        </Defs>
        <Rect width="390" height="844" fill="url(#homeSky)" />
        {night ? (
          <G>
            <Circle cx="326" cy="105" r="35" fill="#FFF0C2" opacity="0.16" />
            <Circle cx="326" cy="105" r="20" fill="#FFF2CB" opacity="0.88" />
            <Circle cx="336" cy="96" r="20" fill="#15243C" />
            {NIGHT_STARS.map(([cx, cy, radius], index) => (
              <Circle key={index} cx={cx} cy={cy} r={radius} fill="#FFF7DD" opacity="0.7" />
            ))}
          </G>
        ) : (
          <G>
            <Circle cx="326" cy="104" r="46" fill="#F6C46D" opacity="0.12" />
            <Circle cx="326" cy="104" r="22" fill="#F4BE5D" opacity="0.58" />
            <Ellipse cx="82" cy="147" rx="78" ry="25" fill="#FFFFFF" opacity="0.18" />
          </G>
        )}
      </Svg>

      <Animated.View style={[styles.motionLayer, animatedStyle]}>
        <Svg width="100%" height="100%" viewBox="0 0 390 844" preserveAspectRatio="xMidYMid slice">
          {night ? (
            TWINKLE_STARS.map(([cx, cy, radius], index) => (
              <Circle key={index} cx={cx} cy={cy} r={radius} fill="#FFF6D7" />
            ))
          ) : (
            <G fill="#FFFFFF" opacity="0.28">
              <Ellipse cx="96" cy="184" rx="47" ry="13" />
              <Ellipse cx="75" cy="178" rx="21" ry="17" />
              <Ellipse cx="117" cy="175" rx="28" ry="21" />
            </G>
          )}
        </Svg>
      </Animated.View>
    </View>
  );
}

function currentHomePeriod(date = new Date()): HomePeriod {
  const hour = date.getHours();
  return hour >= 6 && hour < 18 ? 'day' : 'night';
}

const styles = StyleSheet.create({
  motionLayer: {
    position: 'absolute',
    left: 0,
    top: 0,
    right: 0,
    bottom: 0,
  },
});
