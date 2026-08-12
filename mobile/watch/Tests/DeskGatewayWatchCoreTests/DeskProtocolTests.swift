/** Desk Gateway Swift 协议与 TypeScript 固定字节语义的回归测试。 */

import Foundation
import Testing

@testable import DeskGatewayWatchCore

@Test("State v1 decodes height, motion and safety flags")
func decodesStateV1() throws {
  let state = try DeskProtocol.decodeState(
    Data([
      0x01, 0x01, 0x19, 0x00, 0xD0, 0x02, 0xFC, 0x03,
    ]))

  #expect(state.motion == .movingUp)
  #expect(state.heightMillimeters == 720)
  #expect(state.maximumHeightMillimeters == 1020)
  #expect(state.childLockEnabled == false)
  #expect(state.bluetoothControlAllowed)
  #expect(state.upwardMotionBlocked)
}

@Test("Unknown height remains unknown")
func preservesUnknownHeight() throws {
  let state = try DeskProtocol.decodeState(
    Data([
      0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFC, 0x03,
    ]))

  #expect(state.heightMillimeters == nil)
}

@Test("Config v2 exposes sitting and standing presets")
func decodesConfigV2() throws {
  let config = try DeskProtocol.decodeConfiguration(
    Data([
      0x02, 0x0F, 0xFC, 0x03, 0x80, 0x02, 0xFC, 0x03,
    ]))

  #expect(config.sittingHeightMillimeters == 640)
  #expect(config.standingHeightMillimeters == 1020)
  #expect(config.bluetoothControlAllowed)
}

@Test("Commands retain the frozen one-byte values")
func encodesCommands() {
  #expect(DeskProtocol.encode(.stop) == Data([0x00]))
  #expect(DeskProtocol.encode(.holdUp) == Data([0x01]))
  #expect(DeskProtocol.encode(.holdDown) == Data([0x02]))
  #expect(DeskProtocol.encode(.preset1) == Data([0x11]))
  #expect(DeskProtocol.encode(.preset4) == Data([0x14]))
}

@Test("Watch Client Info uses version 1 and watchOS kind")
func encodesWatchClientInfo() {
  #expect(DeskProtocol.encodeWatchClientInfo() == Data([0x01, 0x01]))
}

@Test("Desk Busy recognizes the stable ATT application error")
func recognizesDeskBusy() {
  #expect(DeskProtocol.isDeskBusyError(code: 128, description: "Write failed"))
  #expect(DeskProtocol.isDeskBusyError(code: 7, description: "ATT error 0x80"))
  #expect(!DeskProtocol.isDeskBusyError(code: 7, description: "Write not permitted"))
}

@Test("Malformed packets fail closed")
func rejectsMalformedState() {
  #expect(throws: DeskProtocolError.invalidLength(expected: "8", actual: 3)) {
    try DeskProtocol.decodeState(Data([0x01, 0x00, 0x00]))
  }
}
