import CoreLocation
import Foundation
import Observation
import OnePageKit
import WeatherKit

/// Fetches current conditions and today's high and low from WeatherKit, and
/// sends them to the device. Uses approximate location, or a typed city.
@Observable
final class WeatherProvider: NSObject {
    @ObservationIgnored private let locationManager = CLLocationManager()
    @ObservationIgnored private let geocoder = CLGeocoder()
    @ObservationIgnored private weak var device: DeviceManager?
    @ObservationIgnored private var locationWaiters: [CheckedContinuation<CLLocation?, Never>] = []

    private(set) var authorization: CLAuthorizationStatus = .notDetermined
    private(set) var lastSent: HomeData?
    private(set) var lastSentAt: Date?
    private(set) var isFetching = false
    var lastError: UserFacingError?

    var isOn: Bool = Prefs.weatherOn { didSet { Prefs.weatherOn = isOn } }
    var usesLocation: Bool = Prefs.weatherUsesLocation { didSet { Prefs.weatherUsesLocation = usesLocation } }
    var useFahrenheit: Bool = Prefs.useFahrenheit { didSet { Prefs.useFahrenheit = useFahrenheit } }
    private(set) var city: String? = Prefs.weatherCity
    private(set) var placeName: String? = Prefs.weatherPlaceName

    init(device: DeviceManager) {
        self.device = device
        super.init()
        authorization = locationManager.authorizationStatus
        locationManager.delegate = self
        locationManager.desiredAccuracy = kCLLocationAccuracyReduced
    }

    var locationAllowed: Bool {
        authorization == .authorizedWhenInUse || authorization == .authorizedAlways
    }

    /// Shows the iOS location prompt. Called when the user turns weather on.
    func requestLocationPermission() {
        locationManager.requestWhenInUseAuthorization()
    }

    /// Looks up a typed city and stores its coordinate.
    @MainActor
    func setCity(_ name: String) async {
        let trimmed = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }
        do {
            let marks = try await geocoder.geocodeAddressString(trimmed)
            guard let mark = marks.first, let loc = mark.location else {
                lastError = UserFacingError(title: "City not found",
                                            message: "No place called \"\(trimmed)\" was found. Check the spelling, or add the country.")
                return
            }
            city = trimmed
            Prefs.weatherCity = trimmed
            store(loc, name: mark.locality ?? trimmed)
            usesLocation = false
            await sendNow()
        } catch {
            lastError = UserFacingError(title: "City not found",
                                        message: "The city lookup failed. Check the internet connection and the spelling, then try again.")
        }
    }

    private func store(_ loc: CLLocation, name: String?) {
        Prefs.weatherCoordinate = [loc.coordinate.latitude, loc.coordinate.longitude]
        if let name {
            placeName = name
            Prefs.weatherPlaceName = name
        }
    }

    /// In the foreground, asks for a fresh approximate fix. In the background
    /// the app cannot, so it uses the last stored coordinate.
    @MainActor
    private func location(allowFresh: Bool) async -> CLLocation? {
        if usesLocation, locationAllowed, allowFresh {
            let fresh: CLLocation? = await withCheckedContinuation { c in
                locationWaiters.append(c)
                if locationWaiters.count == 1 { locationManager.requestLocation() }
                DispatchQueue.main.asyncAfter(deadline: .now() + 8) { [weak self] in
                    self?.resolveLocationWaiters(nil)
                }
            }
            if let fresh {
                let name = try? await geocoder.reverseGeocodeLocation(fresh).first?.locality
                store(fresh, name: name ?? placeName)
                return fresh
            }
        }
        if usesLocation, locationAllowed, let last = locationManager.location { return last }
        if let c = Prefs.weatherCoordinate, c.count == 2 {
            return CLLocation(latitude: c[0], longitude: c[1])
        }
        return nil
    }

    private func resolveLocationWaiters(_ loc: CLLocation?) {
        let waiters = locationWaiters
        locationWaiters.removeAll()
        waiters.forEach { $0.resume(returning: loc) }
    }

    /// Builds Home data and sends it. `foreground` allows a fresh location fix.
    @MainActor
    func sendNow(foreground: Bool = true) async {
        guard isOn, let device, device.state.isConnected, !isFetching else { return }
        isFetching = true
        defer { isFetching = false }
        guard let loc = await location(allowFresh: foreground) else {
            lastError = UserFacingError(title: "No weather location",
                                        message: "The app has no location for weather. Allow approximate location, or type a city in Calendar and Home.")
            return
        }
        do {
            let weather = try await WeatherService.shared.weather(for: loc)
            let now = Date()
            var data = HomeData()
            data.tempNowDeci = HomeData.deci(weather.currentWeather.temperature.converted(to: .celsius).value)
            if let today = weather.dailyForecast.first {
                data.tempHighDeci = HomeData.deci(today.highTemperature.converted(to: .celsius).value)
                data.tempLowDeci = HomeData.deci(today.lowTemperature.converted(to: .celsius).value)
            }
            data.condition = WeatherCode.fromWeatherKit(weather.currentWeather.condition.rawValue)
            // The device asks about hourly. Allow one missed request before it hides the data.
            data.validUntil = LocalSeconds.from(now.addingTimeInterval(150 * 60))
            data.place = placeName ?? city ?? ""
            data.fahrenheit = useFahrenheit
            try await device.sendHomeData(data)
            lastSent = data
            lastSentAt = now
            lastError = nil
        } catch let e as LinkError {
            lastError = UserFacingError("Weather was not sent", e)
        } catch {
            lastError = UserFacingError(title: "Weather is not available",
                                        message: "Apple Weather did not answer. Check the internet connection. The device hides old weather on its own.")
        }
    }
}

extension WeatherProvider: CLLocationManagerDelegate {
    func locationManagerDidChangeAuthorization(_ manager: CLLocationManager) {
        authorization = manager.authorizationStatus
    }

    func locationManager(_ manager: CLLocationManager, didUpdateLocations locations: [CLLocation]) {
        resolveLocationWaiters(locations.last)
    }

    func locationManager(_ manager: CLLocationManager, didFailWithError error: Error) {
        resolveLocationWaiters(nil)
    }
}
