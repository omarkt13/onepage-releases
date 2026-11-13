import Foundation
import OnePageKit
import UserNotifications

/// The app's own notifications on the phone: "update available" and
/// "battery low". They are separate from the notifications the device shows.
enum LocalAlerts {
    static func requestPermission() async -> Bool {
        (try? await UNUserNotificationCenter.current().requestAuthorization(options: [.alert, .sound])) ?? false
    }

    /// Posts once per version. The check runs at every reconnection and data
    /// request, and returns the stored result between daily checks.
    static func updateAvailable(version: String) {
        guard Prefs.lastUpdateAlertVersion != version else { return }
        Prefs.lastUpdateAlertVersion = version
        post(id: "update-\(version)",
             title: "Device update available",
             body: "Version \(version) is ready for your OnePage Mini. Open the app and keep the phone near the device to install it.")
    }

    /// Posts once per discharge, below 10%, while not charging.
    static func checkBattery(_ status: DeviceStatus) {
        guard let level = status.battery else { return }
        let charging = status.charging == .charging || status.charging == .full
        if charging || level > 15 {
            Prefs.lowBatteryAlerted = false
            return
        }
        if level <= 10, !Prefs.lowBatteryAlerted {
            Prefs.lowBatteryAlerted = true
            post(id: "battery-low",
                 title: "OnePage Mini battery low",
                 body: "The device battery is at \(level)%. Charge it soon. At 3% it switches off.")
        }
    }

    private static func post(id: String, title: String, body: String) {
        let content = UNMutableNotificationContent()
        content.title = title
        content.body = body
        let request = UNNotificationRequest(identifier: id, content: content, trigger: nil)
        UNUserNotificationCenter.current().add(request)
    }
}
