import OnePageKit
import SwiftUI

extension View {
    /// Shows a `UserFacingError`: what happened, and what to do next.
    func errorAlert(_ error: Binding<UserFacingError?>) -> some View {
        alert(error.wrappedValue?.title ?? "",
              isPresented: Binding(get: { error.wrappedValue != nil },
                                   set: { if !$0 { error.wrappedValue = nil } }),
              presenting: error.wrappedValue) { _ in
            Button("OK", role: .cancel) {}
        } message: { e in
            Text(e.message)
        }
    }
}

/// A connection badge used on several screens.
struct LinkBadge: View {
    let state: LinkState

    var body: some View {
        Label(state.label, systemImage: state.isConnected ? "checkmark.circle.fill" : "antenna.radiowaves.left.and.right")
            .foregroundStyle(state.isConnected ? Color.green : Color.secondary)
            .accessibilityLabel("Connection: \(state.label)")
    }
}

/// Shown when a screen needs the device and it is not connected.
struct NotConnectedNote: View {
    let state: LinkState

    var body: some View {
        Label {
            Text(state == .bluetoothOff
                 ? "Bluetooth is off. Turn it on in Control Centre."
                 : "The device is not connected. Keep it near the phone. It connects on its own.")
        } icon: {
            Image(systemName: "exclamationmark.triangle")
        }
        .font(.callout)
        .foregroundStyle(.secondary)
    }
}

enum Minutes {
    /// Minutes after midnight → a Date today, for DatePicker.
    static func date(_ minutes: UInt16) -> Date {
        let start = Calendar.current.startOfDay(for: Date())
        return start.addingTimeInterval(TimeInterval(minutes) * 60)
    }

    static func minutes(_ date: Date) -> UInt16 {
        let c = Calendar.current.dateComponents([.hour, .minute], from: date)
        return UInt16((c.hour ?? 0) * 60 + (c.minute ?? 0))
    }
}

extension LocalSeconds {
    static func describe(_ value: UInt32?) -> String {
        guard let value, value != 0 else { return "Never" }
        return date(value).shortRelative
    }
}

/// Open the app's page in iOS Settings.
@MainActor
func openAppSettings() {
    if let url = URL(string: UIApplication.openSettingsURLString) {
        UIApplication.shared.open(url)
    }
}
