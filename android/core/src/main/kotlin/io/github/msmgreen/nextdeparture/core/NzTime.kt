package io.github.msmgreen.nextdeparture.core

import java.time.Instant
import java.time.LocalDate
import java.time.LocalTime
import java.time.ZoneId
import java.time.ZonedDateTime

// Pacific/Auckland from the platform's tz database. The board carries its own
// DST rules (lib/core/src/nztime.cpp) because the Windows C runtime got them
// wrong; java.time does not, so only the GTFS-specific rules are ported here.
val AKL: ZoneId = ZoneId.of("Pacific/Auckland")

fun local(epoch: Long): ZonedDateTime = Instant.ofEpochSecond(epoch).atZone(AKL)

private fun twoDigits(s: String, at: Int): Int? {
    val a = s[at]
    val b = s[at + 1]
    if (a !in '0'..'9' || b !in '0'..'9') return null
    return (a - '0') * 10 + (b - '0')
}

// "HH:MM:SS", hours 0..47 - GTFS uses 25:10:00 for 01:10 the next day.
fun parseGtfsTime(s: String): Int? {
    if (s.length != 8 || s[2] != ':' || s[5] != ':') return null
    val h = twoDigits(s, 0) ?: return null
    val m = twoDigits(s, 3) ?: return null
    val sec = twoDigits(s, 6) ?: return null
    if (h > 47 || m > 59 || sec > 59) return null
    return h * 3600 + m * 60 + sec
}

fun parseIsoDate(s: String): LocalDate? {
    if (s.length != 10 || s[4] != '-' || s[7] != '-') return null
    return try {
        LocalDate.parse(s)
    } catch (e: java.time.format.DateTimeParseException) {
        null
    }
}

// GTFS measures a departure as elapsed seconds from noon minus 12 hours of the
// service day, in absolute time, so noon stays noon when the clocks change.
// NZ changes its clocks at 02:00 and 03:00, so noon is never ambiguous.
fun gtfsEpoch(serviceDate: LocalDate, secs: Int): Long =
    serviceDate.atTime(LocalTime.NOON).atZone(AKL).toEpochSecond() - 12 * 3600 + secs

// "17:42", or "5:42pm" when the phone is set to 12-hour time.
fun formatClock(epoch: Long, h24: Boolean): String {
    val t = local(epoch)
    if (h24) return "%d:%02d".format(t.hour, t.minute)
    val h = if (t.hour % 12 == 0) 12 else t.hour % 12
    return "%d:%02d%s".format(h, t.minute, if (t.hour < 12) "am" else "pm")
}
