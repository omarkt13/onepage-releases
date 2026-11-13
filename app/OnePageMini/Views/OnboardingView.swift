import OnePageKit
import SwiftUI

/// Set-up: find and pair the device, then one page per permission that says
/// why it is needed before iOS asks.
struct OnboardingView: View {
    enum Step: Int, CaseIterable {
        case welcome, bluetooth, iosPrompts, find, calendar, weather, notifications, done
    }

    @Environment(AppModel.self) private var model
    @State private var step: Step = .welcome

    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                ProgressView(value: Double(step.rawValue), total: Double(Step.allCases.count - 1))
                    .padding(.horizontal)
                    .accessibilityLabel("Set-up step \(step.rawValue + 1) of \(Step.allCases.count)")
                ScrollView {
                    page
                        .padding()
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
            }
            .navigationTitle("Set up")
            .navigationBarTitleDisplayMode(.inline)
        }
        .onChange(of: model.device.state) { _, newState in
            if step == .find, newState == .ready { step = .calendar }
        }
        .onAppear {
            // An NFC tap before set-up goes straight to the Bluetooth page.
            if model.device.pairingTarget != nil, step == .welcome { step = .bluetooth }
        }
    }

    @ViewBuilder
    private var page: some View {
        switch step {
        case .welcome: welcome
        case .bluetooth: bluetooth
        case .iosPrompts: iosPrompts
        case .find: FindDeviceView(onSkip: { step = .calendar })
        case .calendar: calendarPage
        case .weather: weatherPage
        case .notifications: notificationsPage
        case .done: done
        }
    }

    private func next() {
        if let n = Step(rawValue: step.rawValue + 1) { step = n }
    }

    private var welcome: some View {
        Page(icon: "rectangle.portrait.on.rectangle.portrait",
             title: "OnePage Mini",
             text: "This app pairs your phone with the device, picks which apps show on it, sends books, and keeps its calendar and weather up to date.\n\nThe device gets notifications from iOS directly. The app never sees them.") {
            Button("Start") { next() }.buttonStyle(.borderedProminent)
        }
    }

    private var bluetooth: some View {
        Page(icon: "antenna.radiowaves.left.and.right",
             title: "Bluetooth",
             text: "The app uses Bluetooth to talk to the device. iOS asks you to allow it next. Without Bluetooth the app cannot work.") {
            Button("Continue") {
                model.device.activate()
                next()
            }
            .buttonStyle(.borderedProminent)
            if model.device.state == .unauthorized {
                Text("Bluetooth is not allowed. Open Settings > OnePage and turn on Bluetooth.")
                    .foregroundStyle(.red)
                Button("Open Settings") { openAppSettings() }
            }
        }
    }

    private var iosPrompts: some View {
        Page(icon: "checkmark.shield",
             title: "Two iOS prompts",
             text: "When the phone connects, iOS shows two prompts. Accept both. The app cannot skip them.\n\n1. \"Bluetooth Pairing Request\": tap Pair.\n\n2. \"Allow OnePage Mini to receive your notifications?\": tap Allow. Without it, the device shows no notifications.") {
            Button("I understand") { next() }.buttonStyle(.borderedProminent)
        }
    }

    private var calendarPage: some View {
        Page(icon: "calendar",
             title: "Calendar",
             text: "The device can show the next 7 days of events, even when the phone is away. The app needs full calendar access to read them. It sends start, end, title and place only. No notes, people or links.") {
            Button("Allow calendar") {
                Task {
                    if await model.calendar.requestAccess() {
                        model.calendar.isOn = true
                        await model.calendar.sync()
                    }
                    next()
                }
            }
            .buttonStyle(.borderedProminent)
            Button("Not now") { next() }
        }
    }

    private var weatherPage: some View {
        Page(icon: "cloud.sun",
             title: "Weather",
             text: "The device shows today's weather on its Home screen. The app uses your approximate location for it. You can type a city instead.") {
            Button("Use approximate location") {
                model.weather.isOn = true
                model.weather.usesLocation = true
                model.weather.requestLocationPermission()
                next()
            }
            .buttonStyle(.borderedProminent)
            CityField { city in
                model.weather.isOn = true
                Task {
                    await model.weather.setCity(city)
                    next()
                }
            }
            Button("Not now") { next() }
        }
    }

    private var notificationsPage: some View {
        Page(icon: "bell.badge",
             title: "Alerts from the app",
             text: "The app can tell you on the phone when a device update is ready or the device battery is low. These are separate from the notifications on the device.") {
            Button("Allow alerts") {
                Task {
                    _ = await LocalAlerts.requestPermission()
                    next()
                }
            }
            .buttonStyle(.borderedProminent)
            Button("Not now") { next() }
        }
    }

    private var done: some View {
        Page(icon: "checkmark.circle",
             title: "Ready",
             text: "Bank and sign-in code apps: when one first sends a notification, the app suggests \"title only\", so the device shows that it arrived without its text.\n\nYou can change every choice later.") {
            Button("Done") {
                Task { await model.finishOnboarding() }
            }
            .buttonStyle(.borderedProminent)
        }
    }
}

/// One set-up page: icon, title, text, then actions.
struct Page<Actions: View>: View {
    let icon: String
    let title: String
    let text: String
    @ViewBuilder let actions: () -> Actions

    var body: some View {
        VStack(alignment: .leading, spacing: 20) {
            Image(systemName: icon)
                .font(.system(size: 48))
                .foregroundStyle(.tint)
                .accessibilityHidden(true)
            Text(title).font(.largeTitle.bold())
            Text(text).font(.body)
            VStack(alignment: .leading, spacing: 12) { actions() }
        }
    }
}

struct CityField: View {
    let onSubmit: (String) -> Void
    @State private var text = ""

    var body: some View {
        HStack {
            TextField("City", text: $text)
                .textFieldStyle(.roundedBorder)
                .textContentType(.addressCity)
                .submitLabel(.done)
                .onSubmit { onSubmit(text) }
            Button("Use city") { onSubmit(text) }
                .disabled(text.trimmingCharacters(in: .whitespaces).isEmpty)
        }
    }
}

/// Lists devices in pairing mode. A device found from an NFC tap connects on its own.
struct FindDeviceView: View {
    @Environment(AppModel.self) private var model
    let onSkip: () -> Void

    var body: some View {
        let device = model.device
        VStack(alignment: .leading, spacing: 16) {
            Text("Find your device").font(.largeTitle.bold())
            if let target = device.pairingTarget {
                Text("Looking for device \(target). Keep it near the phone.")
            } else {
                Text("Tap the phone on the device to start pairing. Or open pairing mode from the reset menu in the device's Settings.")
            }

            switch device.state {
            case .bluetoothOff:
                NotConnectedNote(state: device.state)
            case .unauthorized:
                Text("Bluetooth is not allowed. Open Settings > OnePage and turn on Bluetooth.").foregroundStyle(.red)
                Button("Open Settings") { openAppSettings() }
            case .connecting, .discovering:
                HStack { ProgressView(); Text("Connecting. Accept the iOS prompts when they appear.") }
            default:
                EmptyView()
            }

            let pairing = device.discovered.filter { $0.info.pairing }
            if pairing.isEmpty {
                HStack { ProgressView(); Text("Searching…") }
                    .accessibilityElement(children: .combine)
            }
            ForEach(pairing) { d in
                Button {
                    device.connect(d)
                } label: {
                    HStack {
                        Image(systemName: "rectangle.portrait").accessibilityHidden(true)
                        VStack(alignment: .leading) {
                            Text(d.name).font(.headline)
                            Text("Signal \(d.rssi) dBm").font(.caption).foregroundStyle(.secondary)
                        }
                        Spacer()
                        Text("Pair")
                    }
                    .padding()
                    .background(.quaternary, in: RoundedRectangle(cornerRadius: 12))
                }
                .accessibilityLabel("Pair with \(d.name)")
            }
            let others = device.discovered.filter { !$0.info.pairing }
            if !others.isEmpty {
                Text("\(others.count) device(s) nearby are not in pairing mode. Open pairing mode on yours, or tap it with the phone.")
                    .font(.footnote).foregroundStyle(.secondary)
            }
            Button("Set up without a device", action: onSkip)
                .font(.footnote)
        }
        .onAppear { device.startScan() }
        .onChange(of: device.state) { _, s in
            // Bluetooth may turn on after this page appears.
            if s == .idle || s == .waitingForDevice, !device.isPaired { device.startScan() }
        }
        .onDisappear { device.stopScan() }
    }
}
