/** REST 运动请求排序与续期合并测试。 */

import Testing

@testable import DeskGatewayWatchCore

@Test("STOP clears stale renewals and precedes reversed direction")
func stopPrecedesReversedDirection() {
  var queue = DeskRESTMotionQueue()

  #expect(queue.submit(.holdUp) == .holdUp)
  #expect(queue.submit(.holdUp) == nil)
  #expect(queue.submit(.stop) == nil)
  #expect(queue.submit(.holdDown) == nil)
  #expect(queue.complete() == .stop)
  #expect(queue.complete() == .holdDown)
  #expect(queue.complete() == nil)
}

@Test("Slow network coalesces repeated Jog renewals")
func repeatedJogRenewalsAreCoalesced() {
  var queue = DeskRESTMotionQueue()

  #expect(queue.submit(.holdUp) == .holdUp)
  #expect(queue.submit(.holdUp) == nil)
  #expect(queue.submit(.holdUp) == nil)
  #expect(queue.complete() == .holdUp)
  #expect(queue.complete() == nil)
}
