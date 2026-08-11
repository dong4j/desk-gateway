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
  @Published private(set) var errorMessage: String?

  private struct PendingWrite: Equatable {
    let command: DeskCommand
    let data: Data
  }

  private let serviceUUID = CBUUID(string: DeskProtocol.serviceUUID)
  private let commandUUID = CBUUID(string: DeskProtocol.commandUUID)
  private let stateUUID = CBUUID(string: DeskProtocol.stateUUID)
  private let configUUID = CBUUID(string: DeskProtocol.configUUID)

  private var centralManager: CBCentralManager!
  private var peripheral: CBPeripheral?
  private var commandCharacteristic: CBCharacteristic?
  private var pendingWrites: [PendingWrite] = []
  private var writeInFlight = false
  private var awaitingInitialStop = false
  private var connectionRequested = false

  override init() {
    super.init()
    // Main queue keeps CoreBluetooth delegate updates aligned with ObservableObject state.
    centralManager = CBCentralManager(delegate: self, queue: .main)
  }

  var isReady: Bool {
    phase == .ready
  }

  /// 用户进入页面或点击重连时开始扫描；Bluetooth 状态未知时等待系统回调。
  func connect() {
    connectionRequested = true
    guard centralManager.state == .poweredOn else {
      phase =
        centralManager.state == .unsupported || centralManager.state == .unauthorized
        ? .bluetoothUnavailable
        : .idle
      return
    }
    startScanning()
  }

  /// 命令严格串行；STOP 清除尚未发送的运动续期，并排到当前 Write 之后的第一位。
  func send(_ command: DeskCommand) {
    guard peripheral != nil, commandCharacteristic != nil else {
      errorMessage = "DeskGateway 尚未连接"
      return
    }

    let write = PendingWrite(command: command, data: DeskProtocol.encode(command))
    if command == .stop {
      pendingWrites.removeAll()
      pendingWrites.insert(write, at: 0)
    } else if pendingWrites.last?.command != command {
      // 高频 Crown 事件不能把相同 HOLD 堆进 GATT 队列。
      pendingWrites.append(write)
    }
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
      let peripheral,
      let commandCharacteristic
    else {
      return
    }

    let write = pendingWrites.removeFirst()
    writeInFlight = true
    peripheral.writeValue(write.data, for: commandCharacteristic, type: .withResponse)
  }

  /// Service 过滤已经足够精确，名称校验只用于避免错误广播配置混入。
  private func startScanning() {
    guard !centralManager.isScanning else {
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
    pendingWrites.removeAll()
    writeInFlight = false
    awaitingInitialStop = false
    deskState = nil
    configuration = nil
    errorMessage = nil
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
    fail(error ?? NSError(domain: "DeskGatewayWatch", code: 1))
  }

  func centralManager(
    _ central: CBCentralManager,
    didDisconnectPeripheral peripheral: CBPeripheral,
    error: Error?
  ) {
    resetConnectionState()
    phase = error == nil ? .disconnected : .failed(error!.localizedDescription)
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
      [commandUUID, stateUUID, configUUID],
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
      default:
        break
      }
    }

    guard commandCharacteristic != nil else {
      fail(NSError(domain: "DeskGatewayWatch", code: 3))
      return
    }
    // 加密 STOP 是最安全的首个 Write；成功回调同时证明配对和命令通道可用。
    phase = .pairing
    awaitingInitialStop = true
    send(.stop)
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
      fail(error)
      return
    }
    if awaitingInitialStop && characteristic.uuid == commandUUID {
      awaitingInitialStop = false
      phase = .ready
    }
    errorMessage = nil
    flushWriteQueue()
  }
}
