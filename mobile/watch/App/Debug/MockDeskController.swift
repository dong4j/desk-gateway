/**
 Watch Simulator 专用的桌面控制器。

 文件整体受 `DEBUG && targetEnvironment(simulator)` 限制。它模拟高度和运动状态，但不
 导入 CoreBluetooth、不发送真实命令，也绝不能作为真机连接失败后的回退路径。
 */

#if DEBUG && targetEnvironment(simulator)
  import Combine
  import DeskGatewayWatchCore
  import Foundation

  /// 为 Simulator UI、Digital Crown 和固定高度交互提供确定性的本地状态。
  @MainActor
  final class MockDeskController: ObservableObject, DeskControlling {
    @Published private(set) var phase: DeskConnectionPhase = .idle
    @Published private(set) var deskState: DeskState?
    @Published private(set) var configuration: DeskConfiguration?
    @Published private(set) var errorMessage: String?

    let isMock = true

    private let minimumHeightMillimeters: UInt16 = 600
    private var currentHeightMillimeters: UInt16 = 724
    private var motionTask: Task<Void, Never>?
    private var connectionTask: Task<Void, Never>?

    init() {
      configuration = DeskConfiguration(
        protocolVersion: 2,
        childLockEnabled: false,
        bluetoothControlAllowed: true,
        maximumHeightMillimeters: 1_020,
        sittingHeightMillimeters: 640,
        standingHeightMillimeters: 1_020
      )
      publishState(motion: .idle)
    }

    var isReady: Bool {
      phase == .ready
    }

    /// 模拟短暂连接过程，让连接态布局也能在 Simulator 中被观察。
    func connect() {
      connectionTask?.cancel()
      phase = .connecting
      connectionTask = Task { @MainActor [weak self] in
        try? await Task.sleep(for: .milliseconds(250))
        guard !Task.isCancelled, let self else { return }
        self.errorMessage = nil
        self.phase = .ready
        self.publishState(motion: .idle)
      }
    }

    /// 执行与真实 GATT 相同的命令语义，但只更新本地模拟高度。
    func send(_ command: DeskCommand) {
      guard isReady else { return }
      switch command {
      case .stop:
        stopMotion()
      case .holdUp:
        startContinuousMotion(.movingUp)
      case .holdDown:
        startContinuousMotion(.movingDown)
      case .preset1:
        startPresetMotion(to: configuration?.sittingHeightMillimeters ?? 640)
      case .preset4:
        startPresetMotion(to: configuration?.standingHeightMillimeters ?? 1_020)
      }
    }

    /// 重连会先停止模拟运动，避免旧命令跨越连接会话。
    func reconnect() {
      stopMotion()
      connect()
    }

    /// HOLD 续期不会重复创建任务；现有 Crown watchdog 负责发送 STOP。
    private func startContinuousMotion(_ motion: DeskMotion) {
      if deskState?.motion == motion, motionTask != nil { return }
      motionTask?.cancel()
      publishState(motion: motion)
      motionTask = Task { @MainActor [weak self] in
        while !Task.isCancelled {
          try? await Task.sleep(for: .milliseconds(100))
          guard !Task.isCancelled, let self else { return }
          self.stepContinuousMotion(motion)
        }
      }
    }

    /// 档位以固定速度逼近目标，达到后回到 idle，便于观察 STOP 状态转换。
    private func startPresetMotion(to target: UInt16) {
      motionTask?.cancel()
      publishState(motion: .gotoPreset)
      motionTask = Task { @MainActor [weak self] in
        while !Task.isCancelled {
          try? await Task.sleep(for: .milliseconds(80))
          guard !Task.isCancelled, let self else { return }
          if self.stepToward(target: target) {
            self.motionTask = nil
            self.publishState(motion: .idle)
            return
          }
        }
      }
    }

    /// 每个 tick 移动 3 mm，并严格限制在 Mock 的物理范围内。
    private func stepContinuousMotion(_ motion: DeskMotion) {
      let maximum = configuration?.maximumHeightMillimeters ?? 1_020
      switch motion {
      case .movingUp:
        currentHeightMillimeters = min(maximum, currentHeightMillimeters + 3)
      case .movingDown:
        currentHeightMillimeters = max(minimumHeightMillimeters, currentHeightMillimeters - 3)
      default:
        return
      }
      publishState(motion: motion)
    }

    /// 返回是否已经到达目标；无符号整数运算前先比较，避免下溢。
    private func stepToward(target: UInt16) -> Bool {
      if currentHeightMillimeters == target { return true }
      if currentHeightMillimeters < target {
        currentHeightMillimeters = min(target, currentHeightMillimeters + 8)
      } else {
        currentHeightMillimeters = max(target, currentHeightMillimeters - 8)
      }
      publishState(motion: .gotoPreset)
      return currentHeightMillimeters == target
    }

    /// STOP 是幂等的，并立即发布 idle，模拟真实设备回读后的页面状态。
    private func stopMotion() {
      motionTask?.cancel()
      motionTask = nil
      publishState(motion: .idle)
    }

    /// 所有状态都带 `heightIsSimulated`，避免调试数据被误认为真实测量。
    private func publishState(motion: DeskMotion) {
      let maximum = configuration?.maximumHeightMillimeters ?? 1_020
      deskState = DeskState(
        motion: motion,
        heightMillimeters: currentHeightMillimeters,
        maximumHeightMillimeters: maximum,
        heightIsSimulated: true,
        childLockEnabled: false,
        bluetoothControlAllowed: true,
        upwardMotionBlocked: currentHeightMillimeters >= maximum
      )
    }
  }
#endif
