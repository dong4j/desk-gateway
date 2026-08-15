/**
 Apple Watch 到 Desk Gateway 的局域网 REST 控制器。

 本类使用 URLSession 读取状态并发送已冻结动作。Crown 请求通过设备侧 500 ms Jog
 租约执行；本地队列会合并续期，并保证 STOP 之后不会残留旧方向请求。
 */

import Combine
import DeskGatewayWatchCore
import Foundation

private enum DeskRESTControllerError: Error, LocalizedError {
  case notConfigured
  case invalidAddress
  case invalidResponse
  case requestFailed(String)

  var errorDescription: String? {
    switch self {
    case .notConfigured: "请先设置网关地址和 REST 密码"
    case .invalidAddress: "网关地址无效"
    case .invalidResponse: "网关响应无效"
    case .requestFailed(let message): message
    }
  }
}

/// 负责 REST 鉴权、状态轮询和运动请求排序。
@MainActor
final class DeskRESTController: ObservableObject, DeskControlling {
  @Published private(set) var phase: DeskConnectionPhase = .idle
  @Published private(set) var deskState: DeskState?
  @Published private(set) var configuration: DeskConfiguration?
  @Published private(set) var reminder: ReminderSnapshot?
  @Published private(set) var errorMessage: String?
  @Published private(set) var controlAllowed = false

  let transport: DeskTransport? = .wifi
  let needsPairingRecovery = false

  private let session: URLSession
  private var baseURL: URL?
  private var restPassword = ""
  private var connectTask: Task<Void, Never>?
  private var pollTask: Task<Void, Never>?
  private var pollFailures = 0

  private var motionQueue = DeskRESTMotionQueue()

  init(session: URLSession? = nil) {
    if let session {
      self.session = session
    } else {
      let configuration = URLSessionConfiguration.ephemeral
      configuration.timeoutIntervalForRequest = 3
      configuration.requestCachePolicy = .reloadIgnoringLocalCacheData
      self.session = URLSession(configuration: configuration)
    }
  }

  var isReady: Bool { phase == .ready }

  /// 密码只保留在当前控制器内存中，不输出到日志或错误信息。
  func configure(host: String, password: String) {
    let trimmed = host.trimmingCharacters(in: .whitespacesAndNewlines)
      .trimmingCharacters(in: CharacterSet(charactersIn: "/"))
    let address = trimmed.contains("://") ? trimmed : "http://\(trimmed)"
    baseURL = URL(string: address)
    restPassword = password
  }

  func connect() {
    guard phase != .connecting, phase != .ready else { return }
    connectTask?.cancel()
    pollTask?.cancel()
    phase = .connecting
    errorMessage = nil
    connectTask = Task { @MainActor [weak self] in
      guard let self else { return }
      do {
        let projection = try await self.fetchStatus()
        guard !Task.isCancelled else { return }
        self.apply(projection)
        self.pollFailures = 0
        self.phase = .ready
        self.startPolling()
      } catch is CancellationError {
        return
      } catch {
        self.fail(error)
      }
    }
  }

  /// 切换通道时尽力发送 STOP，同时立刻停止状态轮询和清除旧快照。
  func disconnect() {
    let shouldStop = isReady
    connectTask?.cancel()
    connectTask = nil
    pollTask?.cancel()
    pollTask = nil
    if shouldStop {
      send(.stop)
    }
    phase = .disconnected
    deskState = nil
    configuration = nil
    reminder = nil
    controlAllowed = false
  }

  /// HOLD 在 REST 中映射为短租约 Jog；STOP 清除未发送续期并优先成为下一条请求。
  func send(_ command: DeskCommand) {
    guard baseURL != nil, !restPassword.isEmpty else {
      errorMessage = DeskRESTControllerError.notConfigured.localizedDescription
      return
    }
    // 高频 Crown 续期只保留最新方向，避免网络抖动时堆积过期动作。
    if let next = motionQueue.submit(command) {
      executeMotionRequest(next)
    }
  }

  func perform(_ action: ReminderAction) {
    let body = ["action": DeskRESTProtocol.reminderActionName(action)]
    runOperation(path: "/api/v1/reminder/action", body: body)
  }

  func resetController() {
    runOperation(path: "/api/v1/desk/controller/reset")
  }

  func reconnect() {
    disconnect()
    connect()
  }

  /// 任一时刻最多发送一个运动请求；方向切换产生的 STOP 必须先于新方向。
  private func executeMotionRequest(_ command: DeskCommand) {
    Task { @MainActor [weak self] in
      guard let self else { return }
      do {
        _ = try await self.request(
          path: DeskRESTProtocol.endpoint(for: command),
          method: "POST"
        )
        self.errorMessage = nil
      } catch {
        self.errorMessage = error.localizedDescription
      }
      if let next = self.motionQueue.complete() {
        self.executeMotionRequest(next)
      }
    }
  }

  private func runOperation(path: String, body: [String: String]? = nil) {
    Task { @MainActor [weak self] in
      guard let self else { return }
      do {
        _ = try await self.request(path: path, method: "POST", body: body)
        self.errorMessage = nil
      } catch {
        self.errorMessage = error.localizedDescription
      }
    }
  }

  private func startPolling() {
    pollTask?.cancel()
    pollTask = Task { @MainActor [weak self] in
      guard let self else { return }
      while !Task.isCancelled, self.phase == .ready {
        let moving =
          self.deskState?.motion != .idle
          || self.deskState?.controllerResetActive == true
        try? await Task.sleep(for: .milliseconds(moving ? 250 : 1_000))
        guard !Task.isCancelled else { return }
        do {
          let projection = try await self.fetchStatus()
          self.pollFailures = 0
          self.apply(projection)
        } catch is CancellationError {
          return
        } catch {
          self.pollFailures += 1
          if self.pollFailures >= 3 {
            self.errorMessage = "局域网连接中断：\(error.localizedDescription)"
            self.phase = .disconnected
            self.controlAllowed = false
            return
          }
        }
      }
    }
  }

  private func fetchStatus() async throws -> DeskRESTProjection {
    let data = try await request(path: "/api/v1/desk/status", method: "GET")
    return try DeskRESTProtocol.decodeStatus(data)
  }

  private func apply(_ projection: DeskRESTProjection) {
    deskState = projection.state
    configuration = projection.configuration
    reminder = projection.reminder
    controlAllowed = projection.restControlAllowed
    errorMessage = nil
  }

  private func fail(_ error: Error) {
    phase = .failed(error.localizedDescription)
    errorMessage = error.localizedDescription
    controlAllowed = false
  }

  private func request(
    path: String,
    method: String,
    body: [String: String]? = nil
  ) async throws -> Data {
    guard let baseURL, !restPassword.isEmpty else {
      throw DeskRESTControllerError.notConfigured
    }
    guard let url = URL(string: path, relativeTo: baseURL)?.absoluteURL else {
      throw DeskRESTControllerError.invalidAddress
    }
    var request = URLRequest(url: url)
    request.httpMethod = method
    request.timeoutInterval = 3
    request.cachePolicy = .reloadIgnoringLocalCacheData
    request.setValue("application/json", forHTTPHeaderField: "Content-Type")
    request.setValue(restPassword, forHTTPHeaderField: "X-Desk-Key")
    if let body {
      request.httpBody = try JSONSerialization.data(withJSONObject: body)
    }

    let (data, response) = try await session.data(for: request)
    guard let httpResponse = response as? HTTPURLResponse else {
      throw DeskRESTControllerError.invalidResponse
    }
    guard (200..<300).contains(httpResponse.statusCode) else {
      let payload = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any]
      let code =
        payload?["error"] as? String
        ?? payload?["err"] as? String
        ?? "HTTP \(httpResponse.statusCode)"
      let reason = payload?["reason"] as? String
      throw DeskRESTControllerError.requestFailed(
        reason.map { "\(code)（\($0)）" } ?? code
      )
    }
    return data
  }
}
