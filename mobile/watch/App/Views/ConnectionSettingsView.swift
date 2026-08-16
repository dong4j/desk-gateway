/** Watch 连接模式、网关地址和 REST 密码设置页。 */

import SwiftUI

struct ConnectionSettingsView: View {
  @ObservedObject var settings: DeskConnectionSettings
  let onSaved: () -> Void

  @Environment(\.dismiss) private var dismiss
  @State private var mode: DeskConnectionMode
  @State private var host: String
  @State private var password = ""
  @State private var errorMessage: String?

  init(settings: DeskConnectionSettings, onSaved: @escaping () -> Void) {
    self.settings = settings
    self.onSaved = onSaved
    _mode = State(initialValue: settings.mode)
    _host = State(initialValue: settings.host)
  }

  var body: some View {
    Form {
      Picker("连接方式", selection: $mode) {
        ForEach(DeskConnectionMode.allCases) { mode in
          Text(mode.label).tag(mode)
        }
      }

      if mode != .ble {
        TextField("网关地址", text: $host)
          .textInputAutocapitalization(.never)
          .autocorrectionDisabled()

        SecureField(settings.hasPassword ? "REST 密码（已保存）" : "REST 密码", text: $password)
      }

      if let errorMessage {
        Text(errorMessage)
          .font(.caption2)
          .foregroundStyle(.red)
      }

      Button {
        save()
      } label: {
        Text("保存并重连")
          .fontWeight(.semibold)
          .multilineTextAlignment(.center)
          .frame(maxWidth: .infinity, alignment: .center)
      }
      .frame(maxWidth: .infinity, alignment: .center)
      .buttonStyle(.borderedProminent)
      .tint(.orange)
      // Form 默认行底色会与胶囊按钮叠成双层背景，主操作只保留系统按钮形状。
      .listRowBackground(Color.clear)
      .listRowInsets(EdgeInsets(top: 4, leading: 0, bottom: 4, trailing: 0))
    }
    .navigationTitle("连接设置")
    .navigationBarTitleDisplayMode(.inline)
  }

  private func save() {
    do {
      try settings.update(mode: mode, host: host, password: password)
      errorMessage = nil
      onSaved()
      dismiss()
    } catch {
      errorMessage = error.localizedDescription
    }
  }
}
