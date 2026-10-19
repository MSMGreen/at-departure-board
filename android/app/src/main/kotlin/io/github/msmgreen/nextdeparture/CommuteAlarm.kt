package io.github.msmgreen.nextdeparture

import android.app.AlarmManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import io.github.msmgreen.nextdeparture.core.activeWindow
import io.github.msmgreen.nextdeparture.core.nextBoundary

// The one commute boundary alarm (widget spec 9). Whatever may have changed the
// windows - boot, an update, a settings save, Not today, the periodic run -
// calls arm(); only the alarm starts the service.
object CommuteAlarm {
    // Inside an open window with no service running, the alarm is for now:
    // a foreground service may be started from an exact-alarm callback, where a
    // boot receiver or a worker may not start one.
    suspend fun arm(context: Context) {
        val store = Store(context)
        val now = System.currentTimeMillis() / 1000
        val windows = store.config()?.windows.orEmpty()
        val open = activeWindow(windows, now, store.quietUntil()) != null
        // A window removed, or Not today, while the service runs: stop it now.
        if (!open) CommuteService.stop(context)
        set(context, if (open && !CommuteService.running) now else nextBoundary(windows, now))
    }

    // After a boundary has fired: the next one, never now again.
    suspend fun armNext(context: Context) {
        val windows = Store(context).config()?.windows.orEmpty()
        set(context, nextBoundary(windows, System.currentTimeMillis() / 1000))
    }

    private fun set(context: Context, at: Long?) {
        val alarms = context.getSystemService(AlarmManager::class.java)
        val pi = PendingIntent.getBroadcast(
            context, 0, Intent(context, BoundaryReceiver::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        when {
            at == null -> alarms.cancel(pi)
            alarms.canScheduleExactAlarms() -> alarms.setExactAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, at * 1000, pi)
            // Without Alarms & reminders the boundary may land ten minutes or
            // more late; the setup check says so (widget spec 11).
            else -> alarms.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, at * 1000, pi)
        }
    }
}
