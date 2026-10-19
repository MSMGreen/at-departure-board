package io.github.msmgreen.nextdeparture

import android.app.ForegroundServiceStartNotAllowedException
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.ServiceInfo
import android.graphics.drawable.Icon
import android.os.IBinder
import android.os.PowerManager
import android.text.format.DateFormat
import io.github.msmgreen.nextdeparture.core.CommuteNotice
import io.github.msmgreen.nextdeparture.core.activeWindow
import io.github.msmgreen.nextdeparture.core.commuteDelay
import io.github.msmgreen.nextdeparture.core.commuteNotice
import io.github.msmgreen.nextdeparture.core.endOfLocalDay
import io.github.msmgreen.nextdeparture.core.widgetView
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

// Commute mode (widget spec 9). A foreground service whose job is to keep a
// process alive that hears the screen turn on and off - broadcasts a manifest
// receiver cannot get - and to fetch every 30 s while it is on. It holds no
// wake lock: with the screen off, nothing runs.
class CommuteService : Service() {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private var loop: Job? = null

    private val screen = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                Intent.ACTION_SCREEN_ON -> startLoop()
                Intent.ACTION_SCREEN_OFF -> stopLoop()
            }
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        running = true
        val filter = IntentFilter().apply {
            addAction(Intent.ACTION_SCREEN_ON)
            addAction(Intent.ACTION_SCREEN_OFF)
        }
        // System broadcasts still reach a receiver that is not exported.
        registerReceiver(screen, filter, RECEIVER_NOT_EXPORTED)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_NOT_TODAY) {
            scope.launch { notToday() }
            return START_NOT_STICKY
        }
        // Required within seconds of startForegroundService, whatever follows.
        startForeground(NOTICE_ID, build(this, CommuteNotice(getString(R.string.commute_starting), "", null)), ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE)
        scope.launch {
            // A sticky restart may come after the window has closed.
            val store = Store(this@CommuteService)
            val now = System.currentTimeMillis() / 1000
            if (activeWindow(store.config()?.windows.orEmpty(), now, store.quietUntil()) == null) {
                stopSelf()
                return@launch
            }
            update(this@CommuteService)
            // The window has started: fetch once, screen on or not.
            Refresher.refresh(this@CommuteService, force = false)
        }
        if (getSystemService(PowerManager::class.java).isInteractive) startLoop()
        return START_STICKY
    }

    // Screen on: fetch now, then every 30 s, or slower while backing off. The
    // refresher's 15 s debounce absorbs a fetch that has only just happened.
    private fun startLoop() {
        if (loop?.isActive == true) return
        loop = scope.launch {
            while (true) {
                Refresher.refresh(this@CommuteService, force = false)
                val now = System.currentTimeMillis() / 1000
                val wait = commuteDelay(Store(this@CommuteService).cache().watch, now) ?: break
                delay(wait * 1000)
            }
        }
    }

    private fun stopLoop() {
        loop?.cancel()
        loop = null
    }

    // A day at home: no window opens again until local midnight.
    private suspend fun notToday() {
        Store(this).setQuietUntil(endOfLocalDay(System.currentTimeMillis() / 1000))
        stopLoop()
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
        CommuteAlarm.arm(this)
        RefreshWorker.now(this) // the window's place gives way to home
    }

    override fun onDestroy() {
        running = false
        unregisterReceiver(screen)
        scope.cancel()
        super.onDestroy()
    }

    companion object {
        private const val NOTICE_ID = 1
        private const val ALERT_ID = 2
        private const val CHANNEL = "commute"
        private const val ALERTS = "commute_alerts"
        private const val ACTION_NOT_TODAY = "io.github.msmgreen.nextdeparture.NOT_TODAY"

        // This process's service, so the notification is only redrawn while
        // commute mode is on.
        @Volatile var running = false
            private set

        fun start(context: Context) {
            val intent = Intent(context, CommuteService::class.java)
            try {
                context.startForegroundService(intent)
            } catch (e: ForegroundServiceStartNotAllowedException) {
                // Both of today's exemptions gone (widget spec 9): ask for a
                // tap, which is a user-initiated start.
                channels(context)
                val tap = PendingIntent.getForegroundService(context, 0, intent, PendingIntent.FLAG_IMMUTABLE)
                val alert = Notification.Builder(context, ALERTS)
                    .setSmallIcon(R.drawable.ic_commute)
                    .setContentTitle(context.getString(R.string.commute_not_started))
                    .setContentIntent(tap)
                    .setAutoCancel(true)
                    .build()
                context.getSystemService(NotificationManager::class.java).notify(ALERT_ID, alert)
            }
        }

        fun stop(context: Context) {
            context.stopService(Intent(context, CommuteService::class.java))
        }

        // Redraws the notification from the stored cache, as renderAll redraws
        // the widget; core's commuteNotice chooses every word.
        suspend fun update(context: Context) {
            if (!running) return
            val store = Store(context)
            val now = System.currentTimeMillis() / 1000
            val place = store.place(now) ?: return
            val watch = store.cache().watch?.takeIf { it.key == place.watch.key }
            val h24 = DateFormat.is24HourFormat(context)
            val notice = commuteNotice(widgetView(place, watch, now, h24), watch?.lastOk ?: 0, now, h24)
            context.getSystemService(NotificationManager::class.java).notify(NOTICE_ID, build(context, notice))
        }

        // Spec 11's setup check: notifications allowed, and neither channel off.
        fun notificationsOn(context: Context): Boolean {
            channels(context)
            val nm = context.getSystemService(NotificationManager::class.java)
            return nm.areNotificationsEnabled() &&
                listOf(CHANNEL, ALERTS).all { nm.getNotificationChannel(it)?.importance != NotificationManager.IMPORTANCE_NONE }
        }

        private fun channels(context: Context) {
            val nm = context.getSystemService(NotificationManager::class.java)
            val quiet = NotificationChannel(CHANNEL, context.getString(R.string.channel_commute), NotificationManager.IMPORTANCE_LOW)
            quiet.setSound(null, null)
            quiet.enableVibration(false)
            quiet.setShowBadge(false)
            nm.createNotificationChannel(quiet)
            nm.createNotificationChannel(
                NotificationChannel(ALERTS, context.getString(R.string.channel_commute_alerts), NotificationManager.IMPORTANCE_DEFAULT),
            )
        }

        private fun build(context: Context, n: CommuteNotice): Notification {
            channels(context)
            val settings = PendingIntent.getActivity(
                context, 0, Intent(context, SettingsActivity::class.java), PendingIntent.FLAG_IMMUTABLE,
            )
            val notToday = PendingIntent.getService(
                context, 1, Intent(context, CommuteService::class.java).setAction(ACTION_NOT_TODAY), PendingIntent.FLAG_IMMUTABLE,
            )
            val b = Notification.Builder(context, CHANNEL)
                .setSmallIcon(R.drawable.ic_commute)
                .setContentTitle(n.title)
                .setContentText(n.text)
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .setCategory(Notification.CATEGORY_STATUS)
                // It does the widget's job on the lock screen (widget spec 9).
                .setVisibility(Notification.VISIBILITY_PUBLIC)
                .setContentIntent(settings)
                .addAction(action(context, R.string.action_refresh, DepartureWidget.refreshIntent(context)))
                .addAction(action(context, R.string.action_not_today, notToday))
            val to = n.countdownTo
            if (to == null) {
                b.setShowWhen(false)
            } else {
                b.setWhen(to * 1000).setShowWhen(true).setUsesChronometer(true).setChronometerCountDown(true)
            }
            return b.build()
        }

        private fun action(context: Context, title: Int, pi: PendingIntent) =
            Notification.Action.Builder(Icon.createWithResource(context, R.drawable.ic_commute), context.getString(title), pi).build()
    }
}
