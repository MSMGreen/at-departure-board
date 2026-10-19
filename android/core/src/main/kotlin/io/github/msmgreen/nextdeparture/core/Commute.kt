package io.github.msmgreen.nextdeparture.core

// Commute mode's decisions (widget spec 7, 9), made here so they are tested on
// a laptop: which place is current, what the notification says, and how long
// the screen-on loop waits.

// Widget spec 7, rules 3 and 4: the open window's default place, else the first
// place. Not today closes the windows, so a day at home shows home. Rules 1 and
// 2 - the manual flip and the place most recently entered - come with
// geofences in phase 3, ahead of these.
fun currentPlace(c: Config, now: Long, quietUntil: Long = 0): Place =
    activeWindow(c.windows, now, quietUntil)?.let { c.places.getOrNull(it.defaultPlace) } ?: c.places.first()

data class CommuteNotice(
    val title: String, // "Leave in 4 min · 20 to Wynyard Quarter"
    val text: String, // "7:42 · 2 min late · live as of 7:31"
    val countdownTo: Long?, // leave-by, for the notification's chronometer
)

// The notification does the widget's job on the lock screen, so it says what
// the widget says. Its title is only redrawn on a fetch, so it counts whole
// minutes; the chronometer beside it has the seconds.
fun commuteNotice(v: WidgetView, lastOk: Long, now: Long, h24: Boolean): CommuteNotice {
    val to = v.countdownTo
    val lead = when {
        to == null -> v.message ?: ""
        to - now < 60 -> "Leave now"
        else -> "Leave in ${(to - now) / 60} min"
    }
    val asOf = if (lastOk > 0) "as of ${formatClock(lastOk, h24)}" else null
    // "as of" belongs to a departure; a message such as "Check settings" has none.
    val line = if (v.detail.isEmpty()) "" else listOfNotNull(v.detail, asOf).joinToString(" ")
    return CommuteNotice(
        title = listOf(lead, v.title).filter { it.isNotEmpty() }.joinToString(" · "),
        text = listOfNotNull(line.ifEmpty { null }, v.note).joinToString(" · "),
        countdownTo = to,
    )
}

// How long the screen-on loop waits before its next fetch (widget spec 9):
// RT_FAST_S, or until the backoff runs out; null to stop, because a 401 will
// not mend itself in 30 s and waits for the next window.
fun commuteDelay(w: WatchCache?, now: Long): Long? {
    if (w?.state == WatchState.CheckKey) return null
    return maxOf(RT_FAST_S.toLong(), (w?.backoffUntil ?: 0) - now)
}
