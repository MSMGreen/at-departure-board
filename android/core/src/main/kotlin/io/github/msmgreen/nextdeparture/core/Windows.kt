package io.github.msmgreen.nextdeparture.core

import java.time.LocalDate
import java.time.LocalTime

// Commute windows as pure functions of the clock (widget spec 9), so that the
// boundaries are tested across both DST changes on a laptop. Times are local
// wall-clock times, and a window never crosses midnight, so the window open at
// any moment is one of that local day's.

private fun CommuteWindow.on(date: LocalDate): Boolean = days and (1 shl (date.dayOfWeek.value - 1)) != 0

// A wall-clock time on a date, as an epoch. In the spring-forward gap java.time
// moves the time on by the gap's length, so a 02:30 start becomes 03:30: late,
// never lost. In the autumn overlap it takes the earlier of the two.
private fun epochAt(date: LocalDate, t: LocalTime): Long = date.atTime(t).atZone(AKL).toEpochSecond()

// The window open at now, if any. quietUntil is Not today's end: until then no
// window is open.
fun activeWindow(windows: List<CommuteWindow>, now: Long, quietUntil: Long = 0): CommuteWindow? {
    if (now < quietUntil) return null
    val date = local(now).toLocalDate()
    return windows.firstOrNull { w ->
        w.on(date) && now >= epochAt(date, w.startTime) && now < epochAt(date, w.endTime)
    }
}

// The first window start or end strictly after now, looking a week ahead; null
// when there are no windows. Strictly after, so an alarm that fires exactly on
// a boundary arms the next one rather than itself again.
fun nextBoundary(windows: List<CommuteWindow>, now: Long): Long? {
    val today = local(now).toLocalDate()
    return (0L..7L).asSequence()
        .map { today.plusDays(it) }
        .flatMap { date -> windows.filter { it.on(date) }.flatMap { listOf(epochAt(date, it.startTime), epochAt(date, it.endTime)) } }
        .filter { it > now }
        .minOrNull()
}

// The next local midnight: when Not today runs out.
fun endOfLocalDay(now: Long): Long = local(now).toLocalDate().plusDays(1).atStartOfDay(AKL).toEpochSecond()
