import SwiftUI

struct RootView: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        @Bindable var device = model.device
        Group {
            if model.onboardingDone {
                MainTabs()
            } else {
                OnboardingView()
            }
        }
        .errorAlert($device.lastError)
    }
}

struct MainTabs: View {
    @Environment(AppModel.self) private var model

    var body: some View {
        TabView {
            NavigationStack { DeviceView() }
                .tabItem { Label("Device", systemImage: "rectangle.portrait") }
            NavigationStack { NotificationsView() }
                .tabItem { Label("Notifications", systemImage: "bell") }
            NavigationStack { LibraryView() }
                .tabItem { Label("Library", systemImage: "books.vertical") }
            NavigationStack { CalendarHomeView() }
                .tabItem { Label("Calendar", systemImage: "calendar") }
            NavigationStack { MoreView() }
                .tabItem { Label("More", systemImage: "ellipsis.circle") }
        }
    }
}

struct MoreView: View {
    var body: some View {
        List {
            NavigationLink { PrivacyView() } label: { Label("Privacy", systemImage: "hand.raised") }
            NavigationLink { DeviceSettingsView() } label: { Label("Device settings", systemImage: "gearshape") }
            NavigationLink { UpdateView() } label: { Label("Device update", systemImage: "arrow.down.circle") }
            NavigationLink { HelpView() } label: { Label("Help", systemImage: "questionmark.circle") }
        }
        .navigationTitle("More")
    }
}
