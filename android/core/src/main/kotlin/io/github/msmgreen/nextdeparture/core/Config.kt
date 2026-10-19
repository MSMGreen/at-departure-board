package io.github.msmgreen.nextdeparture.core

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.SerializationException
import kotlinx.serialization.json.Json
import java.time.LocalTime

// Each phone's configuration (widget spec 3). One JSON document with a "v"
// field, as the board stores its config in NVS (config portal spec 4), and with
// the board's field names for the watch. Coordinates live here and only here:
// this repository is public, and a place is someone's home.

const val CONFIG_VERSION = 1
const val MAX_PLACES = 4 // matches the board's MAX_WATCHES
const val MAX_WINDOWS = 4
const val WEEKDAYS = 0b0011111 // Monday to Friday; bit 0 is Monday

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

// A commute window (widget spec 3, 9): local Pacific/Auckland wall-clock times,
// [start, end) on the days in the mask, never crossing midnight.
@Serializable
data class CommuteWindow(
    val days: Int = WEEKDAYS,
    val start: String, // "07:30"
    val end: String, // "08:30"
    @SerialName("default_place") val defaultPlace: Int = 0, // index into places
) {
    // Only for a validated window: validate() rejects a time that will not parse.
    val startTime: LocalTime get() = parseClock(start)!!
    val endTime: LocalTime get() = parseClock(end)!!
}

// A window's default place after the place at removed is deleted: the same
// place where it survives, else the first.
fun placeAfterRemoval(place: Int, removed: Int): Int = when {
    place == removed -> 0
    place > removed -> place - 1
    else -> place
}

// "07:30", strictly: two ASCII digits, a colon, two digits.
fun parseClock(s: String): LocalTime? {
    if (s.length != 5 || s[2] != ':') return null
    if (!s.removeRange(2, 3).all { it in '0'..'9' }) return null
    val h = s.substring(0, 2).toInt()
    val m = s.substring(3, 5).toInt()
    if (h > 23 || m > 59) return null
    return LocalTime.of(h, m)
}

// Windows arrived in phase 2 with a default, so a phase 1 config still reads
// back and "v" stays 1.
@Serializable
data class Config(
    val v: Int = CONFIG_VERSION,
    val places: List<Place>,
    val windows: List<CommuteWindow> = emptyList(),
)

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
    TooManyWindows("at most four commute windows"),
    WindowDays("a window needs at least one day"),
    WindowTime("a window is HH:MM to a later HH:MM the same day"),
    WindowPlace("a window's place must be one of the places"),
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
        if (c.windows.size > MAX_WINDOWS) return ConfigError.TooManyWindows
        for (w in c.windows) {
            if (w.days !in 1..0b1111111) return ConfigError.WindowDays
            val start = parseClock(w.start) ?: return ConfigError.WindowTime
            val end = parseClock(w.end) ?: return ConfigError.WindowTime
            if (!start.isBefore(end)) return ConfigError.WindowTime
            if (w.defaultPlace !in c.places.indices) return ConfigError.WindowPlace
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
