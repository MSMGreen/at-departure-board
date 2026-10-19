package io.github.msmgreen.nextdeparture.core

import java.time.ZonedDateTime
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class CommuteTest {
    private fun at(y: Int, mo: Int, d: Int, h: Int, mi: Int, s: Int = 0) =
        ZonedDateTime.of(y, mo, d, h, mi, s, 0, AKL).toEpochSecond()

    private val home = Place("Home", WatchConfig("to Wynyard Quarter", "8213", "20", "1060"), walkMin = 4)
    private val work = Place("Work", WatchConfig("home", "1060", "20", "8213"), walkMin = 2)
    private val config = Config(
        places = listOf(home, work),
        windows = listOf(
            CommuteWindow(start = "07:30", end = "08:30", defaultPlace = 0),
            CommuteWindow(start = "16:00", end = "18:00", defaultPlace = 1),
        ),
    )

    // Monday 19 October 2026.
    @Test fun `a window shows its default place`() {
        assertEquals(work, currentPlace(config, at(2026, 10, 19, 17, 0)))
        assertEquals(home, currentPlace(config, at(2026, 10, 19, 7, 45)))
    }

    @Test fun `outside every window, the first place`() {
        assertEquals(home, currentPlace(config, at(2026, 10, 19, 12, 0)))
        assertEquals(home, currentPlace(config, at(2026, 10, 19, 18, 0)))
        assertEquals(home, currentPlace(config.copy(windows = emptyList()), at(2026, 10, 19, 17, 0)))
    }

    @Test fun `Not today means home all day`() {
        val quiet = endOfLocalDay(at(2026, 10, 19, 6, 0))
        assertEquals(home, currentPlace(config, at(2026, 10, 19, 17, 0), quiet))
    }

    private val now = at(2026, 10, 19, 7, 31)

    private fun view(
        countdownTo: Long? = now + 4 * 60 + 12,
        message: String? = null,
        detail: String = "7:42 · 2 min late · live",
        note: String? = null,
        asOf: String? = null,
        stale: Boolean = false,
    ) = WidgetView("Home", "20 to Wynyard Quarter", countdownTo, message, detail, note, asOf, stale, null, null)

    @Test fun `the title counts whole minutes to leave-by`() {
        assertEquals("Leave in 4 min · 20 to Wynyard Quarter", commuteNotice(view(), now, now, true).title)
        assertEquals("Leave in 1 min · 20 to Wynyard Quarter", commuteNotice(view(countdownTo = now + 60), now, now, true).title)
        assertEquals("Leave now · 20 to Wynyard Quarter", commuteNotice(view(countdownTo = now + 59), now, now, true).title)
    }

    @Test fun `the text says how old the data is, live or not`() {
        val n = commuteNotice(view(), at(2026, 10, 19, 7, 31), now, true)
        assertEquals("7:42 · 2 min late · live as of 7:31", n.text)
        assertEquals(now + 4 * 60 + 12, n.countdownTo)
    }

    @Test fun `with nothing to count down to, the message leads`() {
        val n = commuteNotice(view(countdownTo = null, message = "Check settings", detail = ""), 0, now, true)
        assertEquals("Check settings · 20 to Wynyard Quarter", n.title)
        assertEquals("", n.text)
        assertNull(n.countdownTo)
    }

    @Test fun `a cancellation is surfaced`() {
        val n = commuteNotice(view(note = "7:36 cancelled"), at(2026, 10, 19, 7, 31), now, true)
        assertEquals("7:42 · 2 min late · live as of 7:31 · 7:36 cancelled", n.text)
    }

    @Test fun `the loop waits 30 s, longer while backing off, and stops on a 401`() {
        assertEquals(30L, commuteDelay(null, now))
        assertEquals(30L, commuteDelay(WatchCache("k", state = WatchState.Ok), now))
        assertEquals(120L, commuteDelay(WatchCache("k", backoffS = 120, backoffUntil = now + 120), now))
        assertEquals(30L, commuteDelay(WatchCache("k", backoffS = 60, backoffUntil = now - 5), now))
        assertNull(commuteDelay(WatchCache("k", state = WatchState.CheckKey), now))
    }
}
