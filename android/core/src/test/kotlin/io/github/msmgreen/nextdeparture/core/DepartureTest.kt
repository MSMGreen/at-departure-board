package io.github.msmgreen.nextdeparture.core

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class DepartureTest {
    private val now = T_15_00

    private fun row(trip: String, sched: Long, delay: Int? = null, cancelled: Boolean = false) =
        LiveRow(trip, "", sched, hasRt = delay != null, delay = delay ?: 0, cancelled = cancelled)

    @Test fun `leave-by exactly now is catchable, a second earlier is not`() {
        assertEquals("a", chooseDeparture(listOf(row("a", now + 300)), now, walkS = 300).departure?.row?.tripId)
        assertNull(chooseDeparture(listOf(row("a", now + 299)), now, walkS = 300).departure)
    }

    @Test fun `an early-running bus moves leave-by earlier`() {
        val d = chooseDeparture(listOf(row("a", now + 600, delay = -180)), now, walkS = 300).departure!!
        assertEquals(now + 120, d.leaveBy)
    }

    @Test fun `order is by when it will actually leave, not by timetable`() {
        val rows = listOf(row("late", now + 600, delay = 600), row("next", now + 900))
        assertEquals("next", chooseDeparture(rows, now, walkS = 0).departure!!.row.tripId)
    }

    @Test fun `a row without realtime uses its schedule`() {
        val d = chooseDeparture(listOf(row("a", now + 600)), now, walkS = 60).departure!!
        assertEquals(now + 540, d.leaveBy)
    }

    @Test fun `the cancelled service you would have caught is reported`() {
        val rows = listOf(row("gone", now + 300, cancelled = true), row("next", now + 600))
        val c = chooseDeparture(rows, now, walkS = 0)
        assertEquals("next", c.departure!!.row.tripId)
        assertEquals("gone", c.cancelled!!.tripId)
    }

    @Test fun `a cancellation after the one you catch is not`() {
        val rows = listOf(row("next", now + 300), row("gone", now + 600, cancelled = true))
        assertNull(chooseDeparture(rows, now, walkS = 0).cancelled)
    }

    @Test fun `a cancellation you could not have reached anyway is not`() {
        val rows = listOf(row("gone", now + 60, cancelled = true), row("next", now + 600))
        assertNull(chooseDeparture(rows, now, walkS = 300).cancelled)
    }

    @Test fun `a late bus is still the one to catch`() {
        // The board's late-bus case: scheduled 15:00, +300, now 15:02.
        val rows = listOf(row("late", now, delay = 300), row("next", now + 1200))
        val d = chooseDeparture(rows, now + 120, walkS = 0).departure!!
        assertEquals("late", d.row.tripId)
        assertEquals(now + 300, d.leaveBy)
    }

    @Test fun `nothing reachable is no departure`() {
        assertEquals(Choice(null, null), chooseDeparture(listOf(row("a", now - 60)), now, walkS = 0))
        assertEquals(Choice(null, null), chooseDeparture(emptyList(), now, walkS = 0))
    }
}
