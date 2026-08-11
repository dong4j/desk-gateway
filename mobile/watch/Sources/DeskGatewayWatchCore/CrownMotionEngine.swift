/**
 Digital Crown 运动意图状态机。

 状态机只处理可测试的时间和位置增量，不发送 BLE。累计 Crown 位置不能变成持续运动
 开关；新增旋转输入建立短时运动意图，watchdog 在意图有效期内续期，超时必须产生 STOP。
 */

import Foundation

/// Crown 输入对应的桌面方向。
public enum CrownDirection: Equatable, Sendable {
  case up
  case down
}

/// 状态机要求传输层执行的安全动作。
public enum CrownMotionAction: Equatable, Sendable {
  case start(CrownDirection)
  case renew(CrownDirection)
  case stop
}

/// 把连续 Crown 位置转换为 HOLD 续期和 STOP 的确定性状态机。
public struct CrownMotionEngine: Sendable {
  public private(set) var activeDirection: CrownDirection?

  private static let timeComparisonTolerance: TimeInterval = 1e-9
  private let minimumDelta: Double
  private let renewalInterval: TimeInterval
  private let inactivityTimeout: TimeInterval
  private var previousPosition: Double?
  private var lastInputTime: TimeInterval?
  private var lastRenewalTime: TimeInterval?

  public init(
    minimumDelta: Double = 0.001,
    renewalInterval: TimeInterval = 0.25,
    inactivityTimeout: TimeInterval = 0.5
  ) {
    self.minimumDelta = minimumDelta
    self.renewalInterval = renewalInterval
    self.inactivityTimeout = inactivityTimeout
  }

  /// 消费一次 Crown 位置变化；首次采样只建立基线，不允许意外启动桌面。
  public mutating func consume(position: Double, at time: TimeInterval) -> [CrownMotionAction] {
    guard let previousPosition else {
      self.previousPosition = position
      return []
    }
    self.previousPosition = position

    let delta = position - previousPosition
    guard abs(delta) >= minimumDelta else {
      return []
    }

    let direction: CrownDirection = delta > 0 ? .up : .down
    lastInputTime = time
    if activeDirection != direction {
      let mustStopPreviousDirection = activeDirection != nil
      activeDirection = direction
      lastRenewalTime = time
      return mustStopPreviousDirection ? [.stop, .start(direction)] : [.start(direction)]
    }

    if let lastRenewalTime, time - lastRenewalTime >= renewalInterval {
      self.lastRenewalTime = time
      return [.renew(direction)]
    }
    return []
  }

  /// Watchdog 定期调用；真机 Crown 回调稀疏时仍续期，停转约 500 ms 后结束运动。
  public mutating func tick(at time: TimeInterval) -> [CrownMotionAction] {
    guard let activeDirection, let lastInputTime else {
      return []
    }

    // STOP 必须优先于续期，避免恰好到期的 tick 再延长 ESP32 HOLD 租约。
    if time - lastInputTime >= inactivityTimeout - Self.timeComparisonTolerance {
      clearMotion()
      return [.stop]
    }

    // 物理 Crown 可能合并或稀疏投递回调；只要用户的短时运动意图仍有效，就由
    // watchdog 保持固定续期节奏，不能把 BLE 安全租约绑定到 UI 回调密度。
    if let lastRenewalTime,
      time - lastRenewalTime >= renewalInterval - Self.timeComparisonTolerance
    {
      self.lastRenewalTime = time
      return [.renew(activeDirection)]
    }
    return []
  }

  /// 页面退出、点击停止或权限变化时无条件清除本地运动状态。
  public mutating func forceStop() -> [CrownMotionAction] {
    guard activeDirection != nil else {
      return []
    }
    clearMotion()
    return [.stop]
  }

  /// 保留位置基线，只清除运动租约，避免重新聚焦时第一帧误启动。
  private mutating func clearMotion() {
    activeDirection = nil
    lastInputTime = nil
    lastRenewalTime = nil
  }
}
