/**
 Watch BLE / REST 双通道连接管理器。

 自动模式优先 BLE，连接超时或不可用时才切换 REST。切换只建立新会话，不重放先前
 Crown、档位或番茄动作；运动停止仍由客户端 STOP 和设备侧短租约共同兜底。
 */

import Combine
import DeskGatewayWatchCore
import Foundation

/// 将两个具体控制器投影成 ContentView 使用的单一状态源。
@MainActor
final class DeskConnectionManager: ObservableObject, DeskControlling {
  @Published private(set) var phase: DeskConnectionPhase = .idle
  @Published private(set) var transport: DeskTransport?
  @Published private(set) var deskState: DeskState?
  @Published private(set) var configuration: DeskConfiguration?
  @Published private(set) var reminder: ReminderSnapshot?
  @Published private(set) var errorMessage: String?
  @Published private(set) var needsPairingRecovery = false
  @Published private(set) var controlAllowed = false

  private enum ActiveTransport {
    case none
    case ble
    case wifi
  }

  private let ble = DeskBLECentral()
  private let rest = DeskRESTController()
  private let settings: DeskConnectionSettings
  private var activeTransport: ActiveTransport = .none
  private var fallbackTask: Task<Void, Never>?
  private var cancellables = Set<AnyCancellable>()

  init(settings: DeskConnectionSettings) {
    self.settings = settings
    observeChildren()
  }

  var isReady: Bool { phase == .ready }
  var isMock: Bool { false }

  func connect() {
    fallbackTask?.cancel()
    switch settings.mode {
    case .automatic:
      activateBLE(enableFallback: settings.restConfiguration() != nil)
    case .ble:
      activateBLE(enableFallback: false)
    case .wifi:
      activateREST()
    }
  }

  func disconnect() {
    fallbackTask?.cancel()
    fallbackTask = nil
    activeTransport = .none
    ble.disconnect()
    rest.disconnect()
    clearSnapshot(phase: .disconnected)
  }

  func reconnect() {
    disconnect()
    connect()
  }

  func send(_ command: DeskCommand) {
    switch activeTransport {
    case .ble: ble.send(command)
    case .wifi: rest.send(command)
    case .none: errorMessage = "DeskGateway 尚未连接"
    }
  }

  func perform(_ action: ReminderAction) {
    switch activeTransport {
    case .ble: ble.perform(action)
    case .wifi: rest.perform(action)
    case .none: errorMessage = "DeskGateway 尚未连接"
    }
  }

  func resetController() {
    switch activeTransport {
    case .ble: ble.resetController()
    case .wifi: rest.resetController()
    case .none: errorMessage = "DeskGateway 尚未连接"
    }
  }

  private func activateBLE(enableFallback: Bool) {
    activeTransport = .ble
    rest.disconnect()
    publishBLE()
    ble.connect()
    guard enableFallback else { return }
    fallbackTask = Task { @MainActor [weak self] in
      try? await Task.sleep(for: .seconds(5))
      guard !Task.isCancelled, let self,
        self.activeTransport == .ble,
        !self.ble.isReady
      else {
        return
      }
      self.activateREST()
    }
  }

  private func activateREST() {
    fallbackTask?.cancel()
    fallbackTask = nil
    guard let configuration = settings.restConfiguration() else {
      activeTransport = .none
      clearSnapshot(phase: .failed("请先设置 REST 密码"))
      errorMessage = "请先设置 REST 密码"
      return
    }
    // 先切换 active 标记，再断开 BLE，避免断开回调重复触发自动回退。
    activeTransport = .wifi
    ble.disconnect()
    rest.configure(host: configuration.host, password: configuration.password)
    publishREST()
    rest.connect()
  }

  private func observeChildren() {
    ble.objectWillChange
      .sink { [weak self] _ in
        Task { @MainActor [weak self] in
          await Task.yield()
          self?.childChanged(.ble)
        }
      }
      .store(in: &cancellables)
    rest.objectWillChange
      .sink { [weak self] _ in
        Task { @MainActor [weak self] in
          await Task.yield()
          self?.childChanged(.wifi)
        }
      }
      .store(in: &cancellables)
  }

  private func childChanged(_ child: ActiveTransport) {
    guard child == activeTransport else { return }
    switch child {
    case .ble:
      publishBLE()
      if ble.isReady {
        fallbackTask?.cancel()
        fallbackTask = nil
      } else if settings.mode == .automatic, settings.restConfiguration() != nil,
        ble.phase == .bluetoothUnavailable || ble.phase == .disconnected || bleIsFailed
      {
        activateREST()
      }
    case .wifi:
      publishREST()
    case .none:
      break
    }
  }

  private var bleIsFailed: Bool {
    if case .failed = ble.phase { return true }
    return false
  }

  private func publishBLE() {
    phase = ble.phase
    transport = .ble
    deskState = ble.deskState
    configuration = ble.configuration
    reminder = ble.reminder
    errorMessage = ble.errorMessage
    needsPairingRecovery = ble.needsPairingRecovery
    controlAllowed = ble.controlAllowed
  }

  private func publishREST() {
    phase = rest.phase
    transport = .wifi
    deskState = rest.deskState
    configuration = rest.configuration
    reminder = rest.reminder
    errorMessage = rest.errorMessage
    needsPairingRecovery = false
    controlAllowed = rest.controlAllowed
  }

  private func clearSnapshot(phase: DeskConnectionPhase) {
    self.phase = phase
    transport = nil
    deskState = nil
    configuration = nil
    reminder = nil
    errorMessage = nil
    needsPairingRecovery = false
    controlAllowed = false
  }
}
