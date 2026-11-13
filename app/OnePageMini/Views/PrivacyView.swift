import OnePageKit
import SwiftUI

/// What the device shows when the phone is away, title-only apps, and erase.
struct PrivacyView: View {
    @Environment(AppModel.self) private var model
    @State private var confirmErase = false
    @State private var erasing = false

    var body: some View {
        let device = model.device
        Form {
            Section {
                Toggle("Show calendar when the phone is away", isOn: Binding(
                    get: { device.privacy.showCalendarWhenAway ?? true },
                    set: { on in Task { await device.setShowCalendarWhenAway(on) } }))
                    .disabled(!device.state.isConnected)
            } footer: {
                Text("On: the device stays useful by itself. Off: the Calendar screen is hidden whenever the phone is not connected, which is safer if you lose the device.")
            }

            Section {
                NavigationLink("Title only, per app") { NotificationsView() }
                let count = device.appFilter.filter(\.titleOnly).count
                LabeledContent("Apps set to title only", value: "\(count)")
            } footer: {
                Text("For banking and sign-in code apps, the device shows that something arrived, without its text.")
            }

            Section("Always true") {
                Rule(icon: "timer", text: "When the phone is away for 2 minutes, the device erases all notifications and music details from its memory and shows a locked Home screen. They come back when the phone reconnects.")
                Rule(icon: "memorychip", text: "Notification text is kept in memory only. It is never written to storage, so a restart or a flat battery leaves nothing behind.")
                Rule(icon: "calendar.badge.minus", text: "After 7 days away from the phone, the device erases its calendar and weather.")
                Rule(icon: "person.badge.key", text: "Pairing a new phone always erases the device first. A found device gives the new holder nothing.")
                Rule(icon: "lock", text: "Pairing mode opens only from the device's own reset menu, or by an NFC tap when no phone is paired. Nobody can take over your device by being near it.")
                Rule(icon: "icloud.slash", text: "Nothing goes to a server. Notifications go from iOS to the device over encrypted Bluetooth. The app never sees them. The only internet use is weather and device updates.")
            }

            Section {
                Button(role: .destructive) {
                    confirmErase = true
                } label: {
                    if erasing { HStack { ProgressView(); Text("Erasing…") } } else { Text("Erase device") }
                }
                .disabled(!device.state.isConnected || erasing)
            } footer: {
                Text("Erases books, calendar, weather, settings and the app list, and removes the pairing. Use it before you sell or give away the device. Then forget \"OnePage Mini\" in iOS Settings > Bluetooth.")
            }
        }
        .navigationTitle("Privacy")
        .confirmationDialog("Erase the device?", isPresented: $confirmErase, titleVisibility: .visible) {
            Button("Erase device", role: .destructive) {
                Task {
                    erasing = true
                    defer { erasing = false }
                    do {
                        try await device.eraseDevice()
                    } catch {
                        device.lastError = UserFacingError("The device was not erased", error)
                    }
                }
            }
        } message: {
            Text("This cannot be undone.")
        }
    }
}

struct Rule: View {
    let icon: String
    let text: String

    var body: some View {
        Label {
            Text(text)
        } icon: {
            Image(systemName: icon).accessibilityHidden(true)
        }
    }
}
