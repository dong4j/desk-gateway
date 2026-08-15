/** Desk Gateway Watch REST 路径和状态映射测试。 */

import Foundation
import Testing

@testable import DeskGatewayWatchCore

@Test("Crown commands use bounded Jog endpoints")
func crownCommandsUseBoundedJogEndpoints() {
  #expect(DeskRESTProtocol.endpoint(for: .holdUp) == "/api/v1/desk/jog/up")
  #expect(DeskRESTProtocol.endpoint(for: .holdDown) == "/api/v1/desk/jog/down")
  #expect(DeskRESTProtocol.endpoint(for: .stop) == "/api/v1/desk/stop")
}

@Test("REST status projects motion safety configuration and reminder")
func restStatusProjectsDeviceState() throws {
  let data = Data(
    """
    {
      "status":"moving_up",
      "height_mm":721,
      "height_known":true,
      "height_sim":false,
      "child_lock":false,
      "upward_blocked":false,
      "controller_reset_supported":true,
      "controller_reset_active":false,
      "controller_reset_recommended":true,
      "min_height_mm":550,
      "max_height_mm":940,
      "preset1_height_mm":560,
      "preset4_height_mm":880,
      "control_sources":{"rest":true,"bluetooth":false},
      "reminder":{
        "available":true,"state":"running","phase":"focus",
        "remaining_sec":1499,"completed_focus_count":3,
        "focus_minutes":25,"short_break_minutes":5,
        "long_break_minutes":15,"focuses_per_long_break":4
      },
      "audio":{"available":true,"enabled":true,"playing":false,"volume_percent":60}
    }
    """.utf8
  )

  let projection = try DeskRESTProtocol.decodeStatus(data)

  #expect(projection.state.motion == .movingUp)
  #expect(projection.state.heightMillimeters == 721)
  #expect(projection.state.bluetoothControlAllowed == false)
  #expect(projection.state.controllerResetRecommended == true)
  #expect(projection.configuration.sittingHeightMillimeters == 560)
  #expect(projection.configuration.standingHeightMillimeters == 880)
  #expect(projection.restControlAllowed == true)
  #expect(projection.reminder?.remainingSeconds == 1499)
  #expect(projection.reminder?.audioEnabled == true)
}

@Test("Unknown REST motion fails closed")
func unknownRestMotionFailsClosed() {
  let data = Data("{\"status\":\"mystery\"}".utf8)
  #expect(throws: DeskRESTProtocolError.invalidStatus("mystery")) {
    try DeskRESTProtocol.decodeStatus(data)
  }
}
