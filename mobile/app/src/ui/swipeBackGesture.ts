/**
 * 二级页面右滑返回的纯手势判定。
 *
 * 判定与 React Native 视图分离，确保边缘范围、方向和完成阈值可以在
 * Node 测试中稳定验证，不依赖真机触摸事件。
 */

export const SWIPE_BACK_EDGE_WIDTH = 28;
export const SWIPE_BACK_ACTIVATION_DISTANCE = 8;
export const SWIPE_BACK_COMPLETE_DISTANCE = 88;
export const SWIPE_BACK_FAST_DISTANCE = 44;
export const SWIPE_BACK_COMPLETE_VELOCITY = 0.55;

export interface SwipeBackGestureSample {
  startX: number;
  dx: number;
  dy: number;
  vx: number;
}

/** 仅接管从左侧边缘开始、且明显向右的横向手势。 */
export function shouldActivateSwipeBack(
  sample: SwipeBackGestureSample,
): boolean {
  return sample.startX >= 0 &&
    sample.startX <= SWIPE_BACK_EDGE_WIDTH &&
    sample.dx >= SWIPE_BACK_ACTIVATION_DISTANCE &&
    sample.dx > Math.abs(sample.dy) * 1.2;
}

/** 足够远，或达到最小距离且快速甩动时，完成返回。 */
export function shouldCompleteSwipeBack(
  sample: SwipeBackGestureSample,
): boolean {
  return sample.dx >= SWIPE_BACK_COMPLETE_DISTANCE ||
    (sample.dx >= SWIPE_BACK_FAST_DISTANCE &&
      sample.vx >= SWIPE_BACK_COMPLETE_VELOCITY);
}
