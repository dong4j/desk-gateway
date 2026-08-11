/** Digital Crown 方向、续期、切向和超时停止的确定性测试。 */

import Testing

@testable import DeskGatewayWatchCore

@Test("First sample establishes a baseline and positive motion starts upward")
func startsOnlyAfterDelta() {
  var engine = CrownMotionEngine()

  #expect(engine.consume(position: 0, at: 0).isEmpty)
  #expect(engine.consume(position: 0.1, at: 0.01) == [.start(.up)])
  #expect(engine.activeDirection == .up)
}

@Test("Continuous input renews at 300 ms without queueing every crown event")
func renewsAtBoundedRate() {
  var engine = CrownMotionEngine()
  _ = engine.consume(position: 0, at: 0)
  #expect(engine.consume(position: 0.1, at: 0.01) == [.start(.up)])
  #expect(engine.consume(position: 0.2, at: 0.20).isEmpty)
  #expect(engine.consume(position: 0.3, at: 0.32) == [.renew(.up)])
}

@Test("Direction reversal stops before starting the opposite direction")
func stopsBeforeDirectionChange() {
  var engine = CrownMotionEngine()
  _ = engine.consume(position: 0, at: 0)
  _ = engine.consume(position: 0.1, at: 0.01)

  #expect(engine.consume(position: 0.0, at: 0.02) == [.stop, .start(.down)])
  #expect(engine.activeDirection == .down)
}

@Test("Four hundred milliseconds without input emits stop")
func stopsAfterInactivity() {
  var engine = CrownMotionEngine()
  _ = engine.consume(position: 0, at: 0)
  _ = engine.consume(position: -0.1, at: 1.0)

  #expect(engine.tick(at: 1.39).isEmpty)
  #expect(engine.tick(at: 1.40) == [.stop])
  #expect(engine.activeDirection == nil)
}

@Test("Force stop is idempotent")
func forceStopIsIdempotent() {
  var engine = CrownMotionEngine()
  _ = engine.consume(position: 0, at: 0)
  _ = engine.consume(position: 0.1, at: 0.01)

  #expect(engine.forceStop() == [.stop])
  #expect(engine.forceStop().isEmpty)
}
