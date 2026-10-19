package io.github.msmgreen.nextdeparture.core

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class WidgetModelTest {
    private val now = T_15_00
    private val home = Place("Home", WatchConfig("to Waitemata", "122", "", "133"), walkMin = 5)
    private val badge = Badge("E-W", Kind.Train, Rgb(0x97, 0xC9, 0x3D))

    private fun cache(vararg rows: LiveRow, lastOk: Long = now, state: WatchState = WatchState.Ok) =
        WatchCache(home.watch.key, state = state, badge = badge, rows = rows.toList(), lastOk = lastOk)

    private val late = LiveRow("t", "", now + 900, hasRt = true, delay = 120) // 15:15, running 2 late

    @Test fun `a fresh live departure counts down to leave-by`() {
        val v = widgetView(home, cache(late), now, h24 = true)
        assertEquals("Home", v.place)
        assertEquals("E-W to Waitemata", v.title)
        assertEquals(now + 900 + 120 - 300, v.countdownTo)
        assertEquals("15:17 · 2 min late · live", v.detail)
        assertNull(v.message)
        assertNull(v.asOf)
        assertEquals(false, v.stale)
        assertEquals(Rgb(0x97, 0xC9, 0x3D), v.badgeColor)
        assertEquals(now + 121, v.nextRenderAt) // redrawn the moment it goes stale
    }

    @Test fun `stale keeps the last good data but never says live`() {
        val v = widgetView(home, cache(late), now + 200, h24 = true)
        assertEquals("15:17 · 2 min late", v.detail)
        assertEquals("as of 15:00", v.asOf)
        assertEquals(true, v.stale)
        assertEquals(now + 900 + 120 - 300 + 1, v.nextRenderAt) // just after leave-by
    }

    @Test fun `no realtime is marked sched`() {
        val v = widgetView(home, cache(LiveRow("t", "", now + 900)), now, h24 = true)
        assertEquals("15:15 · sched", v.detail)
    }

    @Test fun `early, on time and the 12-hour clock`() {
        val early = LiveRow("t", "", now + 900, hasRt = true, delay = -180)
        assertEquals("3:12pm · 3 min early · live", widgetView(home, cache(early), now, h24 = false).detail)
        val onTime = LiveRow("t", "", now + 900, hasRt = true, delay = 20)
        assertEquals("15:15 · on time · live", widgetView(home, cache(onTime), now, h24 = true).detail)
    }

    @Test fun `a cancellation you would have caught is noted`() {
        val gone = LiveRow("gone", "", now + 600, cancelled = true)
        val v = widgetView(home, cache(gone, late), now, h24 = true)
        assertEquals("15:10 cancelled", v.note)
        assertEquals("15:17 · 2 min late · live", v.detail)
    }

    @Test fun `nothing catchable says so, or asks for a refresh once stale`() {
        val left = LiveRow("t", "", now + 60) // closer than the 5-minute walk
        assertEquals("None in the next 3 h", widgetView(home, cache(left), now, h24 = true).message)
        assertEquals("Tap to refresh", widgetView(home, cache(left, lastOk = now - 600), now, h24 = true).message)
    }

    @Test fun `a state that cannot show times shows its message`() {
        val v = widgetView(home, cache(late, state = WatchState.CheckKey), now, h24 = true)
        assertEquals("Check API key", v.message)
        assertNull(v.countdownTo)
        assertEquals("Starting", widgetView(home, null, now, h24 = true).message)
    }

    @Test fun `the title falls back to the route short name, then the label alone`() {
        val noBadge = cache(late).copy(badge = null)
        assertEquals("to Waitemata", widgetView(home, noBadge, now, h24 = true).title)
        val pinned = home.copy(watch = home.watch.copy(routeShortName = "20", label = "to Wynyard"))
        assertEquals("20 to Wynyard", widgetView(pinned, noBadge, now, h24 = true).title)
    }
}
