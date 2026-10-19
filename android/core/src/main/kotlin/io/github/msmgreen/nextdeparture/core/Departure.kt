package io.github.msmgreen.nextdeparture.core

// The one rule the board does not have. The board shows four lanes and lets you
// judge; a one-line widget has to judge for you (widget spec 5).

data class Departure(val row: LiveRow, val leaveBy: Long)

// cancelled is the cancelled service you would otherwise have caught, so the
// widget can say so rather than silently showing a later one (board spec 8:
// cancellations are surfaced, never dropped).
data class Choice(val departure: Departure?, val cancelled: LiveRow?)

// The first row you can still reach, ordered by when it will actually leave
// rather than by timetable: a bus running early leaves sooner than its slot.
fun chooseDeparture(rows: List<LiveRow>, now: Long, walkS: Int): Choice {
    val reachable = rows.filter { it.predicted - walkS >= now }.sortedBy { it.predicted }
    val next = reachable.firstOrNull { !it.cancelled }
    val cancelled = reachable.firstOrNull()?.takeIf { it.cancelled }
    return Choice(next?.let { Departure(it, it.predicted - walkS) }, cancelled)
}
