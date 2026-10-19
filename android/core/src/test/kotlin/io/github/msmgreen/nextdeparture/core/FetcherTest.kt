package io.github.msmgreen.nextdeparture.core

import java.time.LocalDate
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

// The fetch loop end to end, with AT played by the captured fixtures.
class FetcherTest {
    // Answers by exact URL and records every request; anything unexpected is a 500.
    private class FakeAt(val answers: MutableMap<String, HttpResult>) : AtHttp {
        val asked = mutableListOf<String>()

        override fun get(url: String): HttpResult {
            asked += url
            return answers[url] ?: HttpResult(500, "")
        }
    }

    private fun ok(body: String) = HttpResult(200, body)

    private fun stopsJson(vararg stops: Triple<String, String, String>) = stops.joinToString(",", """{"data":[""", "]}") {
        """{"type":"stop","attributes":{"stop_id":"${it.first}","stop_code":"${it.second}","parent_station":"${it.third}"}}"""
    }

    private val day = LocalDate.of(2026, 9, 13)

    // --- Kingsland toward Waitemata: two lines, both ways, only E-W/1 serves ---

    private val train = WatchConfig("to Waitemata", "122", "", "133")
    private val ew1 = "258-880002-52680-2-W125580-df1fa82c" // first E-W dir-1 trip, 15:13

    private fun kingsland() = FakeAt(
        mutableMapOf(
            AtUrls.railRoutes() to ok(fixture("post-crl/routes_rail.json")),
            AtUrls.stopByCode("122") to ok(fixture("post-crl/stops_122.json")),
            AtUrls.stopByCode("133") to ok(
                """{"data":[{"type":"stop","attributes":{"stop_id":"133-08da14b5","stop_name":"Waitemata Train Station","location_type":1}}]}""",
            ),
            AtUrls.stoptrips("122-34ecc043", day, 15, 3)!! to ok(fixture("post-crl/stoptrips_kingsland.json")),
            // E-W dir 1 is the real captured trip, which reaches Waitemata.
            AtUrls.tripStops(ew1) to ok(fixture("post-crl/trip_dir1_stops.json")),
            // E-W dir 0 passes Waitemata before Kingsland.
            AtUrls.tripStops("258-880001-51480-2-W118540-4f47826a") to ok(
                stopsJson(Triple("9001-x", "9001", "133-08da14b5"), Triple("9305-ef07ca76", "9305", "122-34ecc043")),
            ),
            // O-W runs to Onehunga via Newmarket, either way never Waitemata.
            AtUrls.tripStops("259-860001-54420-2-H112430-40bf48e5") to ok(
                stopsJson(Triple("9305-ef07ca76", "9305", "122-34ecc043"), Triple("9306-x", "9306", "104-ff3dff29")),
            ),
            AtUrls.tripStops("259-860002-54600-2-H113410-a4151fb4") to ok(
                stopsJson(Triple("9304-dcb2ed75", "9304", "122-34ecc043"), Triple("9291-x", "9291", "136-2d5b76e2")),
            ),
        ),
    )

    private fun realtimeFor(at: FakeAt, ids: List<String>, body: String) {
        at.answers[AtUrls.realtime(ids)!!] = ok(body)
    }

    private fun trainIds(c: Cache) = c.watch!!.rows.take(RT_PER_WATCH).map { it.tripId }

    @Test fun `a first refresh resolves, derives direction and shows only the serving line`() {
        val at = kingsland()
        val c = Fetcher(at).refresh(Cache(), train, T_15_00)
        val w = c.watch!!

        assertEquals(WatchState.Ok, w.state)
        assertEquals("122-34ecc043", w.stopId)
        assertEquals("133-08da14b5", w.targetStopId)
        assertEquals(8, w.rows.size) // E-W dir 1 only: O-W never reaches Waitemata
        assertEquals(ew1, w.rows[0].tripId)
        assertEquals(gtfsEpoch(day, 15 * 3600 + 13 * 60), w.rows[0].schedEpoch)
        assertEquals("9304-dcb2ed75", w.rows[0].stopId) // the platform
        assertEquals(Badge("E-W", Kind.Train, Rgb(0x97, 0xC9, 0x3D)), w.badge)
        assertEquals(8, c.rail!!.size)
        // rail, two stops, one stoptrips, four pairs probed; realtime was a 500.
        assertEquals(9, at.asked.size)
        assertEquals(T_15_00 + SCHEDULE_PERIOD_S, w.nextSchedAt)
        assertEquals(T_15_00, w.schedOkAt)
    }

    @Test fun `realtime lands on the rows it was asked about`() {
        val at = kingsland()
        val f = Fetcher(at)
        val first = f.refresh(Cache(), train, T_15_00) // realtime 500s: no news yet
        assertTrue(isStale(first.watch!!.lastOk, T_15_00))
        assertEquals(60, first.watch.backoffS)

        realtimeFor(
            at, trainIds(first),
            """{"response":{"entity":[{"trip_update":{"trip":{"trip_id":"$ew1"},"delay":120}}]}}""",
        )
        at.asked.clear()
        val c = f.refresh(first, train, T_15_00 + 30, force = true)
        val w = c.watch!!
        assertEquals(listOf(AtUrls.realtime(trainIds(first))), at.asked) // schedule is not due
        assertTrue(w.rows[0].hasRt)
        assertEquals(120, w.rows[0].delay)
        assertFalse(w.rows[1].hasRt)
        assertEquals(T_15_00 + 30, w.lastOk)
        assertEquals(0, w.backoffS)
    }

    @Test fun `a backoff is waited out unless the refresh is a tap`() {
        val at = kingsland()
        val f = Fetcher(at)
        val first = f.refresh(Cache(), train, T_15_00)
        at.asked.clear()
        f.refresh(first, train, T_15_00 + 30)
        assertEquals(emptyList(), at.asked)
        f.refresh(first, train, T_15_00 + 30, force = true)
        assertEquals(1, at.asked.size)
    }

    @Test fun `the backoff doubles to a five-minute cap`() {
        val at = kingsland()
        val f = Fetcher(at)
        var c = f.refresh(Cache(), train, T_15_00)
        val seen = mutableListOf(c.watch!!.backoffS)
        repeat(4) { c = f.refresh(c, train, c.watch!!.backoffUntil); seen += c.watch!!.backoffS }
        assertEquals(listOf(60, 120, 240, 300, 300), seen)
    }

    @Test fun `a failed probe with no pair serving is inconclusive and changes nothing`() {
        val at = kingsland()
        at.answers.remove(AtUrls.tripStops(ew1)) // the serving pair's probe 500s
        val c = Fetcher(at).refresh(Cache(), train, T_15_00)
        val w = c.watch!!
        assertEquals(WatchState.Starting, w.state) // not "check settings"
        assertEquals(emptyList(), w.rows)
        assertEquals(T_15_00 + 60, w.nextSchedAt)
        assertEquals(0L, w.schedOkAt)
    }

    @Test fun `a toward stop no direction reaches is check settings`() {
        val at = kingsland()
        at.answers[AtUrls.stopByCode("1060")] = ok(fixture("post-crl/stops_1060.json"))
        val c = Fetcher(at).refresh(Cache(), train.copy(towardStopCode = "1060"), T_15_00)
        assertEquals(WatchState.CheckConfig, c.watch!!.state)
        assertEquals(emptyList(), c.watch.rows)
    }

    @Test fun `an empty window is a 404 and an empty widget, not an error`() {
        val at = kingsland()
        at.answers[AtUrls.stoptrips("122-34ecc043", day, 15, 3)!!] = HttpResult(404, "")
        val w = Fetcher(at).refresh(Cache(), train, T_15_00).watch!!
        assertEquals(WatchState.Ok, w.state)
        assertTrue(w.resolved) // and never a reason to re-resolve
        assertEquals(emptyList(), w.rows)
        assertEquals(T_15_00, w.lastOk) // nothing to ask realtime about is news
    }

    @Test fun `in the small hours yesterday's late services are asked for too`() {
        val at = kingsland()
        val t0040 = gtfsEpoch(LocalDate.of(2026, 9, 20), 40 * 60)
        at.answers[AtUrls.stoptrips("122-34ecc043", LocalDate.of(2026, 9, 19), 24, 3)!!] =
            ok(fixture("post-crl/stoptrips_kingsland_after_midnight.json"))
        at.answers[AtUrls.stoptrips("122-34ecc043", LocalDate.of(2026, 9, 20), 1, 3)!!] = HttpResult(404, "")
        at.answers[AtUrls.tripStops("258-880002-85980-2-W201630-da85575f")] = ok(fixture("post-crl/trip_dir1_stops.json"))
        val w = Fetcher(at).refresh(Cache(), train, t0040).watch!!
        assertEquals(WatchState.Ok, w.state)
        assertTrue(w.rows.isNotEmpty())
        assertTrue(w.rows.all { it.schedEpoch >= t0040 - 1800 })
        assertTrue(at.asked.none { "start_hour%5D=0&" in it })
    }

    // --- Kingsland Avenue toward Wynyard Quarter: a pinned bus route ---

    private val bus = WatchConfig("to Wynyard Quarter", "8213", "20", "1060")

    private fun kingslandAvenue() = FakeAt(
        mutableMapOf(
            AtUrls.railRoutes() to ok(fixture("post-crl/routes_rail.json")),
            AtUrls.stopByCode("8213") to ok(fixture("post-crl/stops_8213.json")),
            AtUrls.stopByCode("1060") to ok(fixture("post-crl/stops_1060.json")),
            AtUrls.routesByShortName("20") to ok(fixture("routes_20.json")),
            AtUrls.stoptrips("8213-7e021a72", day, 15, 3)!! to ok(fixture("post-crl/stoptrips_8213.json")),
            AtUrls.tripStops("20-02005-54000-2-89a317b8") to ok(
                stopsJson(Triple("8213-7e021a72", "8213", ""), Triple("1060-00b64ee7", "1060", "")),
            ),
        ),
    )

    @Test fun `a pinned route drops the other routes before any pair is probed`() {
        val at = kingslandAvenue()
        val first = Fetcher(at).refresh(Cache(), bus, T_15_00)
        val w = first.watch!!
        assertEquals(WatchState.Ok, w.state)
        assertEquals(listOf("20-202"), w.routeIds)
        assertEquals(8, w.rows.size) // 22R and 22N never appear
        assertEquals(Badge("20", Kind.Bus, null), w.badge)
        assertEquals(1, at.asked.count { "/trips/" in it }) // one pair, not three
    }

    @Test fun `realtime for trips we never asked about is discarded`() {
        val at = kingslandAvenue()
        val f = Fetcher(at)
        val first = f.refresh(Cache(), bus, T_15_00)
        // The captured feed is from another day: none of its trips are ours.
        realtimeFor(at, first.watch!!.rows.take(RT_PER_WATCH).map { it.tripId }, fixture("realtime_tripupdates_only.json"))
        val w = f.refresh(first, bus, T_15_00 + 30, force = true).watch!!
        assertTrue(w.rows.none { it.hasRt })
        assertEquals(T_15_00 + 30, w.lastOk) // a 200 is news even when it says nothing
    }

    // --- the error surfaces of board spec 8 ---

    @Test fun `an unknown stop code is check settings, retried after a minute`() {
        val at = kingslandAvenue()
        at.answers[AtUrls.stopByCode("8213")] = ok(fixture("post-crl/stops_unknown.json"))
        val f = Fetcher(at)
        val c = f.refresh(Cache(), bus, T_15_00)
        assertEquals(WatchState.CheckConfig, c.watch!!.state)
        at.asked.clear()
        f.refresh(c, bus, T_15_00 + 59)
        assertEquals(emptyList(), at.asked)
        f.refresh(c, bus, T_15_00 + 60)
        assertEquals(AtUrls.stopByCode("8213"), at.asked.first())
    }

    @Test fun `a 400 from the stop lookup means the filter was withdrawn`() {
        val at = kingslandAvenue()
        at.answers[AtUrls.stopByCode("8213")] = HttpResult(400, "")
        assertEquals(WatchState.StopLookupGone, Fetcher(at).refresh(Cache(), bus, T_15_00).watch!!.state)
    }

    @Test fun `a 400 from the route lookup is a config fault instead`() {
        val at = kingslandAvenue()
        at.answers[AtUrls.routesByShortName("20")] = HttpResult(400, "")
        assertEquals(WatchState.CheckConfig, Fetcher(at).refresh(Cache(), bus, T_15_00).watch!!.state)
    }

    @Test fun `a 401 anywhere is check API key, without backing off`() {
        val at = kingslandAvenue()
        at.answers[AtUrls.stopByCode("8213")] = HttpResult(401, "")
        val w = Fetcher(at).refresh(Cache(), bus, T_15_00).watch!!
        assertEquals(WatchState.CheckKey, w.state)
        assertEquals(0, w.backoffS)
        assertEquals(T_15_00 + 60, w.nextResolveAt)
    }

    @Test fun `a changed watch starts over`() {
        val at = kingslandAvenue()
        val f = Fetcher(at)
        val c = f.refresh(Cache(), bus, T_15_00)
        at.asked.clear()
        val other = bus.copy(routeShortName = "")
        val w = f.refresh(c, other, T_15_00 + 5).watch!!
        assertEquals(other.key, w.key)
        assertEquals(AtUrls.stopByCode("8213"), at.asked.first()) // resolved again
    }

    @Test fun `the cache survives the process dying`() {
        val c = Fetcher(kingsland()).refresh(Cache(), train, T_15_00)
        assertEquals(c, Cache.fromJson(c.toJson()))
        assertEquals(Cache(), Cache.fromJson("not json"))
        assertEquals(Cache(), Cache.fromJson(null))
        assertNull(Cache.fromJson("{}").watch)
    }
}
