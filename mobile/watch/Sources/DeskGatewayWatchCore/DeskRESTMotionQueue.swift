/**
 REST 运动请求的可测试单飞队列。

 网络请求可能慢于 Crown 事件，队列只保留最新未发送动作。STOP 会清除旧续期，并确保
 方向切换产生的新动作只能在 STOP 完成后发送。
 */

/// 不执行网络 I/O，只决定调用方下一条应发送的运动命令。
public struct DeskRESTMotionQueue: Sendable {
  private var inFlight: DeskCommand?
  private var pending: DeskCommand?
  private var stopRequested = false

  public init() {}

  /// 提交动作；返回值非空时，调用方必须立即发送并在结束后调用 `complete()`。
  public mutating func submit(_ command: DeskCommand) -> DeskCommand? {
    if command == .stop {
      pending = nil
      stopRequested = true
    } else {
      pending = command
    }
    return takeNextIfIdle()
  }

  /// 标记当前请求结束，并返回严格排序后的下一条动作。
  public mutating func complete() -> DeskCommand? {
    if inFlight == .stop {
      stopRequested = false
    }
    inFlight = nil
    return takeNextIfIdle()
  }

  private mutating func takeNextIfIdle() -> DeskCommand? {
    guard inFlight == nil else { return nil }
    let next: DeskCommand
    if stopRequested {
      next = .stop
    } else if let pending {
      next = pending
      self.pending = nil
    } else {
      return nil
    }
    inFlight = next
    return next
  }
}
