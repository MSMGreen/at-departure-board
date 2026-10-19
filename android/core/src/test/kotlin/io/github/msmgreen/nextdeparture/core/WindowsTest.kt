package io.github.msmgreen.nextdeparture.core

import java.time.ZonedDateTime
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class WindowsTest {
    private fun at(y: Int, mo: Int, d: Int, h: Int, mi: Int, s: Int = 0) =
        ZonedDateTime.of(y, mo, d, h, mi, s, 0, AKL).toEpochSecond()

    private val morning = CommuteWindow(start = "07:30", end = "08:30", defaultPlace = 0)
    private val evening = CommuteWindow(start = "16:00", end = "18:00", defaultPlace = 1)
    private val both = listOf(morning, evening)

    // 2026-10-19 is a Monday; 2026-10-24 a Saturday.
    @Test fun `open from start until end, on the days in the mask`() {
        assertEquals(morning, activeWindow(both, at(2026, 10, 19, 7, 30)))
        assertEquals(morning, activeWindow(both, at(2026, 10, 19, 8, 29, 59)))
        assertNull(activeWindow(both, at(2026, 10, 19, 8, 30)))
        assertNull(activeWindow(both, at(2026, 10, 19, 7, 29, 59)))
        assertEquals(evening, activeWindow(both, at(2026, 10, 19, 17, 0)))
        assertNull(activeWindow(both, at(2026, 10, 24, 7, 45))) // Saturday
        assertEquals(morning.copy(days = 0b0100000), activeWindow(listOf(morning.copy(days = 0b0100000)), at(2026, 10, 24, 7, 45)))
    }

    @Test fun `no windows, never open`() {
        assertNull(activeWindow(emptyList(), at(2026, 10, 19, 8, 0)))
        assertNull(nextBoundary(emptyList(), at(2026, 10, 19, 8, 0)))
    }

    @Test fun `Not today closes every window until it runs out`() {
        val quiet = endOfLocalDay(at(2026, 10, 19, 6, 0))
        assertNull(activeWindow(both, at(2026, 10, 19, 7, 45), quiet))
        assertNull(activeWindow(both, at(2026, 10, 19, 17, 0), quiet))
        assertEquals(morning, activeWindow(both, at(2026, 10, 20, 7, 45), quiet)) // and no longer
    }

    @Test fun `the end of the local day is the next local midnight`() {
        assertEquals(at(2026, 10, 20, 0, 0), endOfLocalDay(at(2026, 10, 19, 23, 59)))
        assertEquals(at(2026, 10, 20, 0, 0), endOfLocalDay(at(2026, 10, 19, 0, 0)))
        // 27 September 2026 is 23 hours long: NZDT starts at 02:00.
        assertEquals(at(2026, 9, 27, 0, 30) + 22 * 3600 + 1800, endOfLocalDay(at(2026, 9, 27, 0, 30)))
    }

    @Test fun `the next boundary is strictly later`() {
        assertEquals(at(2026, 10, 19, 7, 30), nextBoundary(both, at(2026, 10, 19, 6, 0)))
        assertEquals(at(2026, 10, 19, 8, 30), nextBoundary(both, at(2026, 10, 19, 7, 30))) // at a start: its end
        assertEquals(at(2026, 10, 19, 16, 0), nextBoundary(both, at(2026, 10, 19, 8, 30)))
    }

    @Test fun `the next boundary after Friday evening is Monday morning`() {
        assertEquals(at(2026, 10, 26, 7, 30), nextBoundary(both, at(2026, 10, 23, 18, 0)))
    }

    @Test fun `a window on one day of the week is found a week ahead`() {
        val monday = listOf(morning.copy(days = 0b0000001))
        assertEquals(at(2026, 10, 26, 7, 30), nextBoundary(monday, at(2026, 10, 19, 8, 30)))
    }

    @Test fun `overlapping windows hand over at each boundary`() {
        val early = CommuteWindow(start = "07:00", end = "09:00")
        val late = CommuteWindow(start = "08:00", end = "10:00")
        val w = listOf(early, late)
        assertEquals(at(2026, 10, 19, 8, 0), nextBoundary(w, at(2026, 10, 19, 7, 0)))
        assertEquals(at(2026, 10, 19, 9, 0), nextBoundary(w, at(2026, 10, 19, 8, 0)))
        assertEquals(late, activeWindow(w, at(2026, 10, 19, 9, 0))) // still open after the first ends
        assertEquals(at(2026, 10, 19, 10, 0), nextBoundary(w, at(2026, 10, 19, 9, 0)))
    }

    @Test fun `across the start of NZDT, local times stay local`() {
        // Friday 25 September 2026, NZST; Monday 28 September, NZDT.
        val next = nextBoundary(both, at(2026, 9, 25, 18, 0))!!
        assertEquals(at(2026, 9, 28, 7, 30), next)
        assertEquals("07:30", "%02d:%02d".format(local(next).hour, local(next).minute))
    }

    @Test fun `across the end of NZDT, local times stay local`() {
        // Friday 2 April 2027, NZDT; Monday 5 April, NZST.
        val next = nextBoundary(both, at(2027, 4, 2, 18, 0))!!
        assertEquals(at(2027, 4, 5, 7, 30), next)
        assertEquals(7, local(next).hour)
    }

    @Test fun `a start in the spring-forward gap is late, never lost`() {
        // 02:30 on 27 September 2026 does not exist; it becomes 03:30 NZDT.
        val sunday = listOf(CommuteWindow(days = 0b1000000, start = "02:30", end = "04:00"))
        assertEquals(at(2026, 9, 27, 3, 30), nextBoundary(sunday, at(2026, 9, 26, 12, 0)))
        assertNull(activeWindow(sunday, at(2026, 9, 27, 3, 15)))
        assertEquals(sunday[0], activeWindow(sunday, at(2026, 9, 27, 3, 45)))
    }
}
