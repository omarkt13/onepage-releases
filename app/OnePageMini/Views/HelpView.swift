import SwiftUI

/// Charging, pairing problems, and what the device cannot do.
struct HelpView: View {
    var body: some View {
        List {
            Section("Charging") {
                Text("Put the device on its magnetic charger. The screen shows the battery level while it charges. At 10% the device warns you. At 3% it shows \"charge me\" and switches off.")
            }
            Section("Pairing problems") {
                Text("Tap the phone on the device, or open pairing mode from the reset menu in the device's Settings. Pairing mode stays open for 2 minutes.")
                Text("iOS shows two prompts. Tap Pair, then Allow. If you tapped Don't Allow, the device gets no notifications: open iOS Settings > Bluetooth, tap the ⓘ next to OnePage Mini, and turn on Share System Notifications.")
                Text("If pairing still fails: in iOS Settings > Bluetooth, forget OnePage Mini. In this app, use Device settings > Pairing reset. Then pair again.")
            }
            Section("If you close the app") {
                Text("If you swipe the app away in the app switcher, iOS stops waking it. Notifications and music control on the device keep working. Weather, calendar sync and book transfers stop until you open the app again. The device keeps showing the last calendar it got.")
            }
            Section("Calendar timing") {
                Text("The device asks for the calendar at midnight and every 6 hours. A change you make on the phone can take up to 6 hours to arrive, unless the app is open.")
            }
            Section("What the device cannot do on iPhone") {
                Text("Reply to messages. iOS does not let an accessory send a reply.")
                Text("List your apps. An app appears in Notifications after it sends its first notification.")
                Text("Read books with copy protection, such as books from Apple Books or Kindle. Use DRM-free EPUB or text files.")
                Text("Stay up to date every second. iOS wakes the app only when the device asks, so weather can be minutes old.")
            }
        }
        .navigationTitle("Help")
    }
}
