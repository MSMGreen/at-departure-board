package io.github.msmgreen.nextdeparture.core

import java.time.LocalDate
import java.time.LocalDateTime
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

// test/test_live, case for case. build_board, select_rows and choose_direction
// are not ported: the widget has no board, and the fetcher never used the other
// two once direction became per (route, direction) pair.
class LiveTest {
    private fun stop(id: String, code: String, parent: String) = TripStop(id, code, parent)

    private fun row(trip: String, route: String, dir: Int, secs: Int) =
        StopTripRow(trip, route, "9305-ef07ca76", dir, LocalDate.of(2026, 9, 13), secs)

    private fun live(trip: String, sched: Long, hasRt: Boolean = false, delay: Int = 0, stopId: String = "") =
        LiveRow(trip, stopId, sched, hasRt, delay)

    @Test fun `a trip serves a target that comes after us`() {
        // The real dir-1 trip: Kingsland platform 9304, then Waitemata platform
        // 9001, whose parent is station 133.
        val stops = listOf(
            stop("9328-f6f84eac", "9328", "127-2affe079"),
            stop("9304-dcb2ed75", "9304", "122-34ecc043"),
            stop("9001-11111111", "9001", "133-08da14b5"),
        )
        assertTrue(tripServes(stops, "9304-dcb2ed75", "133", "133-08da14b5"))
    }

    @Test fun `a trip going the other way does not serve it`() {
        val stops = listOf(
            stop("9004-22222222", "9004", "133-08da14b5"),
            stop("9305-ef07ca76", "9305", "122-34ecc043"),
            stop("9328-f6f84eac", "9328", "127-2affe079"),
        )
        assertFalse(tripServes(stops, "9305-ef07ca76", "133", "133-08da14b5"))
    }

    @Test fun `a bus target matches on stop code alone`() {
        val stops = listOf(stop("8213-7e021a72", "8213", ""), stop("1060-00b64ee7", "1060", ""))
        assertTrue(tripServes(stops, "8213-7e021a72", "1060", "1060-00b64ee7"))
    }

    @Test fun `a trip that never calls at our stop serves nothing`() {
        val stops = listOf(stop("1111-aaaa", "1111", ""), stop("1060-00b64ee7", "1060", ""))
        assertFalse(tripServes(stops, "8213-7e021a72", "1060", "1060-00b64ee7"))
    }

    @Test fun `route dirs are collected per route and direction`() {
        val rows = listOf(
            row("ew0", "E-W-201", 0, 15 * 3600),
            row("ow0", "O-W-201", 0, 15 * 3600 + 60),
            row("ew1", "E-W-201", 1, 15 * 3600 + 120),
            row("ow1", "O-W-201", 1, 15 * 3600 + 180),
            row("ew0b", "E-W-201", 0, 15 * 3600 + 240), // same pair again
        )
        val pairs = collectRouteDirs(rows)
        assertEquals(4, pairs.size)
        assertEquals(RouteDir("E-W-201", 0, "ew0", "9305-ef07ca76"), pairs[0]) // first trip of that pair
        assertEquals("O-W-201", pairs[1].routeId)
        assertEquals("ew1", pairs[2].tripId)
    }

    @Test fun `the pair cap keeps the first pairs seen`() {
        val rows = (0 until 8).map { row("t$it", "R$it-1", 0, 15 * 3600 + it) }
        assertEquals(listOf("R0-1", "R1-1", "R2-1", "R3-1", "R4-1", "R5-1"), collectRouteDirs(rows).map { it.routeId })
    }

    @Test fun `only the serving line is shown`() {
        // The bug: O-W does not reach Waitemata, E-W does. Rows of the line that
        // does not go your way must never appear.
        val rows = listOf(row("ow1", "O-W-201", 1, 15 * 3600 + 60), row("ew1", "E-W-201", 1, 15 * 3600 + 120))
        val pairs = collectRouteDirs(rows).map { it.copy(serves = it.routeId == "E-W-201") }
        assertEquals(listOf("ew1"), selectServingRows(rows, pairs, emptyList(), T_15_00).map { it.tripId })
    }

    @Test fun `a pinned route filters the serving rows`() {
        val rows = listOf(
            row("a", "E-W-201", 1, 15 * 3600 + 13 * 60),
            row("c", "O-W-201", 1, 15 * 3600 + 20 * 60),
            row("d", "E-W-201", 1, 15 * 3600 + 30 * 60),
        )
        val pairs = collectRouteDirs(rows).map { it.copy(serves = true) }
        val out = selectServingRows(rows, pairs, listOf("E-W-201"), T_15_00)
        assertEquals(listOf("a", "d"), out.map { it.tripId })
        assertEquals(T_15_00 + 13 * 60, out[0].schedEpoch)
    }

    @Test fun `recent departures are kept because delays can be large`() {
        // Scheduled 10 minutes ago, but a bus can run 7 minutes late; dropping
        // it here would lose a service that is still to come.
        val rows = listOf(row("a", "E-W-201", 1, 14 * 3600 + 50 * 60), row("b", "E-W-201", 1, 13 * 3600))
        val pairs = collectRouteDirs(rows).map { it.copy(serves = true) }
        assertEquals(listOf("a"), selectServingRows(rows, pairs, emptyList(), T_15_00).map { it.tripId })
    }

    @Test fun `verdict is check config only when a route runs both ways in vain`() {
        val pairs = listOf(RouteDir("O-W-201", 0, "x", "s"), RouteDir("O-W-201", 1, "y", "s"))
        // One line, both directions, neither reaches the target: the target is
        // wrong for this stop.
        assertEquals(WatchState.CheckConfig, verdictForPairs(pairs))
        // One line, one direction, not reaching it: nothing is due your way.
        assertEquals(WatchState.Ok, verdictForPairs(pairs.take(1)))
        // Anything serving at all is Ok.
        assertEquals(WatchState.Ok, verdictForPairs(listOf(pairs[0], pairs[1].copy(serves = true))))
    }

    @Test fun `no pairs at all is an empty window`() {
        assertEquals(WatchState.Ok, verdictForPairs(emptyList()))
    }

    @Test fun `realtime prefers our own stop time update`() {
        val rows = listOf(live("t1", 0, stopId = "9304-dcb2ed75"), live("t2", 0, stopId = "9304-dcb2ed75"))
        val ents = listOf(
            RtEntity("t1", -28, false, "9304-dcb2ed75", 66),
            RtEntity("t2", -427, false, "1060-00b64ee7", 999),
            RtEntity("stranger", 12345, false, "", null), // the tripid filter is inexact
        )
        val out = applyRealtime(rows, ents, listOf("t1", "t2"))
        assertTrue(out[0].hasRt)
        assertEquals(66, out[0].delay) // our stop wins
        assertEquals(-427, out[1].delay) // someone else's is ignored
    }

    @Test fun `realtime marks cancellations`() {
        val out = applyRealtime(listOf(live("t1", 0)), listOf(RtEntity("t1", null, true, "", null)), listOf("t1"))
        assertTrue(out[0].cancelled)
    }

    @Test fun `a requested trip the feed forgets stops being live`() {
        val rows = listOf(
            LiveRow("asked", "", 0, hasRt = true, delay = 240, cancelled = true),
            LiveRow("not-asked", "", 0, hasRt = true, delay = 60),
        )
        val out = applyRealtime(rows, listOf(RtEntity("someone-else", 12, false, "", null)), listOf("asked"))
        assertEquals(LiveRow("asked", "", 0), out[0]) // no longer claimed as live
        assertEquals(rows[1], out[1]) // never asked about: untouched
    }

    @Test fun `realtime ids ask only about services still to come`() {
        val rows = listOf(live("gone", T_15_00 - 3600), live("soon", T_15_00 + 300), live("later", T_15_00 + 900))
        assertEquals(listOf("soon", "later"), realtimeIds(rows, T_15_00))
        assertEquals(listOf("soon"), realtimeIds(rows, T_15_00, cap = 1))
    }

    @Test fun `a late bus survives a schedule refresh`() {
        // Scheduled 15:00, realtime says +300. At 15:02 the 15-minute schedule
        // refresh rebuilds the rows. The bus is still 3 minutes out and must stay.
        val rows = listOf(row("late", "20-202", 0, 15 * 3600), row("next", "20-202", 0, 15 * 3600 + 1200))
        val pairs = collectRouteDirs(rows).map { it.copy(serves = true) }
        var w = selectServingRows(rows, pairs, emptyList(), T_15_00 - 600)
        assertEquals(2, w.size)
        w = applyRealtime(w, listOf(RtEntity("late", 300, false, "", null)), listOf("late", "next"))
        assertTrue(w[0].hasRt)

        val now = T_15_00 + 120
        w = carryRealtime(w, selectServingRows(rows, pairs, emptyList(), now))
        assertTrue(w[0].hasRt)
        assertEquals(300, w[0].delay)
        assertFalse(w[1].hasRt) // nothing was said about it
        assertEquals(listOf("late", "next"), realtimeIds(w, now))
        assertEquals(180L, w[0].predicted - now) // still three minutes out
    }

    @Test fun `carry realtime matches by trip id, not position`() {
        val old = listOf(LiveRow("a", "", 0), LiveRow("b", "", 0, hasRt = true, delay = -45, cancelled = true))
        val out = carryRealtime(old, listOf(LiveRow("b", "", 0), LiveRow("c", "", 0)))
        assertEquals(LiveRow("b", "", 0, hasRt = true, delay = -45, cancelled = true), out[0])
        assertEquals(LiveRow("c", "", 0), out[1]) // a new trip starts with no realtime
    }

    @Test fun `a never-heard-of row five minutes past is still asked about`() {
        assertEquals(listOf("maybe-late"), realtimeIds(listOf(live("maybe-late", T_15_00 - 300)), T_15_00))
    }

    @Test fun `a never-heard-of row twenty minutes past is not`() {
        assertEquals(emptyList(), realtimeIds(listOf(live("gone", T_15_00 - 1200)), T_15_00))
    }

    @Test fun `a heard-of row that has left is not asked about`() {
        // Left three minutes ago.
        assertEquals(emptyList(), realtimeIds(listOf(live("left", T_15_00 - 300, hasRt = true, delay = 120)), T_15_00))
    }

    private fun at(y: Int, m: Int, d: Int, hour: Int) = LocalDateTime.of(y, m, d, hour, 0).atZone(AKL)

    private fun window(y: Int, m: Int, d: Int, start: Int) = FetchWindow(LocalDate.of(y, m, d), start, 3)

    @Test fun `schedule windows cover three hours from now`() {
        assertEquals(listOf(window(2026, 9, 19, 17)), scheduleWindows(at(2026, 9, 19, 17)))
    }

    @Test fun `schedule windows run past midnight on the same service date`() {
        // The API does not clamp at midnight: 23:00 for three hours returns
        // 23:04 through 25:25 against one date (verified live 2026-09-19).
        assertEquals(listOf(window(2026, 9, 19, 23)), scheduleWindows(at(2026, 9, 19, 23)))
    }

    @Test fun `schedule windows at midnight ask yesterday for its late services`() {
        assertEquals(listOf(window(2026, 9, 19, 24), window(2026, 9, 20, 1)), scheduleWindows(at(2026, 9, 20, 0)))
    }

    @Test fun `schedule windows in the small hours`() {
        assertEquals(listOf(window(2026, 9, 19, 26), window(2026, 9, 20, 2)), scheduleWindows(at(2026, 9, 20, 2)))
        // From 04:00 one window is enough again.
        assertEquals(listOf(window(2026, 9, 20, 4)), scheduleWindows(at(2026, 9, 20, 4)))
    }

    @Test fun `schedule windows roll back over a month and a year end`() {
        assertEquals(listOf(window(2026, 9, 30, 24), window(2026, 10, 1, 1)), scheduleWindows(at(2026, 10, 1, 0)))
        assertEquals(listOf(window(2026, 12, 31, 25), window(2027, 1, 1, 1)), scheduleWindows(at(2027, 1, 1, 1)))
    }

    @Test fun `no hour of the day ever asks for start hour zero`() {
        for (h in 0..23) {
            for (w in scheduleWindows(at(2026, 9, 20, h))) {
                assertTrue(w.startHour in 1..47, "hour $h asked for ${w.startHour}")
            }
        }
    }

    @Test fun `badges come from the rail table or the route id`() {
        val rail = AtParse.routes(fixture("post-crl/routes_rail.json"))!!
        assertEquals(Badge("E-W", Kind.Train, Rgb(0x97, 0xC9, 0x3D)), badgeFor("E-W-201", rail))
        assertEquals(Badge("20", Kind.Bus, null), badgeFor("20-202", rail))
    }

    @Test fun `state messages`() {
        assertEquals("", stateMessage(WatchState.Ok))
        assertEquals("Check settings", stateMessage(WatchState.CheckConfig))
        assertEquals("Check API key", stateMessage(WatchState.CheckKey))
        assertEquals("Starting", stateMessage(WatchState.Starting))
        assertEquals("Stop lookup gone", stateMessage(WatchState.StopLookupGone))
    }
}
