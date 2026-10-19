package io.github.msmgreen.nextdeparture

import android.app.AlarmManager
import android.app.PendingIntent
import android.appwidget.AppWidgetManager
import android.appwidget.AppWidgetProvider
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.os.SystemClock
import android.text.format.DateFormat
import android.util.SizeF
import android.view.View
import android.widget.RemoteViews
import io.github.msmgreen.nextdeparture.core.WidgetView
import io.github.msmgreen.nextdeparture.core.widgetView
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

// Maps a WidgetView onto RemoteViews. Every decision about what to say was made
// in core's widgetView and is tested there; nothing here chooses words.
class DepartureWidget : AppWidgetProvider() {

    override fun onReceive(context: Context, intent: Intent) {
        when (intent.action) {
            ACTION_REFRESH -> RefreshWorker.now(context)
            ACTION_RENDER -> renderAsync(context)
            else -> super.onReceive(context, intent)
        }
    }

    override fun onUpdate(context: Context, manager: AppWidgetManager, ids: IntArray) {
        RefreshWorker.ensurePeriodic(context)
        renderAsync(context)
    }

    private fun renderAsync(context: Context) {
        val pending = goAsync()
        CoroutineScope(Dispatchers.Default).launch {
            try {
                renderAll(context)
            } finally {
                pending.finish()
            }
        }
    }

    companion object {
        const val ACTION_REFRESH = "io.github.msmgreen.nextdeparture.REFRESH"
        const val ACTION_RENDER = "io.github.msmgreen.nextdeparture.RENDER"

        // Draws every instance from the stored cache, without the network, and
        // arms the next local re-render (widget spec 6).
        suspend fun renderAll(context: Context) {
            val manager = AppWidgetManager.getInstance(context)
            val ids = manager.getAppWidgetIds(ComponentName(context, DepartureWidget::class.java))
            if (ids.isEmpty()) return
            val store = Store(context)
            val now = System.currentTimeMillis() / 1000
            val place = store.place(now)
            if (place == null) {
                manager.updateAppWidget(ids, unconfigured(context))
                return
            }
            val watch = store.cache().watch?.takeIf { it.key == place.watch.key }
            val view = widgetView(place, watch, now, DateFormat.is24HourFormat(context))
            scheduleRender(context, view.nextRenderAt)
            // 2x1 keeps the title and the countdown; 4x1 has room for the rest.
            val sized = RemoteViews(
                mapOf(
                    SizeF(130f, 40f) to remoteViews(context, view, compact = true),
                    SizeF(250f, 40f) to remoteViews(context, view, compact = false),
                ),
            )
            manager.updateAppWidget(ids, sized)
        }

        private fun remoteViews(context: Context, v: WidgetView, compact: Boolean): RemoteViews {
            val r = RemoteViews(context.packageName, R.layout.widget_departure)
            val text = context.getColor(if (v.stale) R.color.widget_text_dim else R.color.widget_text)
            val quiet = context.getColor(R.color.widget_text_dim)

            r.setTextViewText(R.id.place, v.place)
            r.setTextViewText(R.id.title, v.title)
            r.setTextViewText(R.id.as_of, v.asOf ?: "")
            r.setTextViewText(R.id.detail, v.detail)
            r.setTextViewText(R.id.note, v.note ?: "")
            for (id in listOf(R.id.place, R.id.title, R.id.leave_label, R.id.countdown, R.id.message, R.id.detail)) {
                r.setTextColor(id, text)
            }
            r.setTextColor(R.id.as_of, quiet)
            r.setTextColor(R.id.note, quiet)
            r.setInt(R.id.badge, "setColorFilter", v.badgeColor?.argb ?: context.getColor(R.color.widget_badge))

            val to = v.countdownTo // a property of another module: no smart cast
            if (to != null) {
                // The base is in the elapsedRealtime timebase, so the countdown
                // ticks with no process running (widget spec 6).
                val base = SystemClock.elapsedRealtime() + (to * 1000 - System.currentTimeMillis())
                r.setChronometer(R.id.countdown, base, null, true)
                r.setChronometerCountDown(R.id.countdown, true)
                show(r, R.id.leave_label, R.id.countdown)
                hide(r, R.id.message)
            } else {
                r.setChronometer(R.id.countdown, SystemClock.elapsedRealtime(), null, false)
                r.setTextViewText(R.id.message, v.message ?: "")
                show(r, R.id.message)
                hide(r, R.id.leave_label, R.id.countdown)
            }
            // Every view's visibility is set on every render: a host may apply
            // these on top of the views it already has, such as the unconfigured
            // ones, which hide the title.
            show(r, R.id.title)
            // The cat-bus trots while the view is fresh and gives way to
            // "as of" once it is stale; the stale render is already armed.
            if (v.stale) hide(r, R.id.catbus) else show(r, R.id.catbus)
            if (compact) {
                hide(r, R.id.as_of, R.id.detail, R.id.note)
            } else {
                show(r, R.id.detail)
                if (v.asOf == null) hide(r, R.id.as_of) else show(r, R.id.as_of)
                if (v.note == null) hide(r, R.id.note) else show(r, R.id.note)
            }

            r.setOnClickPendingIntent(R.id.root, broadcast(context, ACTION_REFRESH))
            return r
        }

        private fun unconfigured(context: Context): RemoteViews {
            val r = RemoteViews(context.packageName, R.layout.widget_departure)
            r.setTextViewText(R.id.place, context.getString(R.string.app_name))
            r.setTextViewText(R.id.message, context.getString(R.string.tap_to_set_up))
            show(r, R.id.message)
            hide(r, R.id.leave_label, R.id.countdown, R.id.title, R.id.as_of, R.id.detail, R.id.note, R.id.catbus)
            val settings = Intent(context, SettingsActivity::class.java)
            r.setOnClickPendingIntent(R.id.root, PendingIntent.getActivity(context, 0, settings, PendingIntent.FLAG_IMMUTABLE))
            return r
        }

        private fun show(r: RemoteViews, vararg ids: Int) = ids.forEach { r.setViewVisibility(it, View.VISIBLE) }

        private fun hide(r: RemoteViews, vararg ids: Int) = ids.forEach { r.setViewVisibility(it, View.GONE) }

        private fun broadcast(context: Context, action: String): PendingIntent = PendingIntent.getBroadcast(
            context,
            action.hashCode(),
            Intent(context, DepartureWidget::class.java).setAction(action),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )

        // Exact when allowed. An inexact alarm may arrive as late as three
        // quarters of its delay, which would leave the countdown running past
        // zero for minutes. Neither kind fires while the phone dozes; both fire
        // when it wakes, which is the only time anyone looks.
        private fun scheduleRender(context: Context, at: Long?) {
            val alarms = context.getSystemService(AlarmManager::class.java)
            val pi = broadcast(context, ACTION_RENDER)
            if (at == null) {
                alarms.cancel(pi)
            } else if (alarms.canScheduleExactAlarms()) {
                alarms.setExact(AlarmManager.RTC, at * 1000, pi)
            } else {
                alarms.set(AlarmManager.RTC, at * 1000, pi)
            }
        }
    }
}
