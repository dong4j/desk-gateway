/**
 Digital Crown 输入与 BLE 命令之间的 watchOS 协调层。

 平台无关状态机决定 start/renew/stop；本类只提供单调时钟、50 ms watchdog、命令映射
 和轻量触感，保证停止旋转不会留下常驻运动状态。
 */

import Combine
import DeskGatewayWatchCore
import Foundation
import WatchKit

/// 在主线程串行处理 Crown 事件，避免 UI 和 watchdog 同时修改状态机。
@MainActor
final class CrownMotionCoordinator: ObservableObject {
  @Published private(set) var activeDirection: CrownDirection?

  private var engine = CrownMotionEngine()
  private let sendCommand: @MainActor (DeskCommand) -> Void
  private var watchdogTask: Task<Void, Never>?

  init(sendCommand: @escaping @MainActor (DeskCommand) -> Void) {
    self.sendCommand = sendCommand
  }

  /// 页面获得 Crown 焦点时建立位置基线，第一帧本身不能启动桌面。
  func prime(position: Double) {
    _ = engine.consume(position: position, at: ProcessInfo.processInfo.systemUptime)
  }

  /// 将一次 Crown 位置更新送入状态机；权限不足时立即进入安全停止。
  func consume(position: Double, canMoveUp: Bool, canMoveDown: Bool) {
    guard canMoveUp || canMoveDown else {
      forceStop(sendEvenIfIdle: false)
      return
    }
    let actions = engine.consume(
      position: position,
      at: ProcessInfo.processInfo.systemUptime,
      canMoveUp: canMoveUp,
      canMoveDown: canMoveDown
    )
    execute(actions)
    ensureWatchdog()
  }

  /// 点击 STOP 或生命周期退出时清除 Crown 状态；必要时即使本地空闲也发送 STOP。
  /// 生命周期安全停止可以关闭触感，避免普通页面导航被误认为一次用户操作。
  func forceStop(sendEvenIfIdle: Bool = true, playHaptic: Bool = true) {
    let actions = engine.forceStop()
    watchdogTask?.cancel()
    watchdogTask = nil
    if actions.isEmpty && sendEvenIfIdle {
      sendCommand(.stop)
      if playHaptic {
        WKInterfaceDevice.current().play(.stop)
      }
    } else {
      execute(actions, playHaptic: playHaptic)
    }
  }

  /// 只在本地运动期间运行 watchdog，避免常驻计时器浪费 Watch 电量。
  private func ensureWatchdog() {
    guard engine.activeDirection != nil, watchdogTask == nil else {
      return
    }
    watchdogTask = Task { @MainActor [weak self] in
      while !Task.isCancelled {
        try? await Task.sleep(for: .milliseconds(50))
        guard let self else {
          return
        }
        let actions = self.engine.tick(at: ProcessInfo.processInfo.systemUptime)
        self.execute(actions)
        if self.engine.activeDirection == nil {
          self.watchdogTask = nil
          return
        }
      }
    }
  }

  /// 保持动作顺序，方向切换时 STOP 必须先于相反方向 HOLD 入队。
  private func execute(_ actions: [CrownMotionAction], playHaptic: Bool = true) {
    for action in actions {
      switch action {
      case .start(let direction):
        sendCommand(command(for: direction))
        if playHaptic {
          WKInterfaceDevice.current().play(.start)
        }
      case .renew(let direction):
        sendCommand(command(for: direction))
      case .stop:
        sendCommand(.stop)
        if playHaptic {
          WKInterfaceDevice.current().play(.stop)
        }
      }
    }
    activeDirection = engine.activeDirection
  }

  /// Crown 方向只映射到冻结的 HOLD 命令，不在 Watch 端生成新协议值。
  private func command(for direction: CrownDirection) -> DeskCommand {
    direction == .up ? .holdUp : .holdDown
  }
}
