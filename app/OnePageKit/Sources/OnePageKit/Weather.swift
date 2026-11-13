import Foundation

/// Condition codes in protocol section 5.4.
public enum WeatherCode: UInt8, CaseIterable {
    case unknown = 0, clear, partlyCloudy, cloudy, fog, drizzle, rain, heavyRain,
         snow, sleet, thunderstorm, wind, hot, cold

    /// Maps the raw value of WeatherKit's `WeatherCondition` (for example
    /// "partlyCloudy") to a device code. Kept here, as strings, so it is
    /// testable without WeatherKit.
    public static func fromWeatherKit(_ raw: String) -> WeatherCode {
        switch raw {
        case "clear", "mostlyClear": return .clear
        case "partlyCloudy", "sunShowers", "sunFlurries": return .partlyCloudy
        case "mostlyCloudy", "cloudy": return .cloudy
        case "foggy", "haze", "smoky", "blowingDust": return .fog
        case "drizzle", "freezingDrizzle": return .drizzle
        case "rain", "freezingRain": return .rain
        case "heavyRain", "hurricane", "tropicalStorm": return .heavyRain
        case "snow", "heavySnow", "flurries", "blowingSnow", "blizzard": return .snow
        case "sleet", "wintryMix", "hail": return .sleet
        case "thunderstorms", "isolatedThunderstorms", "scatteredThunderstorms", "strongStorms": return .thunderstorm
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
