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

@Test("Continuous input renews at 250 ms without queueing every crown event")
func renewsAtBoundedRate() {
  var engine = CrownMotionEngine()
  _ = engine.consume(position: 0, at: 0)
  #expect(engine.consume(position: 0.1, at: 0.01) == [.start(.up)])
  #expect(engine.consume(position: 0.2, at: 0.20).isEmpty)
  #expect(engine.consume(position: 0.3, at: 0.26) == [.renew(.up)])
}

@Test("Watchdog renews while real crown callbacks are sparse")
func watchdogRenewsWithoutNewInput() {
  var engine = CrownMotionEngine()
  _ = engine.consume(position: 0, at: 0)
  _ = engine.consume(position: 0.1, at: 1.0)

  #expect(engine.tick(at: 1.24).isEmpty)
  #expect(engine.tick(at: 1.25) == [.renew(.up)])
  #expect(engine.tick(at: 1.49).isEmpty)
  #expect(engine.activeDirection == .up)
}

@Test("Sparse crown input inside five hundred milliseconds keeps motion active")
func sparseCrownInputExtendsIntent() {
  var engine = CrownMotionEngine()
  _ = engine.consume(position: 0, at: 0)
  _ = engine.consume(position: 0.1, at: 1.0)

  #expect(engine.tick(at: 1.25) == [.renew(.up)])
  #expect(engine.consume(position: 0.2, at: 1.45).isEmpty)
  #expect(engine.tick(at: 1.50) == [.renew(.up)])
  #expect(engine.tick(at: 1.75) == [.renew(.up)])
  #expect(engine.tick(at: 1.94).isEmpty)
  #expect(engine.tick(at: 1.95) == [.stop])
}

@Test("Direction reversal stops before starting the opposite direction")
func stopsBeforeDirectionChange() {
  var engine = CrownMotionEngine()
  _ = engine.consume(position: 0, at: 0)
  _ = engine.consume(position: 0.1, at: 0.01)

  #expect(engine.consume(position: 0.0, at: 0.02) == [.stop, .start(.down)])
  #expect(engine.activeDirection == .down)
}

@Test("Five hundred milliseconds without input emits stop before renewal")
func stopsAfterInactivity() {
  var engine = CrownMotionEngine()
  _ = engine.consume(position: 0, at: 0)
  _ = engine.consume(position: -0.1, at: 1.0)

  #expect(engine.tick(at: 1.25) == [.renew(.down)])
  #expect(engine.tick(at: 1.49).isEmpty)
  #expect(engine.tick(at: 1.50) == [.stop])
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
