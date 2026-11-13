import Foundation

/// Condition codes in protocol section 5.4.
public enum WeatherCode: UInt8, CaseIterable, Sendable {
    case unknown = 0, clear, partlyCloudy, cloudy, fog, drizzle, rain, heavyRain,
         snow, sleet, thunderstorm, wind, hot, cold

    /// Maps the raw value of WeatherKit's `WeatherCondition` (for example
    /// "partlyCloudy") to a device code. Kept here, as strings, so it is
    /// testable without WeatherKit. The match ignores case, so "PartlyCloudy"
    /// (the REST API spelling) also maps.
    public static func fromWeatherKit(_ raw: String) -> WeatherCode {
        switch raw.lowercased() {
        case "clear", "mostlyclear": return .clear
        case "partlycloudy", "sunshowers", "sunflurries": return .partlyCloudy
        case "mostlycloudy", "cloudy": return .cloudy
        case "foggy", "haze", "smoky", "blowingdust": return .fog
        case "drizzle", "freezingdrizzle": return .drizzle
        case "rain", "freezingrain": return .rain
        case "heavyrain", "hurricane", "tropicalstorm": return .heavyRain
        case "snow", "heavysnow", "flurries", "blowingsnow", "blizzard": return .snow
        case "sleet", "wintrymix", "hail": return .sleet
        case "thunderstorms", "isolatedthunderstorms", "scatteredthunderstorms", "strongstorms": return .thunderstorm
        case "windy", "breezy": return .wind
        case "hot": return .hot
        case "frigid": return .cold
        default: return .unknown
        }
    }

    public var label: String {
        switch self {
        case .unknown: return "Unknown"
        case .clear: return "Clear"
        case .partlyCloudy: return "Partly cloudy"
        case .cloudy: return "Cloudy"
        case .fog: return "Fog"
        case .drizzle: return "Drizzle"
        case .rain: return "Rain"
        case .heavyRain: return "Heavy rain"
        case .snow: return "Snow"
        case .sleet: return "Sleet"
        case .thunderstorm: return "Thunderstorm"
        case .wind: return "Wind"
        case .hot: return "Hot"
        case .cold: return "Cold"
        }
    }
}
