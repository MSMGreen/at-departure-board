package io.github.msmgreen.nextdeparture.core

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.SerializationException
import kotlinx.serialization.json.Json

// Each phone's configuration (widget spec 3). One JSON document with a "v"
// field, as the board stores its config in NVS (config portal spec 4), and with
// the board's field names for the watch. Coordinates live here and only here:
// this repository is public, and a place is someone's home.

const val CONFIG_VERSION = 1
const val MAX_PLACES = 4 // matches the board's MAX_WATCHES

@Serializable
data class WatchConfig(
    val label: String, // e.g. "to Wynyard Quarter"
    @SerialName("stop_code") val stopCode: String, // the number on the pole
    @SerialName("route_short_name") val routeShortName: String = "", // "" means any route
    @SerialName("toward_stop_code") val towardStopCode: String, // never a direction (board spec 3a)
) {
    // What a cached schedule belongs to. A change to any of these starts over.
    val key: String get() = "$stopCode|$routeShortName|$towardStopCode"
}

@Serializable
data class Place(
    val name: String,
    val watch: WatchConfig,
    @SerialName("walk_min") val walkMin: Int = 0,
    val lat: Double? = null, // set from phase 3, for geofences
    val lon: Double? = null,
    @SerialName("radius_m") val radiusM: Int = 200,
)

@Serializable
data class Config(val v: Int = CONFIG_VERSION, val places: List<Place>)

enum class ConfigError(val text: String) {
    BadJson("could not read the settings"),
    BadVersion("settings version is not supported"),
    NoPlaces("at least one place is required"),
    TooManyPlaces("at most four places"),
    Name("a place name is 1 to 12 characters"),
    Label("a label is at most 24 characters"),
    StopCode("a stop code is 1 to 11 letters or digits"),
    Route("a route is at most 11 letters, digits or dashes"),
    Toward("the toward stop is 1 to 11 letters or digits"),
    WalkMin("walk time is 0 to 30 minutes"),
    Radius("radius is 100 to 1000 m"),
    Coordinates("set both latitude and longitude, or neither"),
}

sealed interface ConfigResult {
    data class Ok(val config: Config) : ConfigResult
    data class Bad(val error: ConfigError) : ConfigResult
}

object ConfigCodec {
    private val json = Json { ignoreUnknownKeys = true; encodeDefaults = true }

    private fun isCode(s: String) = s.length in 1..11 && s.all { it in '0'..'9' || it in 'a'..'z' || it in 'A'..'Z' }

    private fun isRoute(s: String) =
        s.length <= 11 && s.all { it in '0'..'9' || it in 'a'..'z' || it in 'A'..'Z' || it == '-' }

    // Validation rejects rather than truncates: a truncated stop code would be
    // a silently wrong departure. Stop codes must be plain ASCII alphanumeric
    // before they reach a URL, as the portal requires (src/portal.cpp). Unlike
    // the board, the toward stop is required: one departure needs a direction.
    fun validate(c: Config): ConfigError? {
        if (c.v != CONFIG_VERSION) return ConfigError.BadVersion
        if (c.places.isEmpty()) return ConfigError.NoPlaces
        if (c.places.size > MAX_PLACES) return ConfigError.TooManyPlaces
        for (p in c.places) {
            if (p.name.isBlank() || p.name.length > 12) return ConfigError.Name
            if (p.watch.label.length > 24) return ConfigError.Label
            if (!isCode(p.watch.stopCode)) return ConfigError.StopCode
            if (!isRoute(p.watch.routeShortName)) return ConfigError.Route
            if (!isCode(p.watch.towardStopCode)) return ConfigError.Toward
            if (p.walkMin !in 0..30) return ConfigError.WalkMin
            if (p.radiusM !in 100..1000) return ConfigError.Radius
            if ((p.lat == null) != (p.lon == null)) return ConfigError.Coordinates
            if (p.lat != null && (p.lat !in -90.0..90.0 || p.lon!! !in -180.0..180.0)) return ConfigError.Coordinates
        }
        return null
    }

    fun parse(text: String): ConfigResult {
        val c = try {
            json.decodeFromString(Config.serializer(), text)
        } catch (e: SerializationException) {
            return ConfigResult.Bad(ConfigError.BadJson)
        } catch (e: IllegalArgumentException) {
            return ConfigResult.Bad(ConfigError.BadJson)
        }
        return validate(c)?.let { ConfigResult.Bad(it) } ?: ConfigResult.Ok(c)
    }

    fun toJson(c: Config): String = json.encodeToString(Config.serializer(), c)
}
