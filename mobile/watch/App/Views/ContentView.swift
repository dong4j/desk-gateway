/**
 已确认 Apple Watch 原型的 SwiftUI 实现。

 待机时展示两个固定高度按钮，运动时用红色 STOP 替换；Crown 只在连接、童锁和
 Bluetooth 来源权限允许时产生运动命令。
 */

import DeskGatewayWatchCore
import SwiftUI
import WatchKit

/// 单页高频控制界面，不引入需要精细滚动的设置层级。
struct ContentView<Controller: DeskControlling>: View {
  @ObservedObject var desk: Controller
  @ObservedObject var crown: CrownMotionCoordinator

  @Environment(\.accessibilityReduceMotion) private var reduceMotion
  @Environment(\.scenePhase) private var scenePhase
  @State private var arrowDriftActive = false
  @State private var crownPosition = 0.0

  private var controlsEnabled: Bool {
    desk.isReady
      && desk.deskState != nil
      && desk.deskState?.childLockEnabled == false
      && desk.deskState?.bluetoothControlAllowed == true
  }

  private var effectiveDirection: CrownDirection? {
    if let localDirection = crown.activeDirection {
      return localDirection
    }
    guard let motion = desk.deskState?.motion else {
      return nil
    }
    switch motion {
    case .movingUp:
      return CrownDirection.up
    case .movingDown:
      return CrownDirection.down
    default:
      return nil
    }
  }

  private var isMoving: Bool {
    crown.activeDirection != nil
      || (desk.deskState?.motion != .idle
        && desk.deskState?.motion != .error
        && desk.deskState != nil)
  }

  var body: some View {
    VStack(spacing: 0) {
      heightDisplay

      motionStatusStage

      controlStage

      if let restriction = restrictionText {
        Text(restriction)
          .font(.caption2)
          .foregroundStyle(.orange)
          .lineLimit(1)
      }
    }
    .padding(.horizontal, 8)
    .toolbar {
      ToolbarItem(placement: .topBarLeading) {
        connectionLabel
      }
    }
    .focusable(controlsEnabled)
    .digitalCrownRotation(
      $crownPosition,
      from: -1_000,
      through: 1_000,
      by: 0.1,
      sensitivity: .high,
      isContinuous: true,
      isHapticFeedbackEnabled: true
    )
    .onChange(of: crownPosition) { _, newValue in
      crown.consume(position: newValue, controlsEnabled: controlsEnabled)
    }
    .onChange(of: controlsEnabled) { _, enabled in
      if !enabled {
        crown.forceStop(sendEvenIfIdle: false)
      }
    }
    .onChange(of: desk.isReady) { _, ready in
      if ready {
        WKInterfaceDevice.current().play(.success)
      }
    }
    .onChange(of: scenePhase) { _, phase in
      if phase != .active {
        crown.forceStop(sendEvenIfIdle: true)
      }
    }
    .onAppear {
      crown.prime(position: crownPosition)
      desk.connect()
    }
    .onDisappear {
      crown.forceStop(sendEvenIfIdle: true)
    }
  }

  /// 箭头、数字和单位共用一行；固定箭头槽位避免运动状态切换时数字左右跳动。
  private var heightDisplay: some View {
    HStack(alignment: .center, spacing: 6) {
      ZStack {
        if let direction = effectiveDirection {
          Image(systemName: direction == .up ? "arrow.up" : "arrow.down")
            .font(.system(size: 22, weight: .semibold))
            .foregroundStyle(direction == .up ? .cyan : .orange)
            .offset(y: arrowOffset(for: direction))
            .opacity(arrowOpacity)
            .id(direction)
            .transition(directionTransition(for: direction))
            .onAppear {
              startArrowAnimation(for: direction)
            }
            .onDisappear {
              arrowDriftActive = false
            }
            .accessibilityHidden(true)
        }
      }
      .frame(width: 22, height: 30)

      HStack(alignment: .lastTextBaseline, spacing: 4) {
        Text(heightText)
          .font(.system(size: 52, weight: .medium, design: .rounded))
          .monospacedDigit()
          .lineLimit(1)
          .minimumScaleFactor(0.75)

        Text("cm")
          .font(.caption2)
          .foregroundStyle(.secondary)
      }
    }
  }

  /// 固定文字行并提供上下对称留白；待机只隐藏内容，不能折叠并推动按钮。
  private var motionStatusStage: some View {
    Text(motionText)
      .font(.caption)
      .foregroundStyle(effectiveDirection == .down ? .orange : .cyan)
      .opacity(isMoving ? 1 : 0)
      .accessibilityHidden(!isMoving)
      .animation(.easeOut(duration: reduceMotion ? 0.1 : 0.16), value: isMoving)
      .frame(maxWidth: .infinity, minHeight: 18, maxHeight: 18)
      .padding(.vertical, 8)
  }

  /// 两套控制始终叠放在同一中心，只改变可见性和点击能力，不触发布局切换。
  private var controlStage: some View {
    ZStack {
      presetControls
        .opacity(isMoving ? 0 : 1)
        .allowsHitTesting(!isMoving)
        .accessibilityHidden(isMoving)

      movingControls
        .opacity(isMoving ? 1 : 0)
        .allowsHitTesting(isMoving)
        .accessibilityHidden(!isMoving)
    }
    .frame(maxWidth: .infinity, minHeight: 52, maxHeight: 52)
    .animation(.easeOut(duration: reduceMotion ? 0.1 : 0.18), value: isMoving)
  }

  /// 连接状态使用系统顶部栏，与右侧系统时间处于同一行。
  private var connectionLabel: some View {
    HStack(spacing: 4) {
      Circle()
        .fill(desk.isReady ? Color.green : Color.gray)
        .frame(width: 6, height: 6)
      Text(desk.phase.label)
        .font(.caption2)
        .lineLimit(1)

      if desk.isMock {
        Text("模拟")
          .font(.system(size: 8, weight: .semibold))
          .foregroundStyle(.black)
          .padding(.horizontal, 4)
          .padding(.vertical, 1)
          .background(.orange, in: Capsule())
      }

      if desk.phase == .disconnected || isFailure {
        Button("重连") {
          desk.reconnect()
        }
        .font(.caption2)
        .buttonStyle(.plain)
      }
    }
    .accessibilityElement(children: .combine)
  }

  /// 运动态只提供整行红色 STOP；运动文案由上方固定状态槽承载。
  private var movingControls: some View {
    Button {
      crown.forceStop(sendEvenIfIdle: true)
    } label: {
      Text("停止")
        .font(.headline)
        .frame(maxWidth: .infinity, minHeight: 34)
    }
    .buttonStyle(.borderedProminent)
    .tint(.red)
    .frame(maxWidth: .infinity)
    .frame(height: 44)
    .accessibilityHint("立即停止桌面运动")
  }

  /// 普通模式沿真实方向漂移；Reduce Motion 下只做透明度呼吸。
  private func arrowOffset(for direction: CrownDirection) -> CGFloat {
    guard !reduceMotion, arrowDriftActive else {
      return 0
    }
    return direction == .up ? -5 : 5
  }

  private var arrowOpacity: Double {
    guard arrowDriftActive else {
      return 1
    }
    return reduceMotion ? 0.6 : 0.35
  }

  /// 下一帧再启动循环，确保箭头每次出现或切换方向都从静止位置开始。
  private func startArrowAnimation(for direction: CrownDirection) {
    arrowDriftActive = false
    DispatchQueue.main.async {
      // 方向可能在下一帧前切换或停止，旧动画不能重新激活已离场的箭头。
      guard effectiveDirection == direction else {
        return
      }
      withAnimation(
        reduceMotion
          ? .easeInOut(duration: 0.7).repeatForever(autoreverses: true)
          : .linear(duration: 0.7).repeatForever(autoreverses: false)
      ) {
        arrowDriftActive = true
      }
    }
  }

  /// 箭头结束时继续沿当前方向离场，形成运动收束而不是原地消失。
  private func directionTransition(for direction: CrownDirection) -> AnyTransition {
    let insertion = AnyTransition.opacity
      .combined(with: .scale(scale: 0.9))
      .animation(.easeOut(duration: 0.16))
    guard !reduceMotion else {
      return .asymmetric(insertion: insertion, removal: .opacity)
    }
    let removal = AnyTransition.opacity
      .combined(with: .offset(y: direction == .up ? -5 : 5))
      .combined(with: .scale(scale: 0.85))
      .animation(.easeOut(duration: 0.18))
    return .asymmetric(insertion: insertion, removal: removal)
  }

  /// 待机态固定高度使用设备 Config 回读值，旧固件才回退到 64/102 cm。
  private var presetControls: some View {
    HStack(spacing: 6) {
      presetButton(
        title: "请坐",
        height: desk.configuration?.sittingHeightMillimeters ?? 640,
        tint: .gray,
        command: .preset1
      )
      presetButton(
        title: "站立",
        height: desk.configuration?.standingHeightMillimeters ?? 1020,
        tint: .cyan,
        command: .preset4
      )
    }
  }

  /// 两个按钮共享同一触摸规格，避免小屏上出现难以命中的次级文字区域。
  private func presetButton(
    title: String,
    height: UInt16,
    tint: Color,
    command: DeskCommand
  ) -> some View {
    Button {
      desk.send(command)
      WKInterfaceDevice.current().play(.click)
    } label: {
      VStack(spacing: 1) {
        Text(title)
          .font(.caption)
        Text(format(height: height))
          .font(.caption2)
          .monospacedDigit()
      }
      .frame(maxWidth: .infinity, minHeight: 34)
    }
    .buttonStyle(.bordered)
    .tint(tint)
    .frame(height: 44)
    .disabled(!controlsEnabled)
    .accessibilityLabel("\(title)，\(format(height: height))")
  }

  private var heightText: String {
    guard let height = desk.deskState?.heightMillimeters else {
      return "--.-"
    }
    return String(format: "%.1f", Double(height) / 10)
  }

  private var motionText: String {
    switch effectiveDirection {
    case .up: "上升中"
    case .down: "下降中"
    case nil: "移动中"
    }
  }

  private var restrictionText: String? {
    if desk.deskState?.childLockEnabled == true {
      return "童锁已开启"
    }
    if desk.deskState?.bluetoothControlAllowed == false {
      return "蓝牙控制已关闭"
    }
    return desk.errorMessage
  }

  private var isFailure: Bool {
    if case .failed = desk.phase {
      return true
    }
    return false
  }

  /// mm 转 cm；整数档位不显示无意义的小数，保留确认原型的紧凑排版。
  private func format(height: UInt16) -> String {
    let centimeters = Double(height) / 10
    return centimeters.rounded() == centimeters
      ? "\(Int(centimeters)) cm"
      : String(format: "%.1f cm", centimeters)
  }
}
