/**
 Desk Gateway REST 状态与动作的 watchOS 平台无关映射。

 Watch 网络层只负责发送请求；本文件集中冻结路径、JSON 字段和安全默认值，避免 BLE
 与 REST 控制器分别解释同一份设备状态。未知或损坏状态必须失败，不能继续显示旧状态。
 */

import Foundation

/// REST 状态解码后提供给 Watch UI 的完整投影。
public struct DeskRESTProjection: Equatable, Sendable {
  public let state: DeskState
  public let configuration: DeskConfiguration
  public let reminder: ReminderSnapshot?
  public let restControlAllowed: Bool

  public init(
    state: DeskState,
    configuration: DeskConfiguration,
    reminder: ReminderSnapshot?,
    restControlAllowed: Bool
  ) {
    self.state = state
    self.configuration = configuration
    self.reminder = reminder
    self.restControlAllowed = restControlAllowed
  }
}

/// REST 响应不满足冻结契约时返回稳定错误，便于 UI 和单元测试诊断。
public enum DeskRESTProtocolError: Error, Equatable, LocalizedError {
  case invalidStatus(String)
  case invalidHeight(String)

  public var errorDescription: String? {
    switch self {
    case .invalidStatus(let status):
      return "未知的桌面状态：\(status)"
    case .invalidHeight(let field):
      return "无效的高度字段：\(field)"
    }
  }
}

/// Watch REST 控制器使用的唯一动作与状态映射入口。
public enum DeskRESTProtocol {
  /// Crown 必须使用 500 ms 设备侧 Jog 租约，不能映射到最长 15 秒的 REST Hold。
  public static func endpoint(for command: DeskCommand) -> String {
    switch command {
    case .stop: "/api/v1/desk/stop"
    case .holdUp: "/api/v1/desk/jog/up"
    case .holdDown: "/api/v1/desk/jog/down"
    case .preset1: "/api/v1/desk/preset/1/goto"
    case .preset4: "/api/v1/desk/preset/4/goto"
    }
  }

  public static func reminderActionName(_ action: ReminderAction) -> String {
    switch action {
    case .startFocus: "start_focus"
    case .startBreak: "start_break"
    case .pause: "pause"
    case .resume: "resume"
    case .skip: "skip"
    case .stop: "stop"
    case .snooze: "snooze"
    }
  }

  /// 解码 `/api/v1/desk/status`；缺省值仅用于兼容旧固件的非安全配置字段。
  public static func decodeStatus(_ data: Data) throws -> DeskRESTProjection {
    let response = try JSONDecoder().decode(StatusResponse.self, from: data)
    let motion = try decodeMotion(response.status)
    let minimum = try height(response.minHeightMillimeters ?? 550, field: "min_height_mm")
    let maximum = try height(response.maxHeightMillimeters ?? 940, field: "max_height_mm")
    let sitting = try height(
      response.preset1HeightMillimeters ?? 550,
      field: "preset1_height_mm"
    )
    let standing = try height(
      response.preset4HeightMillimeters ?? 870,
      field: "preset4_height_mm"
    )
    let current =
      response.heightKnown == true
      ? try response.heightMillimeters.map { try height($0, field: "height_mm") }
      : nil
    let bluetoothAllowed = response.controlSources?.bluetooth != false
    let restAllowed = response.controlSources?.rest != false
    let childLock = response.childLock == true

    return DeskRESTProjection(
      state: DeskState(
        motion: motion,
        heightMillimeters: current,
        maximumHeightMillimeters: maximum,
        heightIsSimulated: response.heightSimulated == true,
        childLockEnabled: childLock,
        bluetoothControlAllowed: bluetoothAllowed,
        upwardMotionBlocked: response.upwardBlocked == true,
        controllerResetSupported: response.controllerResetSupported == true,
        controllerResetActive: response.controllerResetActive == true,
        controllerResetRecommended: response.controllerResetRecommended == true
      ),
      configuration: DeskConfiguration(
        protocolVersion: 3,
        childLockEnabled: childLock,
        bluetoothControlAllowed: bluetoothAllowed,
        minimumHeightMillimeters: minimum,
        maximumHeightMillimeters: maximum,
        sittingHeightMillimeters: sitting,
        standingHeightMillimeters: standing
      ),
      reminder: decodeReminder(response.reminder, audio: response.audio),
      restControlAllowed: restAllowed
    )
  }

  private static func decodeMotion(_ status: String) throws -> DeskMotion {
    switch status {
    case "idle": .idle
    case "moving_up": .movingUp
    case "moving_down": .movingDown
    case "goto_preset": .gotoPreset
    case "controller_resetting": .idle
    case "error": .error
    default: throw DeskRESTProtocolError.invalidStatus(status)
    }
  }

  private static func height(_ value: Int, field: String) throws -> UInt16 {
    guard value >= 0, value <= Int(UInt16.max) else {
      throw DeskRESTProtocolError.invalidHeight(field)
    }
    return UInt16(value)
  }

  private static func decodeReminder(
    _ reminder: ReminderResponse?,
    audio: AudioResponse?
  ) -> ReminderSnapshot? {
    guard let reminder, reminder.available == true,
      let state = reminderState(reminder.state),
      let phase = reminderPhase(reminder.phase)
    else {
      return nil
    }
    return ReminderSnapshot(
      state: state,
      phase: phase,
      remainingSeconds: UInt32(clamping: reminder.remainingSeconds ?? 0),
      completedFocusCount: UInt32(clamping: reminder.completedFocusCount ?? 0),
      available: true,
      audioAvailable: audio?.available == true,
      audioEnabled: audio?.enabled == true,
      audioPlaying: audio?.playing == true,
      volumePercent: UInt8(clamping: audio?.volumePercent ?? 0),
      focusMinutes: UInt8(clamping: reminder.focusMinutes ?? 25),
      shortBreakMinutes: UInt8(clamping: reminder.shortBreakMinutes ?? 5),
      longBreakMinutes: UInt8(clamping: reminder.longBreakMinutes ?? 15),
      focusesPerLongBreak: UInt8(clamping: reminder.focusesPerLongBreak ?? 4)
    )
  }

  private static func reminderState(_ value: String?) -> ReminderState? {
    switch value {
    case "idle": .idle
    case "running": .running
    case "paused": .paused
    case "waiting": .waiting
    case "snoozed": .snoozed
    default: nil
    }
  }

  private static func reminderPhase(_ value: String?) -> ReminderPhase? {
    switch value {
    case "focus": .focus
    case "short_break": .shortBreak
    case "long_break": .longBreak
    default: nil
    }
  }
}

private struct StatusResponse: Decodable {
  let status: String
  let heightMillimeters: Int?
  let heightKnown: Bool?
  let heightSimulated: Bool?
  let childLock: Bool?
  let upwardBlocked: Bool?
  let controllerResetSupported: Bool?
  let controllerResetActive: Bool?
  let controllerResetRecommended: Bool?
  let minHeightMillimeters: Int?
  let maxHeightMillimeters: Int?
  let preset1HeightMillimeters: Int?
  let preset4HeightMillimeters: Int?
  let controlSources: ControlSourcesResponse?
  let reminder: ReminderResponse?
  let audio: AudioResponse?

  enum CodingKeys: String, CodingKey {
    case status
    case heightMillimeters = "height_mm"
    case heightKnown = "height_known"
    case heightSimulated = "height_sim"
    case childLock = "child_lock"
    case upwardBlocked = "upward_blocked"
    case controllerResetSupported = "controller_reset_supported"
    case controllerResetActive = "controller_reset_active"
    case controllerResetRecommended = "controller_reset_recommended"
    case minHeightMillimeters = "min_height_mm"
    case maxHeightMillimeters = "max_height_mm"
    case preset1HeightMillimeters = "preset1_height_mm"
    case preset4HeightMillimeters = "preset4_height_mm"
    case controlSources = "control_sources"
    case reminder
    case audio
  }
}

private struct ControlSourcesResponse: Decodable {
  let rest: Bool?
  let bluetooth: Bool?
}

private struct ReminderResponse: Decodable {
  let available: Bool?
  let state: String?
  let phase: String?
  let remainingSeconds: Int?
  let completedFocusCount: Int?
  let focusMinutes: Int?
  let shortBreakMinutes: Int?
  let longBreakMinutes: Int?
  let focusesPerLongBreak: Int?

  enum CodingKeys: String, CodingKey {
    case available
    case state
    case phase
    case remainingSeconds = "remaining_sec"
    case completedFocusCount = "completed_focus_count"
    case focusMinutes = "focus_minutes"
    case shortBreakMinutes = "short_break_minutes"
    case longBreakMinutes = "long_break_minutes"
    case focusesPerLongBreak = "focuses_per_long_break"
  }
}

private struct AudioResponse: Decodable {
  let available: Bool?
  let enabled: Bool?
  let playing: Bool?
  let volumePercent: Int?

  enum CodingKeys: String, CodingKey {
    case available
    case enabled
    case playing
    case volumePercent = "volume_percent"
  }
}
