/**
 Desk Gateway 独立 watchOS App 入口。

 BLE Central 与 Crown 协调器由 App 生命周期持有，避免 SwiftUI 页面重绘时重建连接或
 丢失尚未完成的安全 STOP。
 */

import SwiftUI

#if DEBUG && targetEnvironment(simulator)
  /// Debug Simulator 在编译期切换为 Mock；真机和 Release 无法引用该类型。
  private typealias ActiveDeskController = MockDeskController
#else
  private typealias ActiveDeskController = DeskBLECentral
#endif

@main
struct DeskGatewayWatchApp: App {
  @StateObject private var desk: ActiveDeskController
  @StateObject private var crown: CrownMotionCoordinator

  init() {
    let desk = ActiveDeskController()
    _desk = StateObject(wrappedValue: desk)
    _crown = StateObject(
      wrappedValue: CrownMotionCoordinator { [weak desk] command in
        desk?.send(command)
      }
    )
  }

  var body: some Scene {
    WindowGroup {
      // watchOS 只有在导航容器内才会呈现 topBarLeading ToolbarItem。
      NavigationStack {
        ContentView(desk: desk, crown: crown)
      }
    }
  }
}
