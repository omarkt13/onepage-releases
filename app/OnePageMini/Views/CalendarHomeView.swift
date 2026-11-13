import EventKit
import OnePageKit
import SwiftUI

/// Calendar sync and Home screen set-up: calendars, sync time, weather place, clock.
struct CalendarHomeView: View {
    @Environment(AppModel.self) private var model
    @Environment(\.colorScheme) private var colorScheme

    var body: some View {
        @Bindable var calendar = model.calendar
        @Bindable var weather = model.weather
        let device = model.device
        Form {
            Section {
                Toggle("Sync calendar to the device", isOn: Binding(
                    get: { calendar.isOn && calendar.hasFullAccess },
                    set: { on in
                        Task {
                            if on {
                                var allowed = calendar.hasFullAccess
                                if !allowed { allowed = await calendar.requestAccess() }
                                if allowed {
                                    calendar.isOn = true
                                    await calendar.sync()
                                }
                            } else {
                                calendar.isOn = false
                                await calendar.clearOnDevice()
                            }
                        }
                    }))
                if calendar.authorization == .denied || calendar.authorization == .restricted || calendar.authorization == .writeOnly {
                    Text("Full calendar access is off. Open Settings > OnePage > Calendars and pick Full Access.")
                        .foregroundStyle(.red)
                    Button("Open Settings") { openAppSettings() }
                }
            } footer: {
                Text("The app sends the next 7 days: start, end, title and place. The device keeps them, so the Calendar screen works when the phone is away. A change on the phone can take up to 6 hours to reach the device if the app is not open.")
            }

            if calendar.isOn && calendar.hasFullAccess {
                Section("Calendars") {
                    ForEach(calendar.calendars, id: \.calendarIdentifier) { cal in
                        Toggle(isOn: Binding(
                            get: { calendar.selectedIDs.contains(cal.calendarIdentifier) },
                            set: { on in
                                var ids = calendar.selectedIDs
                                if on { ids.insert(cal.calendarIdentifier) } else { ids.remove(cal.calendarIdentifier) }
                                calendar.selectedIDs = ids
                                calendar.scheduleSync()
                            })) {
                            HStack {
                                Circle().fill(Color(cgColor: cal.cgColor)).frame(width: 10, height: 10)
                                    .accessibilityHidden(true)
                                Text(cal.title)
                            }
                        }
                    }
                }
                Section {
                    LabeledContent("Last sync", value: calendar.lastSync?.shortRelative ?? "Never")
                    LabeledContent("On the device", value: LocalSeconds.describe(device.status.calendarSyncedAt))
                    Button {
                        Task { await calendar.sync() }
                    } label: {
                        if calendar.isSyncing {
                            HStack { ProgressView(); Text("Syncing…") }
                        } else {
                            Text("Sync now")
                        }
                    }
                    .disabled(!device.state.isConnected || calendar.isSyncing)
                    if !device.state.isConnected { NotConnectedNote(state: device.state) }
                }
            }

            Section {
                Toggle("Weather on Home", isOn: Binding(
                    get: { weather.isOn },
                    set: { on in
                        weather.isOn = on
                        if on {
                            if weather.usesLocation && !weather.locationAllowed { weather.requestLocationPermission() }
                            Task { await weather.sendNow() }
                        }
                    }))
                if weather.isOn {
                    Toggle("Use approximate location", isOn: Binding(
                        get: { weather.usesLocation },
                        set: { on in
                            weather.usesLocation = on
                            if on, !weather.locationAllowed { weather.requestLocationPermission() }
                        }))
                    if weather.usesLocation && (weather.authorization == .denied || weather.authorization == .restricted) {
                        Text("Location is off for OnePage. Allow it in Settings, or type a city below.")
                            .foregroundStyle(.red)
                        Button("Open Settings") { openAppSettings() }
                    }
                    if !weather.usesLocation {
                        LabeledContent("City", value: weather.city ?? "None")
                    }
                    CityField { city in Task { await weather.setCity(city) } }
                    Picker("Unit", selection: Binding(
                        get: { weather.useFahrenheit },
                        set: { f in
                            weather.useFahrenheit = f
                            Task { await weather.sendNow() }
                        })) {
                        Text("°C").tag(false)
                        Text("°F").tag(true)
                    }
                    .pickerStyle(.segmented)
                    if let data = weather.lastSent, let t = data.tempNowDeci {
                        let celsius = Double(t) / 10
                        let f = data.fahrenheit == true
                        LabeledContent("Last sent",
                                       value: String(format: "%@ %.0f %@, %@", data.condition?.label ?? "",
                                                     f ? celsius * 9 / 5 + 32 : celsius, f ? "°F" : "°C",
                                                     weather.placeName ?? ""))
                    }
                }
            } header: {
                Text("Weather")
            } footer: {
                VStack(alignment: .leading, spacing: 8) {
                    Text("Weather comes from Apple Weather. The device asks for it about every hour and hides it when it is old.")
                    if let a = weather.attribution {
                        AsyncImage(url: colorScheme == .dark ? a.combinedMarkDarkURL : a.combinedMarkLightURL) { image in
                            image.resizable().scaledToFit()
                        } placeholder: {
                            Text(a.serviceName)
                        }
                        .frame(height: 14)
                        .accessibilityLabel(a.serviceName)
                        Link("Weather data sources", destination: a.legalPageURL)
                    }
                }
            }

            Section("Clock") {
                Picker("Clock", selection: Binding(
                    get: { device.settings.clock12Hour ?? false },
                    set: { twelve in
                        var c = DeviceSettings()
                        c.clock12Hour = twelve
                        Task { await device.updateSettings(c) }
                    })) {
                    Text("24 hour").tag(false)
                    Text("12 hour").tag(true)
                }
                .pickerStyle(.segmented)
                .disabled(!device.state.isConnected)
            }
        }
        .navigationTitle("Calendar and Home")
        .task { await weather.loadAttribution() }
        .errorAlert($calendar.lastError)
        .errorAlert($weather.lastError)
    }
}
