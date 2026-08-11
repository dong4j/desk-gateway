/**
 * 首页桌面原型图与实时高度尺。
 *
 * 桌面主体直接使用已确认的原型截图，避免代码重绘造成细节偏差；右侧高度尺仍根据
 * 设备高度独立动画，从而保留实时反馈能力。
 */

import { useEffect, useRef, useState } from 'react';
import {
  AccessibilityInfo,
  Animated,
  AppState,
  Image,
  StyleSheet,
  View,
} from 'react-native';
import Svg, {
  Circle,
  Defs,
  Ellipse,
  G,
  LinearGradient,
  Line,
  Rect,
  Stop,
  Text as SvgText,
} from 'react-native-svg';

import { palette } from './theme';

interface DeskSceneProps {
  heightMm: number | null;
  minHeightMm?: number;
  maxHeightMm?: number;
}

const SCENE_MIN_MM = 640;
const SCENE_MAX_MM = 1290;
const DESK_WORKSTATION_SOURCE = require('../../assets/desk-workstation.png');
type ScenePeriod = 'day' | 'night';

const STATIC_STARS = [
  [28, 48, 1.2], [77, 31, 1], [116, 69, 1.3], [157, 38, 0.9],
  [201, 59, 1.1], [239, 28, 1.3], [282, 67, 0.8], [324, 36, 1.1],
] as const;
const TWINKLE_STARS = [
  [48, 82, 1.5], [139, 25, 1.2], [222, 79, 1.4], [300, 49, 1.6],
] as const;

export function DeskScene({
  heightMm,
  minHeightMm = SCENE_MIN_MM,
  maxHeightMm = SCENE_MAX_MM,
}: DeskSceneProps) {
  const target = normalizeHeight(heightMm, minHeightMm, maxHeightMm);
  const animated = useRef(new Animated.Value(target)).current;
  const [progress, setProgress] = useState(target);
  const [period, setPeriod] = useState<ScenePeriod>(() => currentScenePeriod());
  const [reduceMotion, setReduceMotion] = useState(false);
  const atmosphere = useRef(new Animated.Value(0)).current;

  useEffect(() => {
    const listener = animated.addListener(({ value }) => setProgress(value));
    return () => animated.removeListener(listener);
  }, [animated]);

  useEffect(() => {
    Animated.timing(animated, {
      toValue: target,
      duration: 260,
      useNativeDriver: false,
    }).start();
  }, [animated, target]);

  useEffect(() => {
    const refreshPeriod = () => setPeriod(currentScenePeriod());
    const timer = setInterval(refreshPeriod, 60_000);
    const appStateSubscription = AppState.addEventListener('change', (state) => {
      if (state === 'active') {
        refreshPeriod();
      }
    });
    return () => {
      clearInterval(timer);
      appStateSubscription.remove();
    };
  }, []);

  useEffect(() => {
    void AccessibilityInfo.isReduceMotionEnabled().then(setReduceMotion);
    const subscription = AccessibilityInfo.addEventListener(
      'reduceMotionChanged',
      setReduceMotion,
    );
    return () => subscription.remove();
  }, []);

  useEffect(() => {
    atmosphere.stopAnimation();
    if (reduceMotion) {
      atmosphere.setValue(period === 'night' ? 0.7 : 0);
      return;
    }

    atmosphere.setValue(0);
    const loop = period === 'night'
      ? Animated.loop(
          Animated.sequence([
            Animated.timing(atmosphere, {
              toValue: 1,
              duration: 2200,
              useNativeDriver: true,
            }),
            Animated.timing(atmosphere, {
              toValue: 0.25,
              duration: 2800,
              useNativeDriver: true,
            }),
          ]),
        )
      : Animated.loop(
          Animated.sequence([
            Animated.timing(atmosphere, {
              toValue: 1,
              duration: 12_000,
              useNativeDriver: true,
            }),
            Animated.timing(atmosphere, {
              toValue: 0,
              duration: 12_000,
              useNativeDriver: true,
            }),
          ]),
        );
    loop.start();
    return () => loop.stop();
  }, [atmosphere, period, reduceMotion]);

  const displayHeight = heightMm === null ? null : Math.round(heightMm / 10);
  const rulerY = 272 - progress * 194;
  const rulerLine = period === 'night' ? 'rgba(245, 239, 225, 0.42)' : palette.line;
  const rulerMuted = period === 'night' ? '#D8D1C5' : palette.inkMuted;
  const rulerInk = period === 'night' ? '#FFF9EE' : palette.ink;
  const atmosphereStyle = period === 'night'
    ? { opacity: atmosphere }
    : {
        transform: [{
          translateX: atmosphere.interpolate({
            inputRange: [0, 1],
            outputRange: [-8, 10],
          }),
        }],
      };

  return (
    <View style={styles.scene} accessibilityLabel="升降桌实时高度示意图">
      <SceneBackdrop period={period} />
      <Animated.View
        pointerEvents="none"
        style={[styles.atmosphere, atmosphereStyle]}
      >
        {period === 'night' ? <TwinkleLayer /> : <CloudLayer />}
      </Animated.View>
      <Image
        source={DESK_WORKSTATION_SOURCE}
        resizeMode="contain"
        fadeDuration={0}
        accessible={false}
        style={styles.workstation}
      />
      <Svg pointerEvents="none" style={styles.ruler} width="100%" height="100%" viewBox="0 0 380 315" fill="none">
        <G>
          <Line x1="346" y1="66" x2="346" y2="272" stroke={rulerLine} strokeWidth="1.4" />
          {[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13].map((tick) => (
            <Line
              key={tick}
              x1="346"
              y1={66 + tick * (206 / 13)}
              x2={tick % 3 === 0 ? '357' : '353'}
              y2={66 + tick * (206 / 13)}
              stroke={tick === 4 ? palette.gold : rulerLine}
              strokeWidth="1.2"
            />
          ))}
          <Circle cx="346" cy={rulerY} r="6" fill={palette.gold} />
          <SvgText x="363" y="71" fontSize="11" fill={rulerMuted}>129</SvgText>
          <SvgText x="363" y="131" fontSize="11" fill={rulerMuted}>110</SvgText>
          <SvgText x="363" y="168" fontSize="11" fill={rulerMuted}>100</SvgText>
          <SvgText x="363" y="219" fontSize="11" fill={rulerMuted}>80</SvgText>
          <SvgText x="363" y="277" fontSize="11" fill={rulerMuted}>64</SvgText>
          <SvgText x="350" y="296" fontSize="10" fill={rulerMuted}>cm</SvgText>
          {displayHeight !== null ? (
            <SvgText x="327" y={rulerY + 4} textAnchor="end" fontSize="11" fill={rulerInk}>{displayHeight}</SvgText>
          ) : null}
        </G>
      </Svg>
    </View>
  );
}

/** Static sky layer; only the subtle cloud/star overlay is animated. */
function SceneBackdrop({ period }: { period: ScenePeriod }) {
  const night = period === 'night';
  return (
    <Svg pointerEvents="none" style={styles.backdrop} viewBox="0 0 380 315">
      <Defs>
        <LinearGradient id="sceneSky" x1="0" y1="0" x2="0" y2="1">
          <Stop offset="0" stopColor={night ? '#17243E' : '#DDEBF0'} />
          <Stop offset="0.58" stopColor={night ? '#263858' : '#F3E8D2'} />
          <Stop offset="1" stopColor={night ? '#3A4357' : '#F8F0E4'} />
        </LinearGradient>
      </Defs>
      <Rect x="0" y="8" width="380" height="287" rx="28" fill="url(#sceneSky)" />
      {night ? (
        <G>
          <Circle cx="54" cy="53" r="18" fill="#FFF2C9" opacity="0.9" />
          <Circle cx="63" cy="46" r="17" fill="#1A2946" />
          {STATIC_STARS.map(([cx, cy, radius], index) => (
            <Circle key={index} cx={cx} cy={cy} r={radius} fill="#FFF8DF" opacity="0.75" />
          ))}
        </G>
      ) : (
        <G>
          <Circle cx="55" cy="51" r="25" fill="#F7C978" opacity="0.24" />
          <Circle cx="55" cy="51" r="14" fill="#F6C269" opacity="0.74" />
          <Ellipse cx="285" cy="75" rx="54" ry="18" fill="#FFFFFF" opacity="0.22" />
        </G>
      )}
    </Svg>
  );
}

/** One slow-moving cloud group keeps the daytime scene calm and inexpensive. */
function CloudLayer() {
  return (
    <Svg width="100%" height="100%" viewBox="0 0 380 315">
      <G fill="#FFFFFF" opacity="0.42">
        <Ellipse cx="92" cy="78" rx="37" ry="11" />
        <Ellipse cx="76" cy="73" rx="17" ry="13" />
        <Ellipse cx="108" cy="70" rx="22" ry="16" />
      </G>
    </Svg>
  );
}

/** A small second star group changes opacity as a single accessible animation. */
function TwinkleLayer() {
  return (
    <Svg width="100%" height="100%" viewBox="0 0 380 315">
      {TWINKLE_STARS.map(([cx, cy, radius], index) => (
        <Circle key={index} cx={cx} cy={cy} r={radius} fill="#FFF7D6" />
      ))}
    </Svg>
  );
}

function currentScenePeriod(date = new Date()): ScenePeriod {
  const hour = date.getHours();
  return hour >= 6 && hour < 18 ? 'day' : 'night';
}

function normalizeHeight(
  heightMm: number | null,
  minHeightMm: number,
  maxHeightMm: number,
): number {
  if (heightMm === null || maxHeightMm <= minHeightMm) {
    return 0.5;
  }
  return Math.max(
    0,
    Math.min(1, (heightMm - minHeightMm) / (maxHeightMm - minHeightMm)),
  );
}

const styles = StyleSheet.create({
  scene: {
    width: '100%',
    height: 305,
  },
  backdrop: {
    position: 'absolute',
    left: 0,
    top: 0,
    right: 0,
    bottom: 0,
  },
  atmosphere: {
    position: 'absolute',
    left: 0,
    top: 0,
    right: 0,
    bottom: 0,
  },
  workstation: {
    position: 'absolute',
    left: 0,
    top: 0,
    width: '88%',
    height: '100%',
    zIndex: 2,
  },
  ruler: {
    position: 'absolute',
    left: 0,
    top: 0,
    zIndex: 3,
  },
});
