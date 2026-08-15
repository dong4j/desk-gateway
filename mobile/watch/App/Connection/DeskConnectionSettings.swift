/**
 Watch 连接偏好与 REST 密码存储。

 连接模式和主机地址不是秘密，可保存在 UserDefaults；REST 密码只进入系统 Keychain，
 不能写入偏好、日志或仓库。Watch-only App 必须能独立完成这项配置。
 */

import Combine
import Foundation
import Security

enum DeskConnectionSettingsError: Error, LocalizedError {
  case missingHost
  case missingPassword
  case keychain(OSStatus)

  var errorDescription: String? {
    switch self {
    case .missingHost: "请输入网关地址"
    case .missingPassword: "请输入 REST 密码"
    case .keychain(let status): "无法保存 REST 密码（\(status)）"
    }
  }
}

/// App 生命周期共享的连接设置；保存成功后由控制器显式重连应用。
@MainActor
final class DeskConnectionSettings: ObservableObject {
  @Published private(set) var mode: DeskConnectionMode
  @Published private(set) var host: String
  @Published private(set) var hasPassword: Bool

  private static let modeKey = "desk.connection.mode"
  private static let hostKey = "desk.connection.host"
  private static let defaultHost = "desk-gateway.local"
  private static let keychainService = "com.dong4j.deskgateway.watch.rest"
  private static let keychainAccount = "rest-password"

  init(defaults: UserDefaults = .standard) {
    let storedMode = defaults.string(forKey: Self.modeKey)
    mode = DeskConnectionMode(rawValue: storedMode ?? "") ?? .automatic
    let storedHost = defaults.string(forKey: Self.hostKey)?
      .trimmingCharacters(in: .whitespacesAndNewlines)
    host = storedHost.flatMap { $0.isEmpty ? nil : $0 } ?? Self.defaultHost
    hasPassword = (try? Self.loadPassword())?.isEmpty == false
  }

  /// 自动模式没有密码时仍可使用 BLE；Wi-Fi 模式则由 UI 在保存时强制补齐。
  func restConfiguration() -> (host: String, password: String)? {
    guard let password = try? Self.loadPassword(), !password.isEmpty else {
      return nil
    }
    return (host, password)
  }

  /// 空密码表示保留已存密码，避免每次修改模式都要求重新输入。
  func update(mode: DeskConnectionMode, host: String, password: String) throws {
    let normalizedHost = host.trimmingCharacters(in: .whitespacesAndNewlines)
    if mode != .ble, normalizedHost.isEmpty {
      throw DeskConnectionSettingsError.missingHost
    }
    if mode == .wifi, password.isEmpty, !hasPassword {
      throw DeskConnectionSettingsError.missingPassword
    }
    if !password.isEmpty {
      try Self.savePassword(password)
      hasPassword = true
    }

    self.mode = mode
    self.host = normalizedHost.isEmpty ? Self.defaultHost : normalizedHost
    UserDefaults.standard.set(mode.rawValue, forKey: Self.modeKey)
    UserDefaults.standard.set(self.host, forKey: Self.hostKey)
  }

  private static func loadPassword() throws -> String? {
    var query = baseKeychainQuery()
    query[kSecReturnData as String] = true
    query[kSecMatchLimit as String] = kSecMatchLimitOne
    var result: CFTypeRef?
    let status = SecItemCopyMatching(query as CFDictionary, &result)
    if status == errSecItemNotFound { return nil }
    guard status == errSecSuccess, let data = result as? Data else {
      throw DeskConnectionSettingsError.keychain(status)
    }
    return String(data: data, encoding: .utf8)
  }

  private static func savePassword(_ password: String) throws {
    let data = Data(password.utf8)
    let query = baseKeychainQuery()
    let updateStatus = SecItemUpdate(
      query as CFDictionary,
      [kSecValueData as String: data] as CFDictionary
    )
    if updateStatus == errSecSuccess { return }
    guard updateStatus == errSecItemNotFound else {
      throw DeskConnectionSettingsError.keychain(updateStatus)
    }

    var addition = query
    addition[kSecValueData as String] = data
    addition[kSecAttrAccessible as String] = kSecAttrAccessibleWhenUnlockedThisDeviceOnly
    let addStatus = SecItemAdd(addition as CFDictionary, nil)
    guard addStatus == errSecSuccess else {
      throw DeskConnectionSettingsError.keychain(addStatus)
    }
  }

  private static func baseKeychainQuery() -> [String: Any] {
    [
      kSecClass as String: kSecClassGenericPassword,
      kSecAttrService as String: keychainService,
      kSecAttrAccount as String: keychainAccount,
    ]
  }
}
