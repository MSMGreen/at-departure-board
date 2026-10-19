package io.github.msmgreen.nextdeparture.core

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class FreshnessTest {
    @Test fun `stale after two minutes, and not at two minutes`() {
        assertFalse(isStale(T_15_00, T_15_00 + 120))
        assertTrue(isStale(T_15_00, T_15_00 + 121))
        assertEquals(T_15_00 + 121, staleAt(T_15_00))
    }

    @Test fun `never fetched is stale`() {
        assertTrue(isStale(0, T_15_00))
    }

    @Test fun `news on a fresh schedule moves last ok`() {
        assertEquals(T_15_00, lastOkAfter(previous = 1, now = T_15_00, news = true, schedOkAt = T_15_00 - 600))
    }

    @Test fun `no news leaves last ok where it was`() {
        assertEquals(1L, lastOkAfter(previous = 1, now = T_15_00, news = false, schedOkAt = T_15_00))
    }

    @Test fun `realtime on a sixteen-minute-old schedule is still stale`() {
        // The schedule refresh failed and realtime answered: the rows may be
        // missing services, so the widget must not claim to be fresh.
        val before = T_15_00 - 300
        val after = lastOkAfter(previous = before, now = T_15_00, news = true, schedOkAt = T_15_00 - 16 * 60)
        assertEquals(before, after)
        assertTrue(isStale(after, T_15_00))
    }

    @Test fun `a schedule that never arrived is no basis for freshness`() {
        assertEquals(0L, lastOkAfter(previous = 0, now = T_15_00, news = true, schedOkAt = 0))
    }
}
