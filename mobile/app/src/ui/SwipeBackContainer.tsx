/**
 * App 二级页面共用的左侧边缘右滑返回容器。
 *
 * 项目当前使用本地 screen 状态切页，没有 Navigation Stack。该容器只补充
 * 手势入口，原有返回按钮仍是无障碍和非触摸场景下的主要兜底入口。
 */

import type { PropsWithChildren } from 'react';
import { useEffect, useMemo, useRef } from 'react';
import {
  Animated,
  Dimensions,
  PanResponder,
  StyleSheet,
  View,
} from 'react-native';

import { palette } from './theme';
import {
  shouldActivateSwipeBack,
  shouldCompleteSwipeBack,
} from './swipeBackGesture';

interface SwipeBackContainerProps extends PropsWithChildren {
  onBack: () => void;
}

const RETURN_ANIMATION_MS = 170;

export function SwipeBackContainer({
  children,
  onBack,
}: SwipeBackContainerProps) {
  const translateX = useRef(new Animated.Value(0)).current;
  const onBackRef = useRef(onBack);
  const completingRef = useRef(false);

  useEffect(() => {
    onBackRef.current = onBack;
  }, [onBack]);

  const resetPosition = () => {
    Animated.spring(translateX, {
      toValue: 0,
      speed: 22,
      bounciness: 0,
      useNativeDriver: true,
    }).start();
  };

  const panResponder = useMemo(() => PanResponder.create({
    onMoveShouldSetPanResponderCapture: (_event, gesture) =>
      !completingRef.current && shouldActivateSwipeBack({
        startX: gesture.x0,
        dx: gesture.dx,
        dy: gesture.dy,
        vx: gesture.vx,
      }),
    onPanResponderGrant: () => {
      // 新手势开始时终止上一次回弹，避免两个原生动画同时写 translateX。
      translateX.stopAnimation();
    },
    onPanResponderMove: (_event, gesture) => {
      translateX.setValue(Math.max(0, gesture.dx));
    },
    onPanResponderRelease: (_event, gesture) => {
      if (!shouldCompleteSwipeBack({
        startX: gesture.x0,
        dx: gesture.dx,
        dy: gesture.dy,
        vx: gesture.vx,
      })) {
        resetPosition();
        return;
      }

      completingRef.current = true;
      Animated.timing(translateX, {
        toValue: Math.max(Dimensions.get('window').width, 320),
        duration: RETURN_ANIMATION_MS,
        useNativeDriver: true,
      }).start(({ finished }) => {
        completingRef.current = false;
        translateX.setValue(0);
        if (finished) {
          onBackRef.current();
        }
      });
    },
    // 系统或父级中断手势时必须回到原位，不能留下半屏页面。
    onPanResponderTerminate: () => {
      completingRef.current = false;
      resetPosition();
    },
  }), [translateX]);

  return (
    <View style={styles.container}>
      <Animated.View
        {...panResponder.panHandlers}
        style={[styles.container, { transform: [{ translateX }] }]}
      >
        {children}
      </Animated.View>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: palette.background,
  },
});
