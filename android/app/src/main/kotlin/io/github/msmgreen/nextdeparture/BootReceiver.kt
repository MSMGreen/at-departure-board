package io.github.msmgreen.nextdeparture

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

// After a reboot every widget Chronometer base is wrong - elapsedRealtime
// starts again from zero - and every alarm is gone; an update clears the
// alarms too. Fetch, which redraws, and arm the commute boundary, for now if a
// window is open (widget spec 8, 9).
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != Intent.ACTION_BOOT_COMPLETED && intent.action != Intent.ACTION_MY_PACKAGE_REPLACED) return
        RefreshWorker.ensurePeriodic(context)
        RefreshWorker.now(context)
        val pending = goAsync()
        CoroutineScope(Dispatchers.Default).launch {
            try {
                CommuteAlarm.arm(context)
            } finally {
                pending.finish()
            }
        }
    }
}
