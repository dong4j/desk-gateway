/**
 Apple Watch 到 Desk Gateway 的 CoreBluetooth Central。

 本类只实现 GATT 连接、严格状态解码和串行 Write。运动安全的 Crown 时间语义位于
 `CrownMotionCoordinator`；固件 `desk_core` 仍是童锁、来源权限和高度上限的最终边界。
 */

import Combine
// Central 明确运行在 main queue；preconcurrency 兼容 CoreBluetooth 尚未标注的 delegate。
@preconcurrency import CoreBluetooth
import DeskGatewayWatchCore
import Foundation

/// 负责单个 DeskGateway 的扫描、配对、Notify 和命令写入。
@MainActor
final class DeskBLECentral: NSObject, ObservableObject, DeskControlling {
  @Published private(set) var phase: DeskConnectionPhase = .idle
  @Published private(set) var deskState: DeskState?
  @Published private(set) var configuration: DeskConfiguration?
  @Published private(set) var reminder: ReminderSnapshot?
  @Published private(set) var errorMessage: String?
  @Published private(set) var needsPairingRecovery = false

  private struct PendingWrite {
    let deskCommand: DeskCommand?
    let characteristic: CBCharacteristic
    let data: Data
  }

  private let serviceUUID = CBUUID(string: DeskProtocol.serviceUUID)
  private let commandUUID = CBUUID(string: DeskProtocol.commandUUID)
  private let stateUUID = CBUUID(string: DeskProtocol.stateUUID)
  private let configUUID = CBUUID(string: DeskProtocol.configUUID)
  private let systemUUID = CBUUID(string: DeskProtocol.systemUUID)
  private let clientInfoUUID = CBUUID(string: DeskProtocol.clientInfoUUID)
  private let reminderUUID = CBUUID(string: DeskProtocol.reminderUUID)

  private var centralManager: CBCentralManager!
  private var peripheral: CBPeripheral?
  private var commandCharacteristic: CBCharacteristic?
  private var systemCharacteristic: CBCharacteristic?
  private var clientInfoCharacteristic: CBCharacteristic?
  private var reminderCharacteristic: CBCharacteristic?
  private var pendingWrites: [PendingWrite] = []
  private var writeInFlight = false
  private var awaitingClientInfo = false
  private var connectionRequested = false

  let transport: DeskTransport? = .ble

  override init() {
    super.init()
    // Main queue keeps CoreBluetooth delegate updates aligned with ObservableObject state.
    centralManager = CBCentralManager(delegate: self, queue: .main)
  }

  var isReady: Bool {
    phase == .ready
  }

  var controlAllowed: Bool {
    deskState?.bluetoothControlAllowed == true
  }

  /// App 请求连接时开始扫描；已有连接流程必须保持幂等，不能因页面切换清空 GATT 状态。
  func connect() {
    connectionRequested = true
    guard centralManager.state == .poweredOn else {
      phase =
        centralManager.state == .unsupported || centralManager.state == .unauthorized
        ? .bluetoothUnavailable
        : .idle
      return
    }
    switch phase {
    case .scanning, .connecting, .pairing, .ready:
      return
    default:
      break
    }
    startScanning()
  }

  /// 显式切换通道时停止扫描或取消连接；不会重放先前的运动命令。
  func disconnect() {
    connectionRequested = false
    centralManager.stopScan()
    if let peripheral {
      centralManager.cancelPeripheralConnection(peripheral)
    }
    resetConnectionState()
    phase = .disconnected
  }

  /// 命令严格串行；STOP 清除尚未发送的运动续期，并排到当前 Write 之后的第一位。
  func send(_ command: DeskCommand) {
    guard peripheral != nil, let commandCharacteristic else {
      errorMessage = "DeskGateway 尚未连接"
      return
    }

    let write = PendingWrite(
      deskCommand: command,
      characteristic: commandCharacteristic,
      data: DeskProtocol.encode(command)
    )
    if command == .stop {
      pendingWrites.removeAll()
      pendingWrites.insert(write, at: 0)
    } else if pendingWrites.last?.deskCommand != command {
      // 高频 Crown 事件不能把相同 HOLD 堆进 GATT 队列。
      pendingWrites.append(write)
    }
    flushWriteQueue()
  }

  /// 番茄动作和桌体运动共用串行 GATT 队列，但写入独立 Reminder 特征。
  func perform(_ action: ReminderAction) {
    guard peripheral != nil, let reminderCharacteristic else {
      errorMessage = "当前固件不支持番茄时钟"
      return
    }
    pendingWrites.append(PendingWrite(
      deskCommand: nil,
      characteristic: reminderCharacteristic,
      data: DeskProtocol.encode(action)
    ))
    flushWriteQueue()
  }

  /// B12 建议出现后由用户确认触发；复用固件已有 System Characteristic，不绕过来源权限。
  func resetController() {
    guard peripheral != nil, let commandCharacteristic, let systemCharacteristic else {
      errorMessage = "当前固件不支持控制盒重置"
      return
    }
    // B12 恢复必须先清掉残留运动续期并发送 STOP，随后才允许进入 8 秒重置序列。
    pendingWrites.removeAll()
    pendingWrites.append(PendingWrite(
      deskCommand: .stop,
      characteristic: commandCharacteristic,
      data: DeskProtocol.encode(DeskCommand.stop)
    ))
    pendingWrites.append(PendingWrite(
      deskCommand: nil,
      characteristic: systemCharacteristic,
      data: DeskProtocol.encode(DeskSystemCommand.resetController)
    ))
    flushWriteQueue()
  }

  /// 清除旧连接状态并重新扫描，不重放任何先前运动命令。
  func reconnect() {
    if let peripheral {
      centralManager.cancelPeripheralConnection(peripheral)
    }
    resetConnectionState()
    connect()
  }

  /// 仅在 Characteristic 可写且没有在途 Write 时发送下一条命令。
  private func flushWriteQueue() {
    guard !writeInFlight,
      !pendingWrites.isEmpty,
      let peripheral
    else {
      return
    }

    let write = pendingWrites.removeFirst()
    writeInFlight = true
    peripheral.writeValue(write.data, for: write.characteristic, type: .withResponse)
  }

  /// Service 过滤已经足够精确，名称校验只用于避免错误广播配置混入。
  private func startScanning() {
    // CoreBluetooth 已持有 Peripheral 时不能重置本地引用，否则会留下无法管理的活动连接。
    guard !centralManager.isScanning, peripheral == nil else {
      return
    }
    resetConnectionState(keepPhase: true)
    phase = .scanning
    centralManager.scanForPeripherals(
      withServices: [serviceUUID],
      options: [CBCentralManagerScanOptionAllowDuplicatesKey: false]
    )
  }

  /// 断连后清除所有本地队列；固件断连 STOP 和 HOLD 租约继续提供最终保护。
  private func resetConnectionState(keepPhase: Bool = false) {
    centralManager?.stopScan()
    peripheral = nil
    commandCharacteristic = nil
    systemCharacteristic = nil
    clientInfoCharacteristic = nil
    reminderCharacteristic = nil
    pendingWrites.removeAll()
    writeInFlight = false
    awaitingClientInfo = false
    deskState = nil
    configuration = nil
    reminder = nil
    errorMessage = nil
    needsPairingRecovery = false
    if !keepPhase {
      phase = .idle
    }
  }

  /// 协议错误可诊断但不能用旧状态继续伪装实时控制。
  private func fail(_ error: Error) {
    errorMessage = error.localizedDescription
    phase = .failed(error.localizedDescription)
  }
}

// MARK: - CBCentralManagerDelegate

extension DeskBLECentral: @preconcurrency CBCentralManagerDelegate {
  func centralManagerDidUpdateState(_ central: CBCentralManager) {
    guard central.state == .poweredOn else {
      central.stopScan()
      phase = .bluetoothUnavailable
      return
    }
    if connectionRequested {
      startScanning()
    } else {
      phase = .idle
    }
  }

  func centralManager(
    _ central: CBCentralManager,
    didDiscover peripheral: CBPeripheral,
    advertisementData: [String: Any],
    rssi _: NSNumber
  ) {
    let advertisedName = advertisementData[CBAdvertisementDataLocalNameKey] as? String
    guard advertisedName == nil || advertisedName == DeskProtocol.advertisingName else {
      return
    }

    central.stopScan()
    self.peripheral = peripheral
    peripheral.delegate = self
    phase = .connecting
    central.connect(peripheral)
  }

  func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
    errorMessage = nil
    peripheral.discoverServices([serviceUUID])
  }

  func centralManager(
    _ central: CBCentralManager,
    didFailToConnect peripheral: CBPeripheral,
    error: Error?
  ) {
    needsPairingRecovery = true
    fail(error ?? NSError(domain: "DeskGatewayWatch", code: 1))
  }

  func centralManager(
    _ central: CBCentralManager,
    didDisconnectPeripheral peripheral: CBPeripheral,
    error: Error?
  ) {
    let wasReady = phase == .ready
    resetConnectionState()
    if let error {
      needsPairingRecovery = !wasReady
      let message = needsPairingRecovery
        ? "蓝牙配对信息可能已失效"
        : error.localizedDescription
      errorMessage = message
      phase = .failed(message)
    } else {
      phase = .disconnected
    }
  }
}

// MARK: - CBPeripheralDelegate

extension DeskBLECentral: @preconcurrency CBPeripheralDelegate {
  func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
    if let error {
      fail(error)
      return
    }
    guard let service = peripheral.services?.first(where: { $0.uuid == serviceUUID }) else {
      fail(NSError(domain: "DeskGatewayWatch", code: 2))
      return
    }
    peripheral.discoverCharacteristics(
      [commandUUID, stateUUID, configUUID, systemUUID, clientInfoUUID, reminderUUID],
      for: service
    )
  }

  func peripheral(
    _ peripheral: CBPeripheral,
    didDiscoverCharacteristicsFor service: CBService,
    error: Error?
  ) {
    if let error {
      fail(error)
      return
    }

    for characteristic in service.characteristics ?? [] {
      switch characteristic.uuid {
      case commandUUID:
        commandCharacteristic = characteristic
      case stateUUID:
        peripheral.setNotifyValue(true, for: characteristic)
        peripheral.readValue(for: characteristic)
      case configUUID:
        peripheral.setNotifyValue(true, for: characteristic)
        peripheral.readValue(for: characteristic)
      case systemUUID:
        systemCharacteristic = characteristic
      case clientInfoUUID:
        clientInfoCharacteristic = characteristic
      case reminderUUID:
        reminderCharacteristic = characteristic
        peripheral.setNotifyValue(true, for: characteristic)
        peripheral.readValue(for: characteristic)
      default:
        break
      }
    }

    guard commandCharacteristic != nil else {
      fail(NSError(domain: "DeskGatewayWatch", code: 3))
      return
    }
    if let clientInfoCharacteristic {
      // Client Info 触发加密配对，但不会像旧 STOP 握手一样中断其他客户端的运动。
      phase = .pairing
      awaitingClientInfo = true
      writeInFlight = true
      peripheral.writeValue(
        DeskProtocol.encodeWatchClientInfo(),
        for: clientInfoCharacteristic,
        type: .withResponse
      )
    } else {
      // Client Info 是可选扩展；旧固件继续使用原 Command / State，不发送 STOP 握手。
      phase = .ready
    }
  }

  func peripheral(
    _ peripheral: CBPeripheral,
    didUpdateValueFor characteristic: CBCharacteristic,
    error: Error?
  ) {
    if let error {
      // Config 是向后兼容扩展；读取失败不能阻断 STOP 和基础控制。
      if characteristic.uuid == configUUID {
        configuration = nil
        return
      }
      if characteristic.uuid == reminderUUID {
        reminder = nil
        return
      }
      fail(error)
      return
    }
    guard let data = characteristic.value else {
      return
    }

    do {
      switch characteristic.uuid {
      case stateUUID:
        deskState = try DeskProtocol.decodeState(data)
      case configUUID:
        configuration = try DeskProtocol.decodeConfiguration(data)
      case reminderUUID:
        reminder = try DeskProtocol.decodeReminder(data)
      default:
        break
      }
      errorMessage = nil
    } catch {
      fail(error)
    }
  }

  func peripheral(
    _ peripheral: CBPeripheral,
    didWriteValueFor characteristic: CBCharacteristic,
    error: Error?
  ) {
    writeInFlight = false
    if let error {
      let cocoaError = error as NSError
      if characteristic.uuid == commandUUID,
        DeskProtocol.isDeskBusyError(
          code: cocoaError.code,
          description: cocoaError.localizedDescription
        )
      {
        pendingWrites.removeAll(where: { $0.deskCommand != .stop })
        errorMessage = "另一台设备正在控制"
        phase = .ready
        flushWriteQueue()
        return
      }
      if awaitingClientInfo && characteristic.uuid == clientInfoUUID {
        awaitingClientInfo = false
        needsPairingRecovery = true
        fail(
          NSError(
            domain: "DeskGatewayWatch",
            code: cocoaError.code,
            userInfo: [
              NSLocalizedDescriptionKey:
                "蓝牙配对信息可能已失效",
              NSUnderlyingErrorKey: error,
            ]
          )
        )
        return
      }
      if characteristic.uuid == reminderUUID {
        errorMessage = "当前番茄状态不接受该操作"
        phase = .ready
        flushWriteQueue()
        return
      }
      if characteristic.uuid == systemUUID {
        errorMessage = "控制盒重置失败，请确认桌面空闲后重试"
        phase = .ready
        flushWriteQueue()
        return
      }
      fail(error)
      return
    }
    if awaitingClientInfo && characteristic.uuid == clientInfoUUID {
      awaitingClientInfo = false
      phase = .ready
    }
    errorMessage = nil
    flushWriteQueue()
  }
}
