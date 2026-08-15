/**
 Watch 主界面使用的最小桌面控制契约。

 真实设备和 Simulator Mock 共享这组状态与命令，但 Mock 只能在编译期限定的 Debug
 Simulator 构建中实例化，不能成为真机或 Release 的运行时回退。
 */

import Combine
import DeskGatewayWatchCore

/// Watch 当前实际使用的控制通道；自动模式也必须展示最终选中的通道。
enum DeskTransport: Equatable {
  case ble
  case wifi
  case mock

  var label: String {
    switch self {
    case .ble: "BLE"
    case .wifi: "Wi-Fi"
    case .mock: "模拟"
    }
  }
}

/// 与移动端一致的连接偏好；自动模式优先 BLE，失败后才尝试局域网 REST。
enum DeskConnectionMode: String, CaseIterable, Identifiable {
  case automatic
  case ble
  case wifi

  var id: String { rawValue }

  var label: String {
    switch self {
    case .automatic: "自动"
    case .ble: "BLE"
    case .wifi: "Wi-Fi"
    }
  }
}

/// Watch UI 可观察的连接阶段。
enum DeskConnectionPhase: Equatable {
  case idle
  case bluetoothUnavailable
  case scanning
  case connecting
  case pairing
  case ready
  case disconnected
  case failed(String)

  var label: String {
    switch self {
    case .idle: "准备连接"
    case .bluetoothUnavailable: "蓝牙不可用"
    case .scanning: "正在查找"
    case .connecting: "正在连接"
    case .pairing: "等待配对"
    case .ready: "已连接"
    case .disconnected: "连接已断开"
    case .failed: "连接失败"
    }
  }
}

/// ContentView 所需的窄接口，避免 UI 感知 CoreBluetooth 或 Mock 细节。
@MainActor
protocol DeskControlling: ObservableObject {
  var phase: DeskConnectionPhase { get }
  var transport: DeskTransport? { get }
  var deskState: DeskState? { get }
  var configuration: DeskConfiguration? { get }
  var reminder: ReminderSnapshot? { get }
  var errorMessage: String? { get }
  var needsPairingRecovery: Bool { get }
  var controlAllowed: Bool { get }
  var isReady: Bool { get }
  var isMock: Bool { get }

  func connect()
  func disconnect()
  func send(_ command: DeskCommand)
  func perform(_ action: ReminderAction)
  func resetController()
  func reconnect()
}

extension DeskControlling {
  /// 真实控制器默认不是 Mock；只有 Simulator 实现显式覆盖。
  var isMock: Bool { false }
}
