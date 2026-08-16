/**
 Apple Watch 番茄时钟精简页。

 页面不运行本地 Timer，数字完全取自 ESP Reminder Notify；离开页面、重连或后台恢复
 后不会自行补算剩余时间。
 */

import DeskGatewayWatchCore
import SwiftUI
import WatchKit

struct PomodoroView<Controller: DeskControlling>: View {
  @ObservedObject var desk: Controller

  var body: some View {
    VStack(spacing: 8) {
      if let reminder = desk.reminder, reminder.available {
        // 状态与倒计时组成独立头部，并避开返回按钮和系统时间所在的顶部区域。
        VStack(spacing: 4) {
          Text(phaseLabel(reminder))
            .font(.caption)
            .foregroundStyle(.orange)

          Text(format(seconds: reminder.remainingSeconds))
            .font(.system(size: 38, weight: .medium, design: .rounded))
            .monospacedDigit()
            .minimumScaleFactor(0.8)
        }
        .padding(.top, 8)

        Text("已完成 \(reminder.completedFocusCount) 次")
          .font(.caption2)
          .foregroundStyle(.secondary)

        Button(primary(reminder).label) {
          desk.perform(primary(reminder).action)
          WKInterfaceDevice.current().play(.click)
        }
        .buttonStyle(.borderedProminent)
        .tint(.orange)
        .disabled(!desk.isReady)

        HStack(spacing: 6) {
          if let secondary = secondary(reminder) {
            Button(secondary.label) {
              desk.perform(secondary.action)
            }
            .buttonStyle(.bordered)
          }
          if reminder.state != .idle && primary(reminder).action != .stop {
            Button("结束") {
              desk.perform(.stop)
            }
            .buttonStyle(.bordered)
            .tint(.red)
          }
        }
        .font(.caption)
        .disabled(!desk.isReady)

        Label(
          reminder.audioEnabled ? "语音已开启" : "语音已关闭",
          systemImage: reminder.audioEnabled ? "speaker.wave.2.fill" : "speaker.slash.fill"
        )
        .font(.caption2)
        .foregroundStyle(.secondary)
      } else {
        ContentUnavailableView(
          "番茄时钟不可用",
          systemImage: "timer",
          description: Text("请连接支持 Reminder v1 的 Desk Gateway")
        )
      }
    }
    .padding(.horizontal, 8)
  }

  private func primary(
    _ reminder: ReminderSnapshot
  ) -> (action: ReminderAction, label: String) {
    switch reminder.state {
    case .idle:
      return (.startFocus, "开始专注")
    case .running:
      return (.pause, "暂停")
    case .paused:
      return (.resume, "继续")
    case .waiting:
      return reminder.phase == .focus
        ? (.startFocus, "开始专注")
        : (.startBreak, "开始休息")
    case .snoozed:
      return (.stop, "结束稍后提醒")
    }
  }

  private func secondary(
    _ reminder: ReminderSnapshot
  ) -> (action: ReminderAction, label: String)? {
    switch reminder.state {
    case .running, .paused:
      return (.skip, "跳过")
    case .waiting:
      return (.snooze, "稍后")
    default:
      return nil
    }
  }

  private func phaseLabel(_ reminder: ReminderSnapshot) -> String {
    if reminder.state == .idle { return "准备开始" }
    switch reminder.phase {
    case .focus: return "专注"
    case .shortBreak: return "短休息"
    case .longBreak: return "长休息"
    }
  }

  private func format(seconds: UInt32) -> String {
    String(format: "%02d:%02d", seconds / 60, seconds % 60)
  }
}
