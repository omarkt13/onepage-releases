import OnePageKit
import SwiftUI

/// Main screen: connection, battery, storage, firmware, update available.
struct DeviceView: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        let device = model.device
        let status = device.status
        List {
            Section {
                LinkBadge(state: device.state)
                if !device.state.isConnected {
                    NotConnectedNote(state: device.state)
                    if !device.isPaired {
                        NavigationLink("Pair a device") { PairAgainView() }
                    }
                }
            }

            Section("Battery") {
                LabeledContent("Level", value: status.battery.map { "\($0)%" } ?? "Unknown")
                LabeledContent("Charging", value: status.charging?.label ?? "Unknown")
                if let level = status.battery {
                    ProgressView(value: Double(level), total: 100)
                        .accessibilityLabel("Battery \(level) percent")
                }
            }

            Section("Storage") {
                if let free = status.storageFreeKiB, let total = status.storageTotalKiB, total > 0 {
                    let used = total - min(free, total)
                    LabeledContent("Used", value: "\(Self.mb(used)) of \(Self.mb(total))")
                    ProgressView(value: Double(used), total: Double(total))
                        .accessibilityLabel("Storage \(Int(Double(used) / Double(total) * 100)) percent used")
                } else {
                    Text("Unknown").foregroundStyle(.secondary)
                }
                LabeledContent("Books", value: "\(device.deviceBooks.count)")
            }

            Section("Calendar and weather") {
                LabeledContent("Calendar synced", value: LocalSeconds.describe(status.calendarSyncedAt))
                if let sent = model.weather.lastSentAt {
                    LabeledContent("Weather sent", value: sent.shortRelative)
                }
            }

            Section("Firmware") {
                LabeledContent("Version", value: status.firmware ?? "Unknown")
                if let m = model.updater.available {
                    NavigationLink {
                        UpdateView()
                    } label: {
                        Label("Update available: \(m.version)", systemImage: "arrow.down.circle.fill")
                            .foregroundStyle(.tint)
                    }
                }
            }
        }
        .navigationTitle("OnePage Mini")
        .refreshable {
            if device.state.isConnected { await device.refreshAll() }
        }
    }

    static func mb(_ kib: UInt32) -> String {
        let f = ByteCountFormatter()
        f.countStyle = .file
        return f.string(fromByteCount: Int64(kib) * 1024)
    }
}

/// Pairing after set-up, for example after a pairing reset.
struct PairAgainView: View {
    var body: some View {
        ScrollView {
            FindDeviceView(onSkip: {})
                .padding()
        }
        .navigationTitle("Pair a device")
    }
}
