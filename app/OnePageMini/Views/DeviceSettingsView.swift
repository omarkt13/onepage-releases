import OnePageKit
import SwiftUI

/// Refresh mode, language, alert strength, pairing reset, device information.
struct DeviceSettingsView: View {
    @Environment(AppModel.self) private var model
    @State private var confirmForget = false

    var body: some View {
        let device = model.device
        let s = device.settings
        Form {
            if !device.state.isConnected {
                Section { NotConnectedNote(state: device.state) }
            }
            Section {
                Picker("Screen refresh", selection: Binding(
                    get: { s.refreshMode ?? .balanced },
                    set: { m in
                        var c = DeviceSettings()
                        c.refreshMode = m
                        Task { await device.updateSettings(c) }
                    })) {
                    ForEach(RefreshMode.allCases) { Text($0.label).tag($0) }
                }
            } footer: {
                Text("Quality: cleanest text, slower page turns. Fast: quickest page turns, with more ghosting between full refreshes.")
            }
            .disabled(!device.state.isConnected)
            Section {
                Picker("Alert strength", selection: Binding(
                    get: { s.alertStrength ?? .medium },
                    set: { a in
                        var c = DeviceSettings()
                        c.alertStrength = a
                        Task { await device.updateSettings(c) }
                    })) {
                    ForEach(AlertStrength.allCases) { Text($0.label).tag($0) }
                }
                Picker("Language", selection: Binding(
                    get: { s.language ?? "en" },
                    set: { l in
                        var c = DeviceSettings()
                        c.language = l
                        Task { await device.updateSettings(c) }
                    })) {
                    Text("English").tag("en")
                }
            } footer: {
                Text("English is the only device language in this version.")
            }
            .disabled(!device.state.isConnected)

            Section("Device") {
                LabeledContent("Firmware", value: device.status.firmware ?? "Unknown")
                LabeledContent("Hardware revision", value: device.status.hardwareRevision.map { "\($0)" } ?? "Unknown")
                LabeledContent("Device ID", value: Prefs.pairedDeviceID ?? "Unknown")
            }

            Section {
                Button("Pairing reset", role: .destructive) { confirmForget = true }
            } footer: {
                Text("Removes this device from the app. To pair again, also forget \"OnePage Mini\" in iOS Settings > Bluetooth, then start pairing mode on the device.")
            }
        }
        .navigationTitle("Device settings")
        .confirmationDialog("Remove the pairing?", isPresented: $confirmForget, titleVisibility: .visible) {
            Button("Remove pairing", role: .destructive) { device.forgetDevice() }
        } message: {
            Text("The device stops getting calendar, weather and books from this phone.")
        }
    }
}
