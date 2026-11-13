import CoreBluetooth
import Foundation
import Observation
import OnePageKit
import UIKit

enum LinkState: Equatable {
    case notStarted
    case bluetoothOff
    case unauthorized
    case unsupported
    case idle
    case scanning
    case connecting
    case waitingForDevice
    case discovering
    case ready

    var label: String {
        switch self {
        case .notStarted: return "Not set up"
        case .bluetoothOff: return "Bluetooth is off"
        case .unauthorized: return "Bluetooth not allowed"
        case .unsupported: return "Bluetooth not available"
        case .idle: return "Not connected"
        case .scanning: return "Searching"
        case .connecting: return "Connecting"
        case .waitingForDevice: return "Waiting for the device"
        case .discovering: return "Connecting"
        case .ready: return "Connected"
        }
    }

    var isConnected: Bool { self == .ready }
}

struct DiscoveredDevice: Identifiable {
    let peripheral: CBPeripheral
    var info: AdvertInfo
    var rssi: Int
    var id: UUID { peripheral.identifier }
    var name: String { "OnePage Mini \(info.deviceIDString.suffix(4))" }
}

/// Owns the CoreBluetooth central and the link to one OnePage Mini.
///
/// All CoreBluetooth callbacks arrive on the main queue, and the async
/// methods are main-actor isolated, so every property is touched on the
/// main thread only.
@Observable
final class DeviceManager: NSObject {
    static let restoreIdentifier = "app.onepage.mini.central"

    static let serviceUUID = CBUUID(string: OnePageUUID.service)
    static let statusUUID = CBUUID(string: OnePageUUID.status)
    static let settingsUUID = CBUUID(string: OnePageUUID.settings)
    static let appFilterUUID = CBUUID(string: OnePageUUID.appFilter)
    static let homeDataUUID = CBUUID(string: OnePageUUID.homeData)
    static let calendarUUID = CBUUID(string: OnePageUUID.calendarSync)
    static let dataRequestUUID = CBUUID(string: OnePageUUID.dataRequest)
    static let privacyUUID = CBUUID(string: OnePageUUID.privacy)
    static let transferControlUUID = CBUUID(string: OnePageUUID.transferControl)
    static let transferDataUUID = CBUUID(string: OnePageUUID.transferData)

    static let notifyUUIDs: [CBUUID] = [statusUUID, appFilterUUID, calendarUUID, dataRequestUUID, transferControlUUID]

    // MARK: Observable state

    private(set) var state: LinkState = .notStarted
    private(set) var discovered: [DiscoveredDevice] = []
    private(set) var status = DeviceStatus()
    private(set) var settings = DeviceSettings.defaults
    private(set) var privacy = PrivacySettings(showCalendarWhenAway: true)
    private(set) var appFilter: [AppFilterEntry] = []
    private(set) var deviceBooks: [DeviceBook] = []
    private(set) var pairedPeripheralID: UUID? = Prefs.pairedPeripheralID
    /// Device identifier from an NFC tap, waiting to be found.
    var pairingTarget: String?
    var lastError: UserFacingError?

    var isPaired: Bool { pairedPeripheralID != nil }

    // MARK: Hooks for the services

    @ObservationIgnored var onReady: (() -> Void)?
    @ObservationIgnored var onDataRequest: ((DataRequest) -> Void)?
    @ObservationIgnored var onStatusChange: ((DeviceStatus) -> Void)?
    let calendarAcks = Mailbox<CalendarAck>()
    let transferReplies = Mailbox<TransferReply>()
    let filterResults = Mailbox<UInt8>()

    // MARK: CoreBluetooth

    @ObservationIgnored private var central: CBCentralManager?
    @ObservationIgnored private(set) var peripheral: CBPeripheral?
    @ObservationIgnored private var characteristics: [CBUUID: CBCharacteristic] = [:]
    @ObservationIgnored private var pendingNotifySetups = 0
    @ObservationIgnored private var notifySetupFailed = false
    @ObservationIgnored private var ops: [PendingOp] = []
    @ObservationIgnored private var inFlight: PendingOp?
    @ObservationIgnored private var writeReadyWaiters: [CheckedContinuation<Void, Error>] = []
    @ObservationIgnored private var filterAccumulator: [AppFilterEntry] = []
    @ObservationIgnored private var bookAccumulator: [DeviceBook] = []

    private struct PendingOp {
        enum Kind { case read, write }
        let id = UUID()
        let kind: Kind
        let uuid: CBUUID
        let bytes: [UInt8]
        let continuation: CheckedContinuation<[UInt8], Error>
    }

    override init() {
        super.init()
    }

    /// Creates the central. The first call shows the iOS Bluetooth prompt,
    /// so the app calls it only after it explains why. With a restore
    /// identifier, iOS relaunches the app for Bluetooth events after a restart.
    func activate() {
        guard central == nil else { return }
        central = CBCentralManager(
            delegate: self,
            queue: .main,
            options: [
                CBCentralManagerOptionRestoreIdentifierKey: Self.restoreIdentifier,
                CBCentralManagerOptionShowPowerAlertKey: true,
            ]
        )
    }

    var isActivated: Bool { central != nil }

    /// Largest value for one write without response: ATT MTU − 3.
    var maxWriteLength: Int {
        peripheral?.maximumWriteValueLength(for: .withoutResponse) ?? 20
    }

    // MARK: Scanning and connecting

    func startScan() {
        guard let central, central.state == .poweredOn else { return }
        discovered.removeAll()
        central.scanForPeripherals(withServices: [Self.serviceUUID],
                                   options: [CBCentralManagerScanOptionAllowDuplicatesKey: true])
        if state != .ready { state = .scanning }
    }

    func stopScan() {
        central?.stopScan()
        if state == .scanning { state = .idle }
    }

    func connect(_ device: DiscoveredDevice) {
        guard let central else { return }
        central.stopScan()
        peripheral = device.peripheral
        device.peripheral.delegate = self
        Prefs.pairedDeviceID = device.info.deviceIDString
        state = .connecting
        central.connect(device.peripheral, options: nil)
    }

    /// Forgets the device in the app. iOS keeps its own bond until the user
    /// forgets the device in Settings > Bluetooth; the Help screen says so.
    func forgetDevice() {
        if let p = peripheral { central?.cancelPeripheralConnection(p) }
        peripheral = nil
        characteristics.removeAll()
        pairedPeripheralID = nil
        Prefs.clearPairing()
        status = DeviceStatus()
        appFilter = []
        deviceBooks = []
        state = .idle
    }

    private func reconnectKnown() {
        guard let central, central.state == .poweredOn else { return }
        if let p = peripheral {
            if p.state == .connected {
                didConnect(p)
            } else {
                state = .waitingForDevice
                central.connect(p, options: nil)
            }
            return
        }
        guard let id = pairedPeripheralID else {
            state = .idle
            if pairingTarget != nil { startScan() }
            return
        }
        if let p = central.retrievePeripherals(withIdentifiers: [id]).first {
            peripheral = p
            p.delegate = self
            if p.state == .connected {
                didConnect(p)
            } else {
                // A pending connection never times out. iOS completes it when
                // the device comes back in range, with no user action.
                state = .waitingForDevice
                central.connect(p, options: nil)
            }
        } else if let p = central.retrieveConnectedPeripherals(withServices: [Self.serviceUUID]).first {
            peripheral = p
            p.delegate = self
            central.connect(p, options: nil)
        } else {
            state = .waitingForDevice
        }
    }

    private func didConnect(_ p: CBPeripheral) {
        state = .discovering
        characteristics.removeAll()
        p.delegate = self
        p.discoverServices([Self.serviceUUID])
    }

    // MARK: GATT operations

    @MainActor
    func read(_ uuid: CBUUID) async throws -> [UInt8] {
        try await enqueue(.read, uuid, [])
    }

    @MainActor
    func write(_ bytes: [UInt8], to uuid: CBUUID) async throws {
        _ = try await enqueue(.write, uuid, bytes)
    }

    /// Write without response. Waits while the iOS send buffer is full.
    @MainActor
    func send(_ bytes: [UInt8], to uuid: CBUUID) async throws {
        guard let p = peripheral, p.state == .connected else { throw LinkError.notConnected }
        guard let ch = characteristics[uuid] else { throw LinkError.notReady }
        while !p.canSendWriteWithoutResponse {
            try await withCheckedThrowingContinuation { (c: CheckedContinuation<Void, Error>) in
                writeReadyWaiters.append(c)
            }
            guard p.state == .connected else { throw LinkError.notConnected }
        }
        p.writeValue(Data(bytes), for: ch, type: .withoutResponse)
    }

    @MainActor
    private func enqueue(_ kind: PendingOp.Kind, _ uuid: CBUUID, _ bytes: [UInt8]) async throws -> [UInt8] {
        guard peripheral?.state == .connected else { throw LinkError.notConnected }
        return try await withCheckedThrowingContinuation { (c: CheckedContinuation<[UInt8], Error>) in
            ops.append(PendingOp(kind: kind, uuid: uuid, bytes: bytes, continuation: c))
            pump()
        }
    }

    private func pump() {
        guard inFlight == nil, !ops.isEmpty else { return }
        let op = ops.removeFirst()
        guard let p = peripheral, p.state == .connected else {
            op.continuation.resume(throwing: LinkError.notConnected)
            pump()
            return
        }
        guard let ch = characteristics[op.uuid] else {
            op.continuation.resume(throwing: LinkError.notReady)
            pump()
            return
        }
        inFlight = op
        switch op.kind {
        case .read: p.readValue(for: ch)
        case .write: p.writeValue(Data(op.bytes), for: ch, type: .withResponse)
        }
        let id = op.id
        // iOS may wait for the pairing prompt on the first encrypted access,
        // so allow time for the user to answer it.
        DispatchQueue.main.asyncAfter(deadline: .now() + 45) { [weak self] in
            guard let self, let current = self.inFlight, current.id == id else { return }
            self.inFlight = nil
            current.continuation.resume(throwing: LinkError.timeout)
            self.pump()
        }
    }

    private func finishInFlight(_ kind: PendingOp.Kind, uuid: CBUUID, value: [UInt8]?, error: Error?) -> Bool {
        guard let op = inFlight, op.kind == kind, op.uuid == uuid else { return false }
        inFlight = nil
        if let error {
            op.continuation.resume(throwing: Self.map(error))
        } else {
            op.continuation.resume(returning: value ?? [])
        }
        pump()
        return true
    }

    private static func map(_ error: Error) -> LinkError {
        let ns = error as NSError
        if ns.domain == CBATTErrorDomain {
            switch CBATTError.Code(rawValue: ns.code) {
            case .insufficientAuthentication?, .insufficientEncryption?:
                return .rejected("The phone and the device are not paired. Accept the pairing prompt, or forget the device in Settings > Bluetooth and pair again.")
            default: break
            }
        }
        return .gatt(ns.localizedDescription)
    }

    private func failAll(_ error: LinkError) {
        if let op = inFlight {
            inFlight = nil
            op.continuation.resume(throwing: error)
        }
        let pending = ops
        ops.removeAll()
        pending.forEach { $0.continuation.resume(throwing: error) }
        let waiters = writeReadyWaiters
        writeReadyWaiters.removeAll()
        waiters.forEach { $0.resume(throwing: error) }
        calendarAcks.fail(error)
        transferReplies.fail(error)
        filterResults.fail(error)
    }

    // MARK: Device commands

    /// Reads everything the app shows, after connection.
    @MainActor
    func refreshAll() async {
        do {
            status = try DeviceStatus(bytes: try await read(Self.statusUUID))
            onStatusChange?(status)
            var s = DeviceSettings.defaults
            s.merge(try DeviceSettings(bytes: try await read(Self.settingsUUID)))
            settings = s
            privacy = try PrivacySettings(bytes: try await read(Self.privacyUUID))
            try await requestFilterList()
            try await requestBookList()
        } catch {
            lastError = UserFacingError("Could not read the device", error)
        }
    }

    @MainActor
    func requestFilterList() async throws {
        filterAccumulator = []
        try await write(AppFilterCommand.list(start: 0).encode(), to: Self.appFilterUUID)
    }

    @MainActor
    func requestBookList() async throws {
        bookAccumulator = []
        try await write(TransferCommand.list(start: 0).encode(), to: Self.transferControlUUID)
    }

    @MainActor
    func updateSettings(_ change: DeviceSettings) async {
        do {
            try await write(change.encode(), to: Self.settingsUUID)
            settings.merge(change)
        } catch {
            lastError = UserFacingError("The setting did not change", error)
        }
    }

    @MainActor
    func setShowCalendarWhenAway(_ on: Bool) async {
        do {
            let p = PrivacySettings(showCalendarWhenAway: on)
            try await write(p.encode(), to: Self.privacyUUID)
            privacy = p
        } catch {
            lastError = UserFacingError("The privacy setting did not change", error)
        }
    }

    @MainActor
    func setFilter(_ entry: AppFilterEntry) async {
        do {
            filterResults.clear()
            try await write(AppFilterCommand.set(entry).encode(), to: Self.appFilterUUID)
            let result = try await filterResults.next(timeout: 5)
            guard result == 0 else {
                let code = DeviceErrorCode(rawValue: UInt16(result)) ?? .malformed
                throw LinkError.rejected(code.message)
            }
            if let i = appFilter.firstIndex(where: { $0.identifier == entry.identifier }) {
                appFilter[i] = entry
            } else {
                appFilter.append(entry)
            }
        } catch {
            lastError = UserFacingError("The app setting did not change", error)
        }
    }

    @MainActor
    func removeFilter(_ identifier: String) async {
        do {
            filterResults.clear()
            try await write(AppFilterCommand.remove(identifier: identifier).encode(), to: Self.appFilterUUID)
            _ = try await filterResults.next(timeout: 5)
            appFilter.removeAll { $0.identifier == identifier }
        } catch {
            lastError = UserFacingError("The app was not removed from the list", error)
        }
    }

    @MainActor
    func sendHomeData(_ data: HomeData) async throws {
        try await write(data.encode(), to: Self.homeDataUUID)
    }

    /// Erases the device and removes the pairing. The device restarts.
    @MainActor
    func eraseDevice() async throws {
        try await write(PrivacySettings.eraseCommand, to: Self.privacyUUID)
        forgetDevice()
    }

    /// Sends FAST on or off once. Calendar sync, book transfer and firmware
    /// update can overlap, so they use `acquireFastLink` and `releaseFastLink`
    /// instead: FAST off goes out only when the last user stops.
    @MainActor
    func setFastLink(_ on: Bool) async {
        try? await write(TransferCommand.fast(on).encode(), to: Self.transferControlUUID)
    }

    @ObservationIgnored private var fastLinkHolds = 0

    @MainActor
    func acquireFastLink() async {
        fastLinkHolds += 1
        await setFastLink(true)
    }

    @MainActor
    func releaseFastLink() async {
        fastLinkHolds = max(0, fastLinkHolds - 1)
        if fastLinkHolds == 0 {
            await setFastLink(false)
        }
    }

    // MARK: Notifications from the device

    private func handleNotification(_ uuid: CBUUID, _ bytes: [UInt8]) {
        do {
            switch uuid {
            case Self.statusUUID:
                let old = status
                status.merge(try DeviceStatus(bytes: bytes))
                if let code = status.lastError, code != 0, code != old.lastError,
                   let known = DeviceErrorCode(rawValue: code) {
                    lastError = UserFacingError(title: "The device reported a problem", message: known.message)
                }
                onStatusChange?(status)
            case Self.dataRequestUUID:
                onDataRequest?(try DataRequest(bytes: bytes))
            case Self.calendarUUID:
                calendarAcks.push(try CalendarAck(bytes: bytes))
            case Self.appFilterUUID:
                switch try AppFilterReply(bytes: bytes) {
                case let .list(total, start, entries):
                    if start == 0 { filterAccumulator = [] }
                    filterAccumulator += entries
                    let next = Int(start) + entries.count
                    if next < Int(total), !entries.isEmpty {
                        Task { @MainActor in
                            try? await self.write(AppFilterCommand.list(start: UInt16(next)).encode(), to: Self.appFilterUUID)
                        }
                    } else {
                        appFilter = filterAccumulator
                    }
                case let .setResult(r), let .removeResult(r):
                    filterResults.push(r)
                }
            case Self.transferControlUUID:
                let reply = try TransferReply(bytes: bytes)
                if case let .list(total, start, books) = reply {
                    if start == 0 { bookAccumulator = [] }
                    bookAccumulator += books
                    let next = Int(start) + books.count
                    if next < Int(total), !books.isEmpty {
                        Task { @MainActor in
                            try? await self.write(TransferCommand.list(start: UInt16(next)).encode(), to: Self.transferControlUUID)
                        }
                    } else {
                        deviceBooks = bookAccumulator
                    }
                } else {
                    transferReplies.push(reply)
                }
            default:
                break
            }
        } catch {
            // A message this app cannot read. Newer firmware may add replies;
            // ignore it rather than interrupt the user.
        }
    }
}

// MARK: - CBCentralManagerDelegate

extension DeviceManager: CBCentralManagerDelegate {
    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        switch central.state {
        case .poweredOn:
            reconnectKnown()
        case .poweredOff:
            state = .bluetoothOff
            failAll(.notConnected)
        case .unauthorized:
            state = .unauthorized
        case .unsupported:
            state = .unsupported
        default:
            state = .idle
        }
    }

    func centralManager(_ central: CBCentralManager, willRestoreState dict: [String: Any]) {
        if let restored = dict[CBCentralManagerRestoredStatePeripheralsKey] as? [CBPeripheral],
           let p = restored.first(where: { $0.identifier == pairedPeripheralID }) ?? restored.first {
            peripheral = p
            p.delegate = self
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        guard let mfr = advertisementData[CBAdvertisementDataManufacturerDataKey] as? Data,
              let info = AdvertInfo(manufacturerData: [UInt8](mfr)) else { return }
        let device = DiscoveredDevice(peripheral: peripheral, info: info, rssi: RSSI.intValue)
        if let i = discovered.firstIndex(where: { $0.id == device.id }) {
            discovered[i] = device
        } else {
            discovered.append(device)
        }
        // A device opened by an NFC tap connects without a list.
        if let target = pairingTarget, info.pairing, DeviceID.matches(target, info.deviceID), state == .scanning {
            pairingTarget = nil
            connect(device)
        }
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        didConnect(peripheral)
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        if peripheral.identifier == pairedPeripheralID {
            // Automatic reconnection, often in the background: retry with no alert.
            state = .waitingForDevice
            central.connect(peripheral, options: nil)
        } else {
            lastError = UserFacingError(title: "Could not connect",
                                        message: "The device did not accept the connection. Keep it near the phone. If you pair for the first time, start pairing mode on the device again.")
            state = .idle
        }
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        failAll(.notConnected)
        characteristics.removeAll()
        if peripheral.identifier == pairedPeripheralID {
            // Reconnect automatically when the device is back in range.
            state = .waitingForDevice
            central.connect(peripheral, options: nil)
        } else {
            state = .idle
        }
    }
}

// MARK: - CBPeripheralDelegate

extension DeviceManager: CBPeripheralDelegate {
    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error {
            // A link drop or a security error, not a wrong device.
            lastError = UserFacingError("Could not set up the device", error)
            abandonSetup(peripheral)
            return
        }
        guard let service = peripheral.services?.first(where: { $0.uuid == Self.serviceUUID }) else {
            lastError = UserFacingError(title: "This is not a OnePage Mini",
                                        message: "The device does not offer the OnePage service. Check that you picked the right device.")
            abandonSetup(peripheral)
            return
        }
        peripheral.discoverCharacteristics(nil, for: service)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        if let error {
            lastError = UserFacingError("Could not set up the device", error)
            abandonSetup(peripheral)
            return
        }
        for ch in service.characteristics ?? [] {
            characteristics[ch.uuid] = ch
        }
        pendingNotifySetups = 0
        notifySetupFailed = false
        for uuid in Self.notifyUUIDs {
            if let ch = characteristics[uuid] {
                pendingNotifySetups += 1
                // The first subscribe on an encrypted characteristic makes iOS
                // show its pairing prompt.
                peripheral.setNotifyValue(true, for: ch)
            }
        }
        if pendingNotifySetups == 0 { linkReady(peripheral) }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
        if let error, !notifySetupFailed {
            notifySetupFailed = true
            lastError = UserFacingError("Pairing did not finish", Self.map(error))
        }
        pendingNotifySetups -= 1
        guard pendingNotifySetups == 0 else { return }
        if notifySetupFailed {
            // The user declined the pairing prompt, or the bond is stale.
            // Do not record the device as paired.
            abandonSetup(peripheral)
        } else {
            linkReady(peripheral)
        }
    }

    /// Stops a connection that could not be set up. A device the phone is
    /// already paired with stays the reconnection target.
    private func abandonSetup(_ p: CBPeripheral) {
        characteristics.removeAll()
        if p.identifier != pairedPeripheralID {
            central?.cancelPeripheralConnection(p)
            if peripheral?.identifier == p.identifier { peripheral = nil }
            state = .idle
        } else {
            state = .waitingForDevice
        }
    }

    private func linkReady(_ p: CBPeripheral) {
        state = .ready
        if pairedPeripheralID != p.identifier {
            pairedPeripheralID = p.identifier
            Prefs.pairedPeripheralID = p.identifier
        }
        Task { @MainActor in
            await self.refreshAll()
            self.onReady?()
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        let bytes = characteristic.value.map { [UInt8]($0) } ?? []
        if finishInFlight(.read, uuid: characteristic.uuid, value: bytes, error: error) {
            // A read reply. Status reads also update the model in refreshAll.
            return
        }
        guard error == nil else { return }
        handleNotification(characteristic.uuid, bytes)
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        _ = finishInFlight(.write, uuid: characteristic.uuid, value: nil, error: error)
    }

    func peripheralIsReady(toSendWriteWithoutResponse peripheral: CBPeripheral) {
        let waiters = writeReadyWaiters
        writeReadyWaiters.removeAll()
        waiters.forEach { $0.resume() }
    }
}
