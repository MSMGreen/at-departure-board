package io.github.msmgreen.nextdeparture.core

import java.time.Instant
import java.time.LocalDate
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class NzTimeTest {
    @Test fun `parses GTFS times including after midnight`() {
        assertEquals(15 * 3600 + 4 * 60 + 7, parseGtfsTime("15:04:07"))
        assertEquals(25 * 3600 + 600, parseGtfsTime("25:10:00"))
        assertEquals(0, parseGtfsTime("00:00:00"))
        assertNull(parseGtfsTime(""))
        assertNull(parseGtfsTime("15:04"))
        assertNull(parseGtfsTime("aa:bb:cc"))
        assertNull(parseGtfsTime("48:00:00"))
        assertNull(parseGtfsTime("15:60:00"))
    }

    @Test fun `parses ISO dates`() {
        assertEquals(LocalDate.of(2026, 9, 13), parseIsoDate("2026-09-13"))
        assertNull(parseIsoDate("2026-13-01"))
        assertNull(parseIsoDate("nope"))
    }

    // The values lib/core's test_nztime checks, so the two implementations agree.
    @Test fun `GTFS times count from noon minus twelve hours`() {
        assertEquals(1789268647, gtfsEpoch(LocalDate.of(2026, 9, 13), 15 * 3600 + 4 * 60 + 7))
        // 25:10 on the 19th is 01:10 on the 20th.
        assertEquals(1789823400, gtfsEpoch(LocalDate.of(2026, 9, 19), 25 * 3600 + 600))
        // On the day DST starts, noon is still noon...
        assertEquals(1790463600, gtfsEpoch(LocalDate.of(2026, 9, 27), 12 * 3600))
        // ...but 01:00 is 00:00, because an hour of that day does not exist.
        assertEquals(1790424000, gtfsEpoch(LocalDate.of(2026, 9, 27), 3600))
        // And on the day it ends, 01:00 is 02:00 for the same reason in reverse.
        assertEquals(1806757200, gtfsEpoch(LocalDate.of(2027, 4, 4), 3600))
    }

    // nztime.cpp carries the DST rules by hand; java.time reads them from the
    // tz database. Pin the database to the board's boundaries.
    @Test fun `the tz database agrees with the board's DST rules`() {
        fun offset(epoch: Long) = AKL.rules.getOffset(Instant.ofEpochSecond(epoch)).totalSeconds
        assertEquals(43200, offset(1790431199)) // 2026-09-27 01:59:59 NZST
        assertEquals(46800, offset(1790431200)) // 03:00:00 NZDT
        assertEquals(46800, offset(1806760799)) // 2027-04-04 02:59:59 NZDT
        assertEquals(43200, offset(1806760800)) // 02:00:00 NZST
    }

    @Test fun `formats the clock both ways`() {
        assertEquals("17:42", formatClock(1789796520, h24 = true))
        assertEquals("5:42pm", formatClock(1789796520, h24 = false))
        assertEquals("7:05", formatClock(1789758300, h24 = true))
        assertEquals("7:05am", formatClock(1789758300, h24 = false))
        assertEquals("12:00pm", formatClock(1789776000, h24 = false))
        assertEquals("12:00am", formatClock(1789732800, h24 = false))
    }
}
