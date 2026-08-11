/**
 * Desk Gateway 原型使用的轻量线性图标。
 *
 * 图标直接使用 react-native-svg 绘制，避免为了少量固定图标引入整套图标字体，
 * 同时保证 iOS 与 Android 的线宽和轮廓一致。
 */

import Svg, {
  Circle,
  Line,
  Path,
  Polyline,
  Rect,
} from 'react-native-svg';

interface IconProps {
  size?: number;
  color?: string;
  strokeWidth?: number;
}

const defaults = {
  size: 24,
  color: '#171613',
  strokeWidth: 1.8,
};

export function GearIcon({
  size = defaults.size,
  color = defaults.color,
  strokeWidth = defaults.strokeWidth,
}: IconProps) {
  return (
    <Svg width={size} height={size} viewBox="0 0 24 24" fill="none">
      <Circle cx="12" cy="12" r="3.1" stroke={color} strokeWidth={strokeWidth} />
      <Path
        d="M9.5 2.8 9 5.1a7.3 7.3 0 0 0-1.7 1L5 5.4 3.4 8.2l1.8 1.6a7.5 7.5 0 0 0 0 2.1l-1.8 1.7L5 16.4l2.3-.7a7.4 7.4 0 0 0 1.7 1l.5 2.4h3.2l.6-2.4a7 7 0 0 0 1.7-1l2.3.7 1.6-2.8-1.8-1.7a7.5 7.5 0 0 0 0-2.1l1.8-1.6L17.3 5.4l-2.3.7a7.3 7.3 0 0 0-1.7-1l-.6-2.3H9.5Z"
        stroke={color}
        strokeWidth={strokeWidth}
        strokeLinejoin="round"
      />
    </Svg>
  );
}

export function ChevronIcon({
  direction,
  size = defaults.size,
  color = defaults.color,
  strokeWidth = defaults.strokeWidth,
}: IconProps & { direction: 'left' | 'right' | 'up' | 'down' }) {
  const points = {
    left: '15.5 4 7.5 12 15.5 20',
    right: '8.5 4 16.5 12 8.5 20',
    up: '4 15.5 12 7.5 20 15.5',
    down: '4 8.5 12 16.5 20 8.5',
  }[direction];
  return (
    <Svg width={size} height={size} viewBox="0 0 24 24" fill="none">
      <Polyline
        points={points}
        stroke={color}
        strokeWidth={strokeWidth}
        strokeLinecap="round"
        strokeLinejoin="round"
      />
    </Svg>
  );
}

export function StopIcon({
  size = defaults.size,
  color = defaults.color,
}: IconProps) {
  return (
    <Svg width={size} height={size} viewBox="0 0 24 24" fill="none">
      <Rect x="4.5" y="4.5" width="15" height="15" rx="2.2" fill={color} />
    </Svg>
  );
}

export function LockIcon({
  size = defaults.size,
  color = defaults.color,
  strokeWidth = defaults.strokeWidth,
}: IconProps) {
  return (
    <Svg width={size} height={size} viewBox="0 0 24 24" fill="none">
      <Rect x="5" y="10" width="14" height="11" rx="2" stroke={color} strokeWidth={strokeWidth} />
      <Path d="M8 10V7a4 4 0 0 1 8 0v3" stroke={color} strokeWidth={strokeWidth} strokeLinecap="round" />
    </Svg>
  );
}

export function GlobeIcon({
  size = defaults.size,
  color = defaults.color,
  strokeWidth = defaults.strokeWidth,
}: IconProps) {
  return (
    <Svg width={size} height={size} viewBox="0 0 24 24" fill="none">
      <Circle cx="12" cy="12" r="9" stroke={color} strokeWidth={strokeWidth} />
      <Path d="M3.5 12h17M12 3c2.3 2.5 3.5 5.5 3.5 9S14.3 18.5 12 21M12 3C9.7 5.5 8.5 8.5 8.5 12S9.7 18.5 12 21" stroke={color} strokeWidth={strokeWidth} />
    </Svg>
  );
}

export function BluetoothIcon({
  size = defaults.size,
  color = defaults.color,
  strokeWidth = defaults.strokeWidth,
}: IconProps) {
  return (
    <Svg width={size} height={size} viewBox="0 0 24 24" fill="none">
      <Path d="m7 7 10 10-5 4V3l5 4L7 17" stroke={color} strokeWidth={strokeWidth} strokeLinecap="round" strokeLinejoin="round" />
    </Svg>
  );
}

export function PanelIcon({
  size = defaults.size,
  color = defaults.color,
  strokeWidth = defaults.strokeWidth,
}: IconProps) {
  return (
    <Svg width={size} height={size} viewBox="0 0 24 24" fill="none">
      <Rect x="4" y="3" width="16" height="18" rx="3" stroke={color} strokeWidth={strokeWidth} />
      <Circle cx="9" cy="9" r="1" fill={color} />
      <Circle cx="15" cy="9" r="1" fill={color} />
      <Circle cx="9" cy="15" r="1" fill={color} />
      <Circle cx="15" cy="15" r="1" fill={color} />
    </Svg>
  );
}

export function LinkIcon({
  size = defaults.size,
  color = defaults.color,
  strokeWidth = defaults.strokeWidth,
}: IconProps) {
  return (
    <Svg width={size} height={size} viewBox="0 0 24 24" fill="none">
      <Path d="m9.5 14.5 5-5M7.2 17.8l-1 1a3.6 3.6 0 0 1-5-5l3.4-3.4a3.6 3.6 0 0 1 5 0M16.8 6.2l1-1a3.6 3.6 0 0 1 5 5l-3.4 3.4a3.6 3.6 0 0 1-5 0" stroke={color} strokeWidth={strokeWidth} strokeLinecap="round" />
    </Svg>
  );
}

export function HapticIcon({
  size = defaults.size,
  color = defaults.color,
  strokeWidth = defaults.strokeWidth,
}: IconProps) {
  return (
    <Svg width={size} height={size} viewBox="0 0 24 24" fill="none">
      <Rect x="8" y="3" width="8" height="18" rx="2" stroke={color} strokeWidth={strokeWidth} />
      <Path d="M4.5 7 3 9l1.5 2L3 13l1.5 2M19.5 7 21 9l-1.5 2 1.5 2-1.5 2" stroke={color} strokeWidth={strokeWidth} strokeLinecap="round" />
    </Svg>
  );
}

export function ChairIcon({
  size = defaults.size,
  color = defaults.color,
  strokeWidth = defaults.strokeWidth,
}: IconProps) {
  return (
    <Svg width={size} height={size} viewBox="0 0 32 32" fill="none">
      <Path d="M7 5v14c0 2 1.5 3 3 3h9M10 13h9v9M9 22l-2 6M16 22v6M20 22l3 6M22 15h7M25 15v13" stroke={color} strokeWidth={strokeWidth} strokeLinecap="round" strokeLinejoin="round" />
    </Svg>
  );
}

export function StandingIcon({
  size = defaults.size,
  color = defaults.color,
  strokeWidth = defaults.strokeWidth,
}: IconProps) {
  return (
    <Svg width={size} height={size} viewBox="0 0 32 32" fill="none">
      <Circle cx="8" cy="5" r="2.5" stroke={color} strokeWidth={strokeWidth} />
      <Path d="M8 8v9m0-5 5 3m-5 2-2 11m2-11 4 11M13 15h15M23 15v13" stroke={color} strokeWidth={strokeWidth} strokeLinecap="round" />
    </Svg>
  );
}
