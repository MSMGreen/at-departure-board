package io.github.msmgreen.nextdeparture.core

// How fresh the widget may claim to be (widget spec 10). The board computes
// stale_s = now - last_ok - poll_interval_s and is stale past STALE_AFTER_S
// (lib/core/src/model.h, freshness.h). A widget does not poll; the only honest
// interval it has is commute mode's 30 s, so it is stale after two minutes.

const val SCHEDULE_PERIOD_S = 15 * 60
const val RT_FAST_S = 30
const val STALE_AFTER_S = 90
const val WIDGET_STALE_AFTER_S = RT_FAST_S + STALE_AFTER_S

fun isStale(lastOk: Long, now: Long): Boolean = lastOk == 0L || now - lastOk > WIDGET_STALE_AFTER_S

// The first second at which a fresh view stops being fresh.
fun staleAt(lastOk: Long): Long = lastOk + WIDGET_STALE_AFTER_S + 1

// last_ok after a fetch. It moves only when there was news - realtime answered
// 200, or there was nothing upcoming to ask it about - AND the schedule under
// it is younger than SCHEDULE_PERIOD_S: the widget is only as fresh as its
// schedule, as board_last_ok holds for the board.
fun lastOkAfter(previous: Long, now: Long, news: Boolean, schedOkAt: Long): Long =
    if (news && schedOkAt > 0 && now - schedOkAt < SCHEDULE_PERIOD_S) now else previous
