/**
 * 首页桌面原型图与实时高度尺。
 *
 * 桌面主体直接使用已确认的原型截图，避免代码重绘造成细节偏差；右侧高度尺仍根据
 * 设备高度独立动画，从而保留实时反馈能力。
 */

import { useEffect, useRef, useState } from 'react';
import { Animated, Image, StyleSheet, View } from 'react-native';
import Svg, { Circle, G, Line, Text as SvgText } from 'react-native-svg';

import { palette } from './theme';

interface DeskSceneProps {
  heightMm: number | null;
  minHeightMm?: number;
  maxHeightMm?: number;
}

const SCENE_MIN_MM = 640;
const SCENE_MAX_MM = 1290;
const DESK_WORKSTATION_SOURCE = require('../../assets/desk-workstation.png');

export function DeskScene({
  heightMm,
  minHeightMm = SCENE_MIN_MM,
  maxHeightMm = SCENE_MAX_MM,
}: DeskSceneProps) {
  const target = normalizeHeight(heightMm, minHeightMm, maxHeightMm);
  const animated = useRef(new Animated.Value(target)).current;
  const [progress, setProgress] = useState(target);

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

  const displayHeight = heightMm === null ? null : Math.round(heightMm / 10);
  const rulerY = 272 - progress * 194;

  return (
    <View style={styles.scene} accessibilityLabel="升降桌实时高度示意图">
      <Image
        source={DESK_WORKSTATION_SOURCE}
        resizeMode="contain"
        fadeDuration={0}
        accessible={false}
        style={styles.workstation}
      />
      <Svg pointerEvents="none" style={styles.ruler} width="100%" height="100%" viewBox="0 0 380 315" fill="none">
        <G>
          <Line x1="346" y1="66" x2="346" y2="272" stroke={palette.line} strokeWidth="1.4" />
          {[0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13].map((tick) => (
            <Line
              key={tick}
              x1="346"
              y1={66 + tick * (206 / 13)}
              x2={tick % 3 === 0 ? '357' : '353'}
              y2={66 + tick * (206 / 13)}
              stroke={tick === 4 ? palette.gold : palette.line}
              strokeWidth="1.2"
            />
          ))}
          <Circle cx="346" cy={rulerY} r="6" fill={palette.gold} />
          <SvgText x="363" y="71" fontSize="11" fill={palette.inkMuted}>129</SvgText>
          <SvgText x="363" y="131" fontSize="11" fill={palette.inkMuted}>110</SvgText>
          <SvgText x="363" y="168" fontSize="11" fill={palette.inkMuted}>100</SvgText>
          <SvgText x="363" y="219" fontSize="11" fill={palette.inkMuted}>80</SvgText>
          <SvgText x="363" y="277" fontSize="11" fill={palette.inkMuted}>64</SvgText>
          <SvgText x="350" y="296" fontSize="10" fill={palette.inkMuted}>cm</SvgText>
          {displayHeight !== null ? (
            <SvgText x="327" y={rulerY + 4} textAnchor="end" fontSize="11" fill={palette.ink}>{displayHeight}</SvgText>
          ) : null}
        </G>
      </Svg>
    </View>
  );
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
  workstation: {
    position: 'absolute',
    left: 0,
    top: 0,
    width: '88%',
    height: '100%',
  },
  ruler: {
    position: 'absolute',
    left: 0,
    top: 0,
  },
});
