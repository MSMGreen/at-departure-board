package io.github.msmgreen.nextdeparture.core

import kotlin.math.abs

// What the widget shows, decided here so that every word of it is tested on a
// laptop. The app only maps a WidgetView onto RemoteViews (widget spec 6).

data class WidgetView(
    val place: String, // "Home"
    val title: String, // "20 to Wynyard Quarter"
    val countdownTo: Long?, // leave-by epoch for the Chronometer; null with a message
    val message: String?, // shown instead of a countdown
    val detail: String, // "7:42 · 2 min late · live"
    val note: String?, // "7:36 cancelled"
    val asOf: String?, // "as of 7:31" once stale; null while fresh
    val stale: Boolean, // dim everything
    val badgeColor: Rgb?,
    val nextRenderAt: Long?, // when this view stops being true without a fetch
)

private fun delayText(delay: Int): String {
    val m = (abs(delay) + 30) / 60
    return when {
        m == 0 -> "on time"
        delay > 0 -> "$m min late"
        else -> "$m min early"
    }
}

fun widgetView(place: Place, w: WatchCache?, now: Long, h24: Boolean): WidgetView {
    val title = listOf(w?.badge?.text ?: place.watch.routeShortName, place.watch.label)
        .filter { it.isNotEmpty() }
        .joinToString(" ")
    val lastOk = w?.lastOk ?: 0L
    val stale = isStale(lastOk, now)
    val asOf = if (stale && lastOk > 0) "as of ${formatClock(lastOk, h24)}" else null
    // A fresh view must be redrawn the moment it goes stale, so "live" never
    // outlasts its evidence.
    val staleRender = if (stale) null else staleAt(lastOk)

    fun message(text: String, note: String? = null) = WidgetView(
        place.name, title, null, text, "", note, asOf, stale, w?.badge?.color, staleRender,
    )

    if (w == null) return message(stateMessage(WatchState.Starting))
    if (w.state != WatchState.Ok) return message(stateMessage(w.state))

    val choice = chooseDeparture(w.rows, now, place.walkMin * 60)
    val note = choice.cancelled?.let { "${formatClock(it.schedEpoch, h24)} cancelled" }
    val d = choice.departure ?: return message(if (stale) "Tap to refresh" else "None in the next 3 h", note)

    val marker = when {
        !d.row.hasRt -> "sched"
        !stale -> "live"
        else -> null // the old delay is still the best guess, but never "live"
    }
    val detail = listOfNotNull(
        formatClock(d.row.predicted, h24),
        if (d.row.hasRt) delayText(d.row.delay) else null,
        marker,
    ).joinToString(" · ")
    // Redraw just after leave-by, when the next catchable departure takes over.
    val next = listOfNotNull(d.leaveBy + 1, staleRender).min()
    return WidgetView(place.name, title, d.leaveBy, null, detail, note, asOf, stale, w.badge?.color, next)
}
