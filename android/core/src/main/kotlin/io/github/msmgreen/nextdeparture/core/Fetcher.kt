package io.github.msmgreen.nextdeparture.core

import kotlinx.serialization.Serializable
import kotlinx.serialization.SerializationException
import kotlinx.serialization.json.Json

// The board's fetch task (src/fetcher.cpp) for one watch, as a function from
// the cache and the clock to the next cache. The network is behind AtHttp, so
// a test can answer with the captured fixtures; the app answers with OkHttp.
// The process is killed between almost every run, so everything that must
// outlive one fetch is in the Cache, which the app persists.

// The HTTP status, or TRANSPORT_ERROR when nothing came back at all.
data class HttpResult(val status: Int, val body: String)

const val TRANSPORT_ERROR = -1

fun interface AtHttp {
    fun get(url: String): HttpResult
}

private const val BACKOFF_CAP_S = 300
private const val RESOLVE_RETRY_S = 60L // a 400/401 must not become a request storm
private const val SCHEDULE_RETRY_S = 60L // after an inconclusive refresh: soon

@Serializable
data class WatchCache(
    val key: String, // WatchConfig.key this cache was built for
    val state: WatchState = WatchState.Starting,
    val resolved: Boolean = false,
    val stopId: String = "",
    val targetStopId: String = "",
    val routeIds: List<String> = emptyList(), // empty: any route
    val nextResolveAt: Long = 0,
    val badge: Badge? = null,
    val rows: List<LiveRow> = emptyList(),
    val schedDate: String = "", // local date of the last refresh, for rollover
    val nextSchedAt: Long = 0,
    val schedOkAt: Long = 0, // last conclusive schedule refresh
    val lastOk: Long = 0, // see lastOkAfter
    val backoffS: Int = 0,
    val backoffUntil: Long = 0,
)

@Serializable
data class Cache(
    val rail: List<RouteInfo>? = null, // the rail route table, fetched once
    val watch: WatchCache? = null,
) {
    fun toJson(): String = json.encodeToString(serializer(), this)

    companion object {
        private val json = Json { ignoreUnknownKeys = true }

        // A cache that will not read back is a cache to rebuild, never an error.
        fun fromJson(text: String?): Cache = try {
            if (text.isNullOrEmpty()) Cache() else json.decodeFromString(serializer(), text)
        } catch (e: SerializationException) {
            Cache()
        } catch (e: IllegalArgumentException) {
            Cache()
        }
    }
}

class Fetcher(private val http: AtHttp, private val log: (String) -> Unit = {}) {

    // Folded over every request in one refresh, to decide backoff once.
    private class Pass {
        var backoff = false
        var news = false
    }

    // One refresh. force (a tap) skips the backoff and resolve waits; it never
    // forces a schedule refresh that is not due.
    fun refresh(cache: Cache, watch: WatchConfig, now: Long, force: Boolean = false): Cache {
        var w = cache.watch?.takeIf { it.key == watch.key } ?: WatchCache(watch.key)
        if (!force && now < w.backoffUntil) return cache.copy(watch = w)

        val pass = Pass()
        val rail = cache.rail ?: loadRail(pass)
        if (!w.resolved && (force || now >= w.nextResolveAt)) w = resolve(w, watch, rail.orEmpty(), now, pass)
        if (w.resolved) {
            val today = local(now).toLocalDate().toString()
            if (now >= w.nextSchedAt || w.schedDate != today) {
                w = refreshSchedule(w, watch, rail.orEmpty(), now, pass)
            }
        }
        if (w.state == WatchState.Ok) w = refreshRealtime(w, now, pass)

        w = w.copy(lastOk = lastOkAfter(w.lastOk, now, pass.news, w.schedOkAt))
        // Spec 8: 429, 5xx and anything that never reached the server double
        // the wait to a 5-minute cap, with the last good data left in place.
        w = if (pass.backoff) {
            val prev = if (w.backoffS > 0) w.backoffS else RT_FAST_S
            val next = if (prev > BACKOFF_CAP_S / 2) BACKOFF_CAP_S else prev * 2
            w.copy(backoffS = next, backoffUntil = now + next)
        } else {
            w.copy(backoffS = 0, backoffUntil = 0)
        }
        return Cache(rail, w)
    }

    private fun get(url: String, pass: Pass): HttpResult {
        val r = http.get(url)
        if (r.status == TRANSPORT_ERROR || r.status == 429 || r.status >= 500) pass.backoff = true
        log("GET ${url.substringAfter("api.at.govt.nz")} -> ${r.status}")
        return r
    }

    // The rail route table decides Kind, and supplies the badge and colour for a
    // watch that pins no route - which is how a rail watch survives a rename.
    private fun loadRail(pass: Pass): List<RouteInfo>? {
        val r = get(AtUrls.railRoutes(), pass)
        if (r.status != 200) return null
        val routes = AtParse.routes(r.body)
        if (routes == null) pass.backoff = true
        return routes
    }

    private sealed interface Lookup {
        data class Found(val stop: StopInfo) : Lookup
        data object Unknown : Lookup // 200 with an empty data array
        data class Failed(val status: Int) : Lookup
    }

    private fun lookupStop(code: String, pass: Pass): Lookup {
        val r = get(AtUrls.stopByCode(code), pass)
        if (r.status != 200) return Lookup.Failed(r.status)
        val stops = AtParse.stops(r.body) ?: return Lookup.Failed(TRANSPORT_ERROR).also { pass.backoff = true }
        return stops.firstOrNull()?.let { Lookup.Found(it) } ?: Lookup.Unknown
    }

    private fun checkKey(w: WatchCache, now: Long) = w.copy(
        state = WatchState.CheckKey,
        nextResolveAt = now + RESOLVE_RETRY_S,
        nextSchedAt = maxOf(w.nextSchedAt, now + RESOLVE_RETRY_S),
    )

    // The stop, the stop it points toward, and the route ids behind a pinned
    // short name. The status mapping is the whole of board spec 8 for this step.
    private fun resolve(w: WatchCache, watch: WatchConfig, rail: List<RouteInfo>, now: Long, pass: Pass): WatchCache {
        val failed = { status: Int ->
            when (status) {
                // The undocumented filter[stop_code] has been withdrawn (spec 8).
                400 -> w.copy(state = WatchState.StopLookupGone, nextResolveAt = now + RESOLVE_RETRY_S)
                401 -> checkKey(w, now)
                else -> w // transient: try again next time, under backoff
            }
        }
        val unknown = w.copy(state = WatchState.CheckConfig, nextResolveAt = now + RESOLVE_RETRY_S)

        val stop = when (val l = lookupStop(watch.stopCode, pass)) {
            is Lookup.Found -> l.stop
            Lookup.Unknown -> return unknown
            is Lookup.Failed -> return failed(l.status)
        }
        val target = when (val l = lookupStop(watch.towardStopCode, pass)) {
            is Lookup.Found -> l.stop
            Lookup.Unknown -> return unknown
            is Lookup.Failed -> return failed(l.status)
        }

        var routeIds = emptyList<String>()
        var badge = w.badge
        if (watch.routeShortName.isNotEmpty()) {
            val r = get(AtUrls.routesByShortName(watch.routeShortName), pass)
            // A 400 here is a short name AT will not accept - a config fault,
            // not the stop-code filter being withdrawn.
            if (r.status == 400) return unknown
            if (r.status != 200) return failed(r.status)
            val routes = AtParse.routes(r.body) ?: return w.also { pass.backoff = true }
            if (routes.isEmpty()) return unknown
            // Every returned route_id: a short name spans both sides of a rename.
            routeIds = routes.map { it.routeId }
            badge = badgeFor(routes[0].routeId, rail).let {
                if (routes[0].routeType == 2) it.copy(kind = Kind.Train) else it
            }
        }
        log("resolve ${watch.stopCode} -> ${stop.stopId}  toward ${watch.towardStopCode} -> ${target.stopId}")
        return w.copy(
            state = WatchState.Starting, // rows arrive with the schedule
            resolved = true,
            stopId = stop.stopId,
            targetStopId = target.stopId,
            routeIds = routeIds,
            badge = badge,
        )
    }

    // One schedule refresh. When nothing conclusive came back, the cached rows
    // and state stay exactly as they were and it is retried in a minute.
    private fun refreshSchedule(w0: WatchCache, watch: WatchConfig, rail: List<RouteInfo>, now: Long, pass: Pass): WatchCache {
        val lt = local(now)
        // Stamped before the requests, so a failed refresh retries on
        // nextSchedAt rather than looking like a rollover on every run.
        val w = w0.copy(schedDate = lt.toLocalDate().toString())
        val inconclusive = w.copy(nextSchedAt = now + SCHEDULE_RETRY_S)

        val rows = mutableListOf<StopTripRow>()
        for (win in scheduleWindows(lt)) {
            val url = AtUrls.stoptrips(w.stopId, win.date, win.startHour, win.hourRange) ?: continue
            val r = get(url, pass)
            // 404 is "no services in this window" - the same status as an
            // unknown stop, and never a reason to re-resolve one. A real answer.
            if (r.status == 404) continue
            if (r.status == 401) return checkKey(w, now)
            if (r.status != 200) return inconclusive
            rows += AtParse.stoptrips(r.body) ?: return inconclusive.also { pass.backoff = true }
        }

        // A pinned watch never shows another route's rows, so drop them before
        // the pairs are collected, or other routes could fill the pair cap.
        val wanted = if (w.routeIds.isEmpty()) rows else rows.filter { it.routeId in w.routeIds }

        // Direction is derived every refresh, per (route_id, direction_id) pair,
        // never per trip (board spec 3a; the Kingsland E-W / O-W lesson).
        var failed = 0
        val pairs = collectRouteDirs(wanted).map { p ->
            val r = get(AtUrls.tripStops(p.tripId), pass)
            if (r.status == 401) return checkKey(w, now)
            val stops = if (r.status == 200) AtParse.tripStops(r.body) else null
            if (stops == null) {
                failed++ // serves stays false, and that is not proof of anything
                p
            } else {
                p.copy(serves = tripServes(stops, p.stopId, watch.towardStopCode, w.targetStopId))
            }
        }
        log("dirs ${watch.stopCode}: " + pairs.joinToString("  ") { "${it.routeId}/${it.directionId} ${if (it.serves) "yes" else "no"}" })

        // Inconclusive is not evidence: a failed pair may be the very one that
        // reaches the target. Conclusive means every pair spoke, or one serves.
        if (failed > 0 && pairs.none { it.serves }) return inconclusive

        val verdict = verdictForPairs(pairs)
        val next = if (verdict != WatchState.Ok) {
            // A service runs both ways past this stop and neither way gets you
            // there. A watch that cannot prove its direction shows no time.
            w.copy(state = verdict, rows = emptyList())
        } else {
            val fresh = carryRealtime(w.rows, selectServingRows(wanted, pairs, w.routeIds, now))
            val first = fresh.firstOrNull()?.let { row -> wanted.first { it.tripId == row.tripId } }
            w.copy(
                state = WatchState.Ok, // no rows is an empty window, not an error
                rows = fresh,
                badge = first?.let { badgeFor(it.routeId, rail) } ?: w.badge,
            )
        }
        return next.copy(nextSchedAt = now + SCHEDULE_PERIOD_S, schedOkAt = now)
    }

    private fun refreshRealtime(w: WatchCache, now: Long, pass: Pass): WatchCache {
        val ids = realtimeIds(w.rows, now)
        if (ids.isEmpty()) {
            pass.news = true // nothing upcoming to ask about; not a failure
            return w
        }
        val r = get(AtUrls.realtime(ids)!!, pass)
        if (r.status == 401) return checkKey(w, now)
        if (r.status != 200) return w
        val ents = AtParse.realtime(r.body) ?: return w.also { pass.backoff = true }
        pass.news = true
        // Realtime only reports trips already under way, so fewer entities than
        // ids is normal. The same id list goes to applyRealtime, so a trip we
        // asked about and heard nothing for loses its realtime.
        return w.copy(rows = applyRealtime(w.rows, ents, ids))
    }
}
