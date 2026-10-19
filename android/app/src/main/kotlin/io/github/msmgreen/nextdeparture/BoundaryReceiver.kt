package io.github.msmgreen.nextdeparture

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import io.github.msmgreen.nextdeparture.core.activeWindow
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

// A commute window has started or ended (widget spec 9). Decided from the clock
// rather than from which alarm this was, so a late alarm, an overlap or a
// settings change in between all come out right.
class BoundaryReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        val pending = goAsync()
        CoroutineScope(Dispatchers.Default).launch {
            try {
                val store = Store(context)
                val now = System.currentTimeMillis() / 1000
                val windows = store.config()?.windows.orEmpty()
                if (activeWindow(windows, now, store.quietUntil()) != null) {
                    CommuteService.start(context) // which fetches the window's place
                } else {
                    CommuteService.stop(context)
                    // The window's place gives way to the first place: fetch it,
                    // rather than leave the widget showing the place just left.
                    RefreshWorker.now(context)
                }
                CommuteAlarm.armNext(context)
            } finally {
                pending.finish()
            }
        }
    }
}
