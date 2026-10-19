package io.github.msmgreen.nextdeparture.core

import kotlinx.serialization.Serializable
import java.time.ZonedDateTime

// Everything between "the API answered" and "the widget draws", ported from
// lib/core/src/live.cpp rule for rule. Pure, so the rules that decide what the
// widget claims are tested on a laptop. Where a comment here is terse, the C++
// it came from says why at length.

enum class WatchState { Starting, Ok, CheckConfig, CheckKey, StopLookupGone }

// What the widget says instead of a time. "check config" on the board reads
// "Check settings" here, because that is where the phone's config lives.
fun stateMessage(s: WatchState): String = when (s) {
    WatchState.Ok -> ""
    WatchState.Starting -> "Starting"
    WatchState.CheckConfig -> "Check settings"
    WatchState.CheckKey -> "Check API key"
    WatchState.StopLookupGone -> "Stop lookup gone"
}

const val MAX_ROWS = 16 // scheduled departures kept per watch
const val MAX_ROUTE_DIRS = 6 // a stop served by more keeps the first six pairs
const val RT_PER_WATCH = 6 // trips asked about per realtime request

// A service scheduled this long ago may still be ahead of us: delays of -427 s
// are real, and so are late ones.
private const val KEEP_PAST_S = 1800L

// How long past its scheduled time a trip realtime has never mentioned is
// still asked about. It may be late rather than gone.
private const val NEVER_HEARD_S = 900L

@Serializable
data class LiveRow(
    val tripId: String,
    val stopId: String,
    val schedEpoch: Long,
    val hasRt: Boolean = false,
    val delay: Int = 0,
    val cancelled: Boolean = false,
) {
    // When it will actually leave, as far as anyone knows.
    val predicted: Long get() = schedEpoch + if (hasRt) delay else 0
}

// One (route_id, direction_id) combination seen in the window, with a trip to
// ask about and whether that trip reaches the target.
data class RouteDir(
    val routeId: String,
    val directionId: Int,
    val tripId: String, // representative trip, for trips/{id}/stops
    val stopId: String, // our stop as that trip calls it (the platform)
    val serves: Boolean = false,
)

// Does this trip call at our stop and then reach the target? Platforms carry a
// parent_station, so a station target matches either the code or the parent.
fun tripServes(stops: List<TripStop>, ourStopId: String, targetCode: String, targetStopId: String): Boolean {
    val ours = stops.indexOfFirst { it.stopId == ourStopId }
    if (ours < 0) return false
    return stops.drop(ours + 1).any {
        it.stopCode == targetCode || (targetStopId.isNotEmpty() && it.parentStation == targetStopId)
    }
}

// Distinct (route_id, direction_id) pairs in the rows, in first-seen order,
// each with the first trip seen for that pair. Direction is a property of the
// pair, not of a trip: at a station served by two lines, one line can reach
// the target and the other not (board spec 3a).
fun collectRouteDirs(rows: List<StopTripRow>, cap: Int = MAX_ROUTE_DIRS): List<RouteDir> {
    val out = mutableListOf<RouteDir>()
    for (r in rows) {
        if (r.directionId !in 0..1) continue
        if (out.any { it.directionId == r.directionId && it.routeId == r.routeId }) continue
        if (out.size >= cap) break
        out += RouteDir(r.routeId, r.directionId, r.tripId, r.stopId)
    }
    return out
}

// Rows belonging to a pair whose `serves` is true, filtered by route, scheduled
// recently enough to still matter, sorted by scheduled time (stably).
fun selectServingRows(
    rows: List<StopTripRow>,
    pairs: List<RouteDir>,
    routeIds: List<String>,
    now: Long,
    cap: Int = MAX_ROWS,
): List<LiveRow> = rows.asSequence()
    .filter { r -> pairs.any { it.serves && it.directionId == r.directionId && it.routeId == r.routeId } }
    .filter { routeIds.isEmpty() || it.routeId in routeIds }
    .map { LiveRow(it.tripId, it.stopId, gtfsEpoch(it.serviceDate, it.departureS)) }
    .filter { it.schedEpoch >= now - KEEP_PAST_S }
    .take(cap)
    .sortedBy { it.schedEpoch }
    .toList()

// Ok when at least one pair serves; CheckConfig when none does AND some route
// had both directions present; otherwise Ok with nothing to show. One direction
// of one line simply means nothing is due your way right now.
fun verdictForPairs(pairs: List<RouteDir>): WatchState {
    if (pairs.any { it.serves }) return WatchState.Ok
    val bothWays = pairs.any { a -> pairs.any { b -> a.routeId == b.routeId && a.directionId != b.directionId } }
    return if (bothWays) WatchState.CheckConfig else WatchState.Ok
}

// Applies delays and cancellations to rows whose trip_id we asked for; entities
// for any other trip are discarded (the tripid filter is inexact). A row we DID
// ask about and heard nothing back for has its realtime cleared: an old delay
// must never keep being shown as live.
fun applyRealtime(rows: List<LiveRow>, ents: List<RtEntity>, requested: Collection<String>): List<LiveRow> =
    rows.map { row ->
        val ent = ents.firstOrNull { it.tripId == row.tripId }
        when {
            ent == null && row.tripId in requested -> row.copy(hasRt = false, delay = 0, cancelled = false)
            ent == null -> row
            // The per-stop update usually describes the vehicle's next stop,
            // not ours; prefer it only when it really is ours.
            ent.stuDepartureDelay != null && ent.stuStopId == row.stopId ->
                row.copy(cancelled = ent.cancelled, hasRt = true, delay = ent.stuDepartureDelay)
            ent.delay != null -> row.copy(cancelled = ent.cancelled, hasRt = true, delay = ent.delay)
            else -> row.copy(cancelled = ent.cancelled)
        }
    }

// A schedule refresh rebuilds every row from scratch, which would forget what
// realtime already said. Without this a bus running late is dropped as
// "departed" at every 15-minute refresh.
fun carryRealtime(old: List<LiveRow>, fresh: List<LiveRow>): List<LiveRow> = fresh.map { row ->
    val was = old.firstOrNull { it.tripId == row.tripId } ?: return@map row
    row.copy(hasRt = was.hasRt, delay = was.delay, cancelled = was.cancelled)
}

// Trip ids worth asking realtime about: those still to come, at most cap. A row
// realtime has spoken about is still to come until sched + delay is a minute
// gone; a row it never has may be late rather than gone, so it is asked about
// for 15 minutes past its scheduled time.
fun realtimeIds(rows: List<LiveRow>, now: Long, cap: Int = RT_PER_WATCH): List<String> = rows.asSequence()
    .filter { it.tripId.isNotEmpty() }
    .filter { if (it.hasRt) it.schedEpoch + it.delay >= now - 60 else it.schedEpoch >= now - NEVER_HEARD_S }
    .map { it.tripId }
    .take(cap)
    .toList()

data class FetchWindow(val date: java.time.LocalDate, val startHour: Int, val hourRange: Int)

private const val LOOKAHEAD_H = 3
private const val SMALL_HOURS_END = 4 // no service date runs later than 27:00

// The stoptrips request(s) covering the next three hours. A service date runs
// past midnight (its late trips are 24:xx, 25:xx) and the API does not clamp
// hour_range at 24, so from 04:00 one request covers it. In the small hours the
// trains still running belong to yesterday's service date, so a second request
// asks yesterday from 24 + hour; today's own is asked from max(hour, 1).
fun scheduleWindows(now: ZonedDateTime): List<FetchWindow> {
    val today = now.toLocalDate()
    if (now.hour >= SMALL_HOURS_END) return listOf(FetchWindow(today, now.hour, LOOKAHEAD_H))
    return listOf(
        FetchWindow(today.minusDays(1), 24 + now.hour, LOOKAHEAD_H),
        FetchWindow(today, maxOf(now.hour, 1), LOOKAHEAD_H),
    )
}

enum class Kind { Bus, Train }

@Serializable
data class Badge(val text: String, val kind: Kind, val color: Rgb?)

// A route in the rail list is a train, badged and coloured from the feed. Any
// other route is a bus, badged with its route_id up to the last '-' ("20-202"
// -> "20"), and uncoloured: AT does not colour buses (docs/at-api-notes.md).
fun badgeFor(routeId: String, rail: List<RouteInfo>): Badge {
    val r = rail.firstOrNull { it.routeId == routeId }
    if (r != null) return Badge(r.shortName, Kind.Train, r.color)
    val dash = routeId.lastIndexOf('-')
    return Badge((if (dash >= 0) routeId.substring(0, dash) else routeId).take(7), Kind.Bus, null)
}
