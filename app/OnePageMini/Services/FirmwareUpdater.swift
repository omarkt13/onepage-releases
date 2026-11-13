import CryptoKit
import Foundation
import iOSMcuManagerLibrary
import Observation
import OnePageKit

/// Checks for device firmware, downloads and verifies it, and sends it with
/// Nordic's McuManager library over the SMP service. If an update fails,
/// MCUboot keeps the device on its old version.
@Observable
final class FirmwareUpdater: NSObject {
    // PLACEHOLDER: the version file location, until the update server exists.
    static let manifestURL = URL(string: "https://updates.onepage.app/mini/manifest.json")!
    // PLACEHOLDER: base64 of the 32-byte Ed25519 public key that signs manifests.
    // Empty means "not configured": the app refuses every update.
    static let manifestPublicKeyBase64 = ""

    enum Phase: Equatable {
        case idle
        case checking
        case upToDate
        case available(FirmwareManifest)
        case downloading
        case uploading(Double)
        case finishing
        case done(String)
        case failed(String)
    }

    private(set) var phase: Phase = .idle
    private(set) var available: FirmwareManifest?
    @ObservationIgnored private var dfu: FirmwareUpgradeManager?
    @ObservationIgnored private var transport: McuMgrBleTransport?
    @ObservationIgnored private var activity: BackgroundActivity?
    @ObservationIgnored private var targetVersion: String?
    /// The OnePage link, used to hold the shortest connection interval
    /// during the upload (firmware spec section 8).
    @ObservationIgnored weak var device: DeviceManager?
    @ObservationIgnored private var fastLinkTask: Task<Void, Never>?
    /// The device that holds a fast-link reference for this update.
    @ObservationIgnored private var fastLinkDevice: DeviceManager?

    var isBusy: Bool {
        switch phase {
        case .downloading, .uploading, .finishing: return true
        default: return false
        }
    }

    /// Checks the version file. Called once a day and when the user asks.
    @MainActor
    func check(currentVersion: String?, hardwareRevision: UInt8?, userAsked: Bool) async -> FirmwareManifest? {
        guard !isBusy else { return nil }
        if !userAsked, let last = Prefs.lastFirmwareCheck, Date().timeIntervalSince(last) < 24 * 3600 {
            return available
        }
        phase = .checking
        do {
            var request = URLRequest(url: Self.manifestURL)
            request.cachePolicy = .reloadIgnoringLocalCacheData
            let (data, response) = try await URLSession.shared.data(for: request)
            guard (response as? HTTPURLResponse)?.statusCode == 200 else { throw UpdateError.download("server reply") }
            let manifest = try FirmwareManifest.decode(data)
            try verifySignature(manifest)
            Prefs.lastFirmwareCheck = Date()
            if let current = currentVersion, SemVer.isNewer(manifest.version, than: current),
               manifest.supports(hardwareRevision: hardwareRevision) {
                available = manifest
                phase = .available(manifest)
                return manifest
            }
            available = nil
            phase = .upToDate
        } catch let e as UpdateError {
            phase = userAsked ? .failed(e.message) : .idle
        } catch {
            phase = userAsked ? .failed(UpdateError.download(error.localizedDescription).message) : .idle
        }
        return nil
    }

    private func verifySignature(_ m: FirmwareManifest) throws {
        guard let keyData = Data(base64Encoded: Self.manifestPublicKeyBase64), keyData.count == 32 else {
            throw UpdateError.notConfigured
        }
        guard let sig = Data(base64Encoded: m.signature) else { throw UpdateError.badSignature }
        let key = try Curve25519.Signing.PublicKey(rawRepresentation: keyData)
        guard key.isValidSignature(sig, for: Data(m.signedMessage)) else { throw UpdateError.badSignature }
    }

    /// Why the update cannot start now, or nil if it can.
    func blocker(status: DeviceStatus, linkReady: Bool) -> String? {
        if !linkReady { return LinkError.notConnected.message }
        let charging = status.charging == .charging || status.charging == .full
        if (status.battery ?? 0) < 30 && !charging { return UpdateError.battery.message }
        return nil
    }

    /// Downloads, checks size and SHA-256, and sends the image.
    @MainActor
    func start(_ manifest: FirmwareManifest, peripheralID: UUID) async {
        guard !isBusy else { return }
        phase = .downloading
        activity = BackgroundActivity(name: "OnePageFirmwareUpdate")
        do {
            let (tempURL, response) = try await URLSession.shared.download(from: manifest.url)
            guard (response as? HTTPURLResponse)?.statusCode == 200 else { throw UpdateError.download("server reply") }
            let data = try Data(contentsOf: tempURL)
            guard data.count == manifest.size else { throw UpdateError.badSize }
            let hash = SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
            guard hash == manifest.sha256.lowercased() else { throw UpdateError.badHash }

            let fileURL = FileManager.default.temporaryDirectory.appendingPathComponent("onepage-mini-\(manifest.version).bin")
            try? FileManager.default.removeItem(at: fileURL)
            try data.write(to: fileURL)
            let package = try McuMgrPackage(from: fileURL)

            let transport = McuMgrBleTransport(peripheralID)
            let manager = FirmwareUpgradeManager(transport: transport, delegate: self)
            self.transport = transport
            self.dfu = manager
            targetVersion = manifest.version
            phase = .uploading(0)
            holdFastLink()
            try manager.start(package: package)
        } catch let e as UpdateError {
            fail(e.message)
        } catch {
            fail(UpdateError.upload(error.localizedDescription).message)
        }
    }

    @MainActor
    func cancel() {
        dfu?.cancel()
    }

    @MainActor
    private func fail(_ message: String) {
        phase = .failed(message)
        cleanUp()
    }

    /// The device returns to its idle interval 30 s after the last FAST
    /// command, so repeat it every 10 s while the update runs.
    @MainActor
    private func holdFastLink() {
        guard fastLinkTask == nil, let held = device else { return }
        fastLinkTask = Task { @MainActor [weak self] in
            await held.acquireFastLink()
            while !Task.isCancelled {
                try? await Task.sleep(nanoseconds: 10_000_000_000)
                guard !Task.isCancelled, let device = self?.device else { return }
                await device.setFastLink(true) // keeps the device's 30 s timer running
            }
        }
        fastLinkDevice = held
    }

    @MainActor
    private func cleanUp() {
        if fastLinkTask != nil {
            fastLinkTask?.cancel()
            fastLinkTask = nil
            if let held = fastLinkDevice { Task { @MainActor in await held.releaseFastLink() } }
            fastLinkDevice = nil
        }
        transport?.close()
        transport = nil
        dfu = nil
        activity?.end()
        activity = nil
    }
}

extension FirmwareUpdater: FirmwareUpgradeDelegate {
    func upgradeDidStart(controller: FirmwareUpgradeController) {}

    func upgradeStateDidChange(from previousState: FirmwareUpgradeState, to newState: FirmwareUpgradeState) {
        Task { @MainActor in
            // After the upload, the device tests, restarts into and confirms the image.
            switch newState {
            case .test, .reset, .confirm: self.phase = .finishing
            default: break
            }
        }
    }

    func upgradeDidComplete() {
        Task { @MainActor in
            self.phase = .done("The device now runs version \(self.targetVersion ?? "new"). It restarted to finish.")
            self.available = nil
            self.cleanUp()
        }
    }

    func upgradeDidFail(inState state: FirmwareUpgradeState, with error: Error) {
        Task { @MainActor in
            self.fail(UpdateError.upload(error.localizedDescription).message)
        }
    }

    func upgradeDidCancel(state: FirmwareUpgradeState) {
        Task { @MainActor in
            self.fail("You stopped the update. The device stays on its old version.")
        }
    }

    func uploadProgressDidChange(bytesSent: Int, imageSize: Int, timestamp: Date) {
        Task { @MainActor in
            self.phase = .uploading(imageSize == 0 ? 0 : Double(bytesSent) / Double(imageSize))
        }
    }
}
