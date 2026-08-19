/** Desk Gateway Swift 协议与 TypeScript 固定字节语义的回归测试。 */

import Foundation
import Testing

@testable import DeskGatewayWatchCore

@Test("State v1 decodes height, motion and safety flags")
func decodesStateV1() throws {
  let state = try DeskProtocol.decodeState(
    Data([
      0x01, 0x01, 0xF9, 0x00, 0xD0, 0x02, 0xAC, 0x03,
    ]))

  #expect(state.motion == .movingUp)
  #expect(state.heightMillimeters == 720)
  #expect(state.maximumHeightMillimeters == 940)
  #expect(state.childLockEnabled == false)
  #expect(state.bluetoothControlAllowed)
  #expect(state.upwardMotionBlocked)
  #expect(state.controllerResetSupported)
  #expect(state.controllerResetActive)
  #expect(state.controllerResetRecommended)
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
      0x02, 0x0F, 0xAC, 0x03, 0x30, 0x02, 0x66, 0x03,
    ]))

  #expect(config.maximumHeightMillimeters == 940)
  #expect(config.minimumHeightMillimeters == 550)
  #expect(config.sittingHeightMillimeters == 560)
  #expect(config.standingHeightMillimeters == 870)
  #expect(config.bluetoothControlAllowed)
}

@Test("Config v3 exposes the configurable preset floor")
func decodesConfigV3() throws {
  let config = try DeskProtocol.decodeConfiguration(
    Data([
      0x03, 0x0F, 0xAC, 0x03, 0x26, 0x02, 0x66, 0x03, 0x26, 0x02,
    ]))

  #expect(config.minimumHeightMillimeters == 550)
  #expect(config.maximumHeightMillimeters == 940)
  #expect(config.sittingHeightMillimeters == 550)
  #expect(config.standingHeightMillimeters == 870)
}

@Test("Commands retain the frozen one-byte values")
func encodesCommands() {
  #expect(DeskProtocol.encode(DeskCommand.stop) == Data([0x00]))
  #expect(DeskProtocol.encode(DeskCommand.holdUp) == Data([0x01]))
  #expect(DeskProtocol.encode(DeskCommand.holdDown) == Data([0x02]))
  #expect(DeskProtocol.encode(DeskCommand.preset1) == Data([0x11]))
  #expect(DeskProtocol.encode(DeskCommand.preset4) == Data([0x14]))
  #expect(DeskProtocol.encode(DeskSystemCommand.resetController) == Data([0x02]))
}

@Test("Reminder v1 decodes ESP timer and audio state")
func decodesReminderV1() throws {
  let reminder = try DeskProtocol.decodeReminder(
    Data([
      0x01, 0x01, 0x00, 0x00, 0x17, 72, 25, 5, 15, 4,
      0xDB, 0x05, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 12, 0x00,
    ]))

  #expect(reminder.state == .running)
  #expect(reminder.phase == .focus)
  #expect(reminder.remainingSeconds == 1_499)
  #expect(reminder.completedFocusCount == 7)
  #expect(reminder.autoCycle)
  #expect(reminder.autoAdvanceSeconds == 12)
  #expect(reminder.audioEnabled)
  #expect(reminder.volumePercent == 72)
  #expect(DeskProtocol.encode(ReminderAction.pause) == Data([0x02]))
  #expect(DeskProtocol.encode(ReminderAction.startAuto) == Data([0x07]))
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
