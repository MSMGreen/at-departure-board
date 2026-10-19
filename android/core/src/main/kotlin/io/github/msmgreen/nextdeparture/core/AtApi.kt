package io.github.msmgreen.nextdeparture.core

import kotlinx.serialization.Serializable
import kotlinx.serialization.SerializationException
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.intOrNull
import java.time.LocalDate

// URLs and parsers for the two AT APIs. Pure: no networking here, so every
// shape below is covered by a fixture in test/fixtures/. docs/at-api-notes.md
// records why each of these looks the way it does.

const val KEY_HEADER = "Ocp-Apim-Subscription-Key"

object AtUrls {
    private const val GTFS = "https://api.at.govt.nz/gtfs/v3"
    private const val REALTIME = "https://api.at.govt.nz/realtime/legacy"

    fun stopByCode(stopCode: String) = "$GTFS/stops?filter%5Bstop_code%5D=$stopCode"

    fun routesByShortName(shortName: String) = "$GTFS/routes?filter%5Broute_short_name%5D=$shortName"

    fun railRoutes() = "$GTFS/routes?filter%5Broute_type%5D=2"

    // startHour must be 1..47. The API answers 400 for 0 (a required-tag
    // zero-value check), and accepts 24 and above: after-midnight trips are
    // filed under the previous service date as 24:xx, 25:xx (verified 2026-09-19).
    fun stoptrips(stopId: String, date: LocalDate, startHour: Int, hourRange: Int): String? {
        if (startHour !in 1..47 || hourRange < 1) return null
        return "$GTFS/stops/$stopId/stoptrips?filter%5Bdate%5D=$date" +
            "&filter%5Bstart_hour%5D=$startHour&filter%5Bhour_range%5D=$hourRange"
    }

    fun tripStops(tripId: String) = "$GTFS/trips/$tripId/stops"

    fun realtime(tripIds: List<String>): String? =
        if (tripIds.isEmpty()) null else "$REALTIME/tripupdates?tripid=${tripIds.joinToString(",")}"
}

@Serializable
data class StopInfo(val stopId: String, val stopName: String, val locationType: Int)

@Serializable
data class RouteInfo(val routeId: String, val shortName: String, val routeType: Int, val color: Rgb?)

data class StopTripRow(
    val tripId: String,
    val routeId: String,
    val stopId: String, // the platform, for a station's rows
    val directionId: Int,
    val serviceDate: LocalDate,
    val departureS: Int, // seconds from the service day's noon minus 12h
)

data class TripStop(val stopId: String, val stopCode: String, val parentStation: String)

data class RtEntity(
    val tripId: String,
    val delay: Int?, // trip level, signed; negative is early
    val cancelled: Boolean,
    val stuStopId: String,
    val stuDepartureDelay: Int?,
)

// Every parser returns null when the body is not the JSON shape it expects at
// all, and an empty list when AT answered with no rows. An unknown stop code
// is the second kind: 200 with {"data":[]} (docs/at-api-notes.md).
object AtParse {
    private val json = Json { ignoreUnknownKeys = true }

    private fun root(body: String): JsonObject? = try {
        json.parseToJsonElement(body) as? JsonObject
    } catch (e: SerializationException) {
        null
    }

    private fun JsonElement?.obj(): JsonObject? = this as? JsonObject

    private fun JsonObject.str(key: String): String =
        (this[key] as? JsonPrimitive)?.takeIf { it.isString }?.content ?: ""

    private fun JsonObject.int(key: String): Int? = (this[key] as? JsonPrimitive)?.intOrNull

    private fun attributes(body: String): List<JsonObject>? {
        val data = root(body)?.get("data") as? JsonArray ?: return null
        return data.mapNotNull { it.obj()?.get("attributes").obj() }
    }

    fun stops(body: String): List<StopInfo>? = attributes(body)?.mapNotNull { a ->
        StopInfo(a.str("stop_id"), a.str("stop_name"), a.int("location_type") ?: 0)
            .takeIf { it.stopId.isNotEmpty() }
    }

    fun routes(body: String): List<RouteInfo>? = attributes(body)?.mapNotNull { a ->
        RouteInfo(a.str("route_id"), a.str("route_short_name"), a.int("route_type") ?: 0, parseHex(a.str("route_color")))
            .takeIf { it.routeId.isNotEmpty() }
    }

    fun stoptrips(body: String): List<StopTripRow>? = attributes(body)?.mapNotNull { a ->
        val secs = parseGtfsTime(a.str("departure_time")) ?: return@mapNotNull null
        val date = parseIsoDate(a.str("service_date")) ?: return@mapNotNull null
        StopTripRow(a.str("trip_id"), a.str("route_id"), a.str("stop_id"), a.int("direction_id") ?: -1, date, secs)
            .takeIf { it.tripId.isNotEmpty() }
    }

    // Order is positional - these rows carry no stop_sequence, so never sort them.
    fun tripStops(body: String): List<TripStop>? = attributes(body)?.mapNotNull { a ->
        TripStop(a.str("stop_id"), a.str("stop_code"), a.str("parent_station"))
            .takeIf { it.stopId.isNotEmpty() }
    }

    fun realtime(body: String): List<RtEntity>? {
        val entities = root(body)?.get("response").obj()?.get("entity") as? JsonArray ?: return null
        return entities.mapNotNull { ent ->
            val tu = ent.obj()?.get("trip_update").obj() ?: return@mapNotNull null
            val trip = tu["trip"].obj()
            // The docs promise an array; the API sends one object. Accept both,
            // taking the first element of an array.
            val stuRaw = tu["stop_time_update"]
            val stu = (if (stuRaw is JsonArray) stuRaw.firstOrNull() else stuRaw).obj()
            RtEntity(
                tripId = trip?.str("trip_id") ?: "",
                delay = tu.int("delay"),
                cancelled = trip?.int("schedule_relationship") == 3,
                stuStopId = stu?.str("stop_id") ?: "",
                stuDepartureDelay = stu?.get("departure").obj()?.int("delay"),
            ).takeIf { it.tripId.isNotEmpty() }
        }
    }

    // The first error's detail from a 4xx body, for the log. AT's 400 for
    // start_hour=0 is the only one captured (stoptrips_start_hour_0.json).
    fun errorDetail(body: String): String? {
        val errors = root(body)?.get("errors") as? JsonArray ?: return null
        val first = errors.firstOrNull().obj() ?: return null
        return first.str("detail").ifEmpty { first.str("title") }.ifEmpty { null }
    }
}
