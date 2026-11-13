import OnePageKit
import SwiftUI

/// Version notes, progress and result of a device update.
struct UpdateView: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        let updater = model.updater
        let device = model.device
        Form {
            Section("Installed") {
                LabeledContent("Version", value: device.status.firmware ?? "Unknown")
            }

            switch updater.phase {
            case .idle, .upToDate:
                Section {
                    if updater.phase == .upToDate {
                        Label("The device is up to date.", systemImage: "checkmark.circle")
                    }
                    Button("Check for update") { Task { await model.checkFirmware(userAsked: true) } }
                }
            case .checking:
                Section { HStack { ProgressView(); Text("Checking…") } }
            case let .available(m):
                Section("Version \(m.version)") {
                    if let notes = m.notes { Text(notes) }
                    if let reason = updater.blocker(status: device.status, linkReady: device.state.isConnected) {
                        Text(reason).foregroundStyle(.red)
                    }
                    Button("Install") {
                        if let id = device.peripheral?.identifier {
                            Task { await updater.start(m, peripheralID: id) }
                        }
                    }
                    .buttonStyle(.borderedProminent)
                    .disabled(updater.blocker(status: device.status, linkReady: device.state.isConnected) != nil)
                }
                Section {
                    Text("Keep the phone near the device and the app open until the update finishes. It takes a few minutes.")
                }
            case .downloading:
                Section { HStack { ProgressView(); Text("Downloading…") } }
            case let .uploading(fraction):
                Section {
                    ProgressView(value: fraction) { Text("Sending to the device") }
                        .accessibilityValue("\(Int(fraction * 100)) percent")
                    Text("Keep the phone near the device.")
                    Button("Stop", role: .destructive) { updater.cancel() }
                }
            case .finishing:
                Section { HStack { ProgressView(); Text("The device is restarting to finish…") } }
            case let .done(message):
                Section { Label(message, systemImage: "checkmark.circle.fill").foregroundStyle(.green) }
            case let .failed(message):
                Section {
                    Label(message, systemImage: "xmark.octagon").foregroundStyle(.red)
                    Button("Check again") { Task { await model.checkFirmware(userAsked: true) } }
                }
            }
        }
        .navigationTitle("Device update")
    }
}
