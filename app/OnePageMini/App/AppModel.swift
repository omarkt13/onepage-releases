import Foundation
import Observation
import OnePageKit
import SwiftUI

/// Wires the services together and answers the device's data requests.
@Observable
final class AppModel {
    let device: DeviceManager
    let calendar: CalendarService
    let weather: WeatherProvider
    let library: BookLibrary
    let updater = FirmwareUpdater()

    var onboardingDone: Bool = Prefs.onboardingDone {
        didSet { Prefs.onboardingDone = onboardingDone }
    }

    /// A file shared to the app before it was ready to import it.
    var pendingImport: URL?

    init() {
        let device = DeviceManager()
        self.device = device
        calendar = CalendarService(device: device)
        weather = WeatherProvider(device: device)
        library = BookLibrary(device: device)
        updater.device = device

        device.onReady = { [weak self] in
            Task { @MainActor in await self?.deviceBecameReady() }
        }
        device.onDataRequest = { [weak self] request in
            Task { @MainActor in await self?.handle(request) }
        }
        device.onStatusChange = { status in
            LocalAlerts.checkBattery(status)
        }

        // After set-up, start Bluetooth at launch so iOS can restore the
        // link and wake the app in the background.
        if Prefs.onboardingDone || Prefs.pairedPeripheralID != nil {
            device.activate()
        }
    }

    // MARK: Events

    @MainActor
    private func deviceBecameReady() async {
        if calendar.isOn { await calendar.sync() }
        if weather.isOn { await weather.sendNow(foreground: UIApplication.shared.applicationState == .active) }
        library.sendPending()
        await checkFirmware(userAsked: false)
    }

    /// The device asks hourly for weather and every 6 hours for the calendar.
    /// iOS gives the app a few seconds in the background to answer.
    @MainActor
    private func handle(_ request: DataRequest) async {
        let activity = BackgroundActivity(name: "OnePageDataRequest")
        defer { activity.end() }
        let foreground = UIApplication.shared.applicationState == .active
        if request.wantsWeather, weather.isOn { await weather.sendNow(foreground: foreground) }
        if request.wantsCalendar, calendar.isOn { await calendar.sync() }
        library.sendPending()
        await checkFirmware(userAsked: false)
    }

    @MainActor
    func checkFirmware(userAsked: Bool) async {
        let found = await updater.check(currentVersion: device.status.firmware,
                                        hardwareRevision: device.status.hardwareRevision,
                                        userAsked: userAsked)
        if let found, !userAsked { LocalAlerts.updateAvailable(version: found.version) }
    }

    @MainActor
    func appBecameActive() async {
        guard device.state.isConnected else { return }
        if calendar.isOn { await calendar.sync() }
        if weather.isOn { await weather.sendNow(foreground: true) }
        await checkFirmware(userAsked: false)
    }

    // MARK: Links and files

    /// Handles an NFC tap link, or a book shared from another app.
    @MainActor
    func open(_ url: URL) async {
        if let id = PairingLink.deviceID(from: url) {
            // A tap on a device that already has this phone only opens the app.
            if device.isPaired, Prefs.pairedDeviceID?.uppercased() == id { return }
            device.pairingTarget = id
            if onboardingDone && !device.isPaired {
                device.activate()
                device.startScan()
            }
            // Otherwise onboarding picks the target up on the "find device" page.
            return
        }
        if url.isFileURL {
            if onboardingDone {
                await library.importFile(at: url)
            } else {
                pendingImport = url
            }
        }
    }

    @MainActor
    func finishOnboarding() async {
        onboardingDone = true
        if let url = pendingImport {
            pendingImport = nil
            await library.importFile(at: url)
        }
    }
}
