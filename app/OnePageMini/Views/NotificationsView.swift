import OnePageKit
import SwiftUI

/// Apps the device has seen, each Show, Silent or Hide. Quiet hours.
struct NotificationsView: View {
    @Environment(AppModel.self) private var model
    /// Apps the user already answered the "title only" suggestion for.
    @State private var answered: Set<String> = Prefs.suggestedTitleOnly

    var body: some View {
        let device = model.device
        let suggestions = device.appFilter.filter {
            $0.looksSensitive && !$0.titleOnly && !answered.contains($0.identifier)
        }
        List {
            if !device.state.isConnected {
                Section { NotConnectedNote(state: device.state) }
            }

            ForEach(suggestions) { entry in
                Section {
                    VStack(alignment: .leading, spacing: 8) {
                        Text("\(AppNames.displayName(for: entry.identifier)) looks like a bank or sign-in code app.")
                            .font(.headline)
                        Text("With \"title only\", the device shows that a notification arrived, without its text.")
                        HStack {
                            Button("Use title only") {
                                var e = entry
                                e.titleOnly = true
                                answer(entry.identifier)
                                Task { await device.setFilter(e) }
                            }
                            .buttonStyle(.borderedProminent)
                            Button("No") { answer(entry.identifier) }
                        }
                    }
                }
            }

            Section {
                if device.appFilter.isEmpty {
                    Text("No apps yet. iOS does not let the app list your apps. Each app appears here after it sends its first notification to the device.")
                        .foregroundStyle(.secondary)
                }
                ForEach(device.appFilter.sorted { AppNames.displayName(for: $0.identifier).localizedCaseInsensitiveCompare(AppNames.displayName(for: $1.identifier)) == .orderedAscending }) { entry in
                    NavigationLink {
                        AppFilterDetailView(entry: entry)
                    } label: {
                        AppRow(entry: entry)
                    }
                }
            } header: {
                Text("Apps")
            } footer: {
                Text("Show: on the device with an alert. Silent: on the device with no light or vibration. Hide: not on the device.")
            }

            QuietHoursSection()
        }
        .navigationTitle("Notifications")
        .refreshable { try? await device.requestFilterList() }
    }

    private func answer(_ identifier: String) {
        answered.insert(identifier)
        Prefs.suggestedTitleOnly = answered
    }
}

struct AppRow: View {
    let entry: AppFilterEntry

    var body: some View {
        HStack {
            VStack(alignment: .leading) {
                Text(AppNames.displayName(for: entry.identifier))
                if AppNames.isKnown(entry.identifier) {
                    Text(entry.identifier).font(.caption).foregroundStyle(.secondary)
                }
            }
            Spacer()
            if entry.titleOnly {
                Image(systemName: "eye.slash").accessibilityLabel("Title only")
            }
            Text(entry.mode.label).foregroundStyle(.secondary)
        }
        .accessibilityElement(children: .combine)
    }
}

struct AppFilterDetailView: View {
    @Environment(AppModel.self) private var model
    @Environment(\.dismiss) private var dismiss
    @State var entry: AppFilterEntry

    var body: some View {
        Form {
            Section {
                Picker("Show on device", selection: $entry.mode) {
                    ForEach(FilterMode.allCases) { Text($0.label).tag($0) }
                }
                .pickerStyle(.segmented)
                Text(entry.mode.explanation).font(.footnote).foregroundStyle(.secondary)
            }
            Section {
                Picker("Alert pattern", selection: $entry.pattern) {
                    ForEach(AlertPattern.allCases) { Text($0.label).tag($0) }
                }
                Toggle("Title only", isOn: $entry.titleOnly)
            } footer: {
                Text("Title only: the device shows the app name and time, without the text. Use it for banking and sign-in code apps.")
            }
            Section {
                Button("Remove from list", role: .destructive) {
                    Task {
                        await model.device.removeFilter(entry.identifier)
                        dismiss()
                    }
                }
            } footer: {
                Text("The app comes back with Show the next time it sends a notification.")
            }
        }
        .navigationTitle(AppNames.displayName(for: entry.identifier))
        .onChange(of: entry) { _, newValue in
            Task { await model.device.setFilter(newValue) }
        }
    }
}

struct QuietHoursSection: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        let device = model.device
        let s = device.settings
        Section {
            Toggle("Quiet hours", isOn: Binding(
                get: { s.quietHoursOn ?? false },
                set: { on in
                    var c = DeviceSettings()
                    c.quietHoursOn = on
                    Task { await device.updateSettings(c) }
                }))
            if s.quietHoursOn == true {
                DatePicker("From", selection: Binding(
                    get: { Minutes.date(s.quietStartMinutes ?? 1320) },
                    set: { d in
                        var c = DeviceSettings()
                        c.quietStartMinutes = Minutes.minutes(d)
                        Task { await device.updateSettings(c) }
                    }), displayedComponents: .hourAndMinute)
                DatePicker("To", selection: Binding(
                    get: { Minutes.date(s.quietEndMinutes ?? 420) },
                    set: { d in
                        var c = DeviceSettings()
                        c.quietEndMinutes = Minutes.minutes(d)
                        Task { await device.updateSettings(c) }
                    }), displayedComponents: .hourAndMinute)
            }
        } header: {
            Text("Quiet hours")
        } footer: {
            Text("During quiet hours, notifications still arrive on the device, with no light or vibration.")
        }
        .disabled(!device.state.isConnected)
    }
}
