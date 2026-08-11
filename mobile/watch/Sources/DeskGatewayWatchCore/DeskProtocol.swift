/**
 Desk Gateway GATT v1 的平台无关协议模型。

 CoreBluetooth 只负责传输；本文件对长度、版本和枚举值采取 fail-closed，避免损坏的
 Notify 数据被解释为可继续运动的安全状态。
 */

import Foundation

/// Command Characteristic 接受的单字节控制命令。
public enum DeskCommand: UInt8, Sendable {
  case stop = 0x00
  case holdUp = 0x01
  case holdDown = 0x02
  case preset1 = 0x11
  case preset4 = 0x14
}

/// State Characteristic 暴露的桌面运动状态。
public enum DeskMotion: UInt8, Sendable {
  case idle = 0x00
  case movingUp = 0x01
  case movingDown = 0x02
  case gotoPreset = 0x03
  case error = 0x04
}

/// 固定 8 字节 State Characteristic 的解码结果。
public struct DeskState: Equatable, Sendable {
  public let motion: DeskMotion
  public let heightMillimeters: UInt16?
  public let maximumHeightMillimeters: UInt16
  public let heightIsSimulated: Bool
  public let childLockEnabled: Bool
  public let bluetoothControlAllowed: Bool
  public let upwardMotionBlocked: Bool

  public init(
    motion: DeskMotion,
    heightMillimeters: UInt16?,
    maximumHeightMillimeters: UInt16,
    heightIsSimulated: Bool,
    childLockEnabled: Bool,
    bluetoothControlAllowed: Bool,
    upwardMotionBlocked: Bool
  ) {
    self.motion = motion
    self.heightMillimeters = heightMillimeters
    self.maximumHeightMillimeters = maximumHeightMillimeters
    self.heightIsSimulated = heightIsSimulated
    self.childLockEnabled = childLockEnabled
    self.bluetoothControlAllowed = bluetoothControlAllowed
    self.upwardMotionBlocked = upwardMotionBlocked
  }
}

/// Config v1/v2 的只读快照；固定高度必须以 ESP32 回读值为准。
public struct DeskConfiguration: Equatable, Sendable {
  public let protocolVersion: UInt8
  public let childLockEnabled: Bool
  public let bluetoothControlAllowed: Bool
  public let maximumHeightMillimeters: UInt16
  public let sittingHeightMillimeters: UInt16
  public let standingHeightMillimeters: UInt16

  public init(
    protocolVersion: UInt8,
    childLockEnabled: Bool,
    bluetoothControlAllowed: Bool,
    maximumHeightMillimeters: UInt16,
    sittingHeightMillimeters: UInt16,
    standingHeightMillimeters: UInt16
  ) {
    self.protocolVersion = protocolVersion
    self.childLockEnabled = childLockEnabled
    self.bluetoothControlAllowed = bluetoothControlAllowed
    self.maximumHeightMillimeters = maximumHeightMillimeters
    self.sittingHeightMillimeters = sittingHeightMillimeters
    self.standingHeightMillimeters = standingHeightMillimeters
  }
}

/// 严格解码失败的稳定错误，便于测试和 UI 给出可诊断原因。
public enum DeskProtocolError: Error, Equatable, LocalizedError {
  case invalidLength(expected: String, actual: Int)
  case unsupportedVersion(UInt8)
  case unknownMotion(UInt8)

  public var errorDescription: String? {
    switch self {
    case .invalidLength(let expected, let actual):
      return "Invalid packet length: expected \(expected), got \(actual)"
    case .unsupportedVersion(let version):
      return "Unsupported protocol version: \(version)"
    case .unknownMotion(let value):
      return "Unknown motion state: \(value)"
    }
  }
}

/// UUID 和字节编解码的唯一 Swift 入口。
public enum DeskProtocol {
  public static let serviceUUID = "7F4E0001-6D4C-4F4B-9F7A-3C1D2E5A9B10"
  public static let commandUUID = "7F4E0002-6D4C-4F4B-9F7A-3C1D2E5A9B10"
  public static let stateUUID = "7F4E0003-6D4C-4F4B-9F7A-3C1D2E5A9B10"
  public static let configUUID = "7F4E0004-6D4C-4F4B-9F7A-3C1D2E5A9B10"
  public static let advertisingName = "DeskGateway"

  private static let unknownHeight = UInt16.max

  /// 编码单字节命令，不在 Watch 端扩展固件未定义的控制值。
  public static func encode(_ command: DeskCommand) -> Data {
    Data([command.rawValue])
  }

  /// 严格解码 State v1；未知高度返回 nil，绝不由 Watch 估算。
  public static func decodeState(_ data: Data) throws -> DeskState {
    let bytes = [UInt8](data)
    guard bytes.count == 8 else {
      throw DeskProtocolError.invalidLength(expected: "8", actual: bytes.count)
    }
    guard bytes[0] == 1 else {
      throw DeskProtocolError.unsupportedVersion(bytes[0])
    }
    guard let motion = DeskMotion(rawValue: bytes[1]) else {
      throw DeskProtocolError.unknownMotion(bytes[1])
    }

    let flags = bytes[2]
    let rawHeight = uint16LE(bytes[4], bytes[5])
    let heightKnown = flags & 0x01 != 0 && rawHeight != unknownHeight
    return DeskState(
      motion: motion,
      heightMillimeters: heightKnown ? rawHeight : nil,
      maximumHeightMillimeters: uint16LE(bytes[6], bytes[7]),
      heightIsSimulated: flags & 0x02 != 0,
      childLockEnabled: flags & 0x04 != 0,
      bluetoothControlAllowed: flags & 0x08 != 0,
      upwardMotionBlocked: flags & 0x10 != 0
    )
  }

  /// 解码 Config v1/v2；v1 缺少档位时使用协议规定的兼容默认值。
  public static func decodeConfiguration(_ data: Data) throws -> DeskConfiguration {
    let bytes = [UInt8](data)
    guard bytes.count == 4 || bytes.count == 8 else {
      throw DeskProtocolError.invalidLength(expected: "4 or 8", actual: bytes.count)
    }
    let version = bytes[0]
    guard version == 1 || version == 2 else {
      throw DeskProtocolError.unsupportedVersion(version)
    }
    guard (version == 1 && bytes.count == 4) || (version == 2 && bytes.count == 8) else {
      throw DeskProtocolError.invalidLength(
        expected: version == 1 ? "4" : "8",
        actual: bytes.count
      )
    }

    let flags = bytes[1]
    let maximum = uint16LE(bytes[2], bytes[3])
    return DeskConfiguration(
      protocolVersion: version,
      childLockEnabled: flags & 0x01 != 0,
      bluetoothControlAllowed: flags & 0x04 != 0,
      maximumHeightMillimeters: maximum,
      sittingHeightMillimeters: version == 2 ? uint16LE(bytes[4], bytes[5]) : 640,
      standingHeightMillimeters: version == 2
        ? uint16LE(bytes[6], bytes[7])
        : min(1020, maximum)
    )
  }

  /// 协议整数统一采用 little-endian，避免两端各自猜测字节序。
  private static func uint16LE(_ low: UInt8, _ high: UInt8) -> UInt16 {
    UInt16(low) | (UInt16(high) << 8)
  }
}
