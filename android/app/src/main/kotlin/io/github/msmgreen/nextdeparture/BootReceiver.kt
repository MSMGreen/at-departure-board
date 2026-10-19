package io.github.msmgreen.nextdeparture

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent

// After a reboot every widget Chronometer base is wrong - elapsedRealtime
// starts again from zero - and every alarm is gone. Fetch, which redraws.
class BootReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != Intent.ACTION_BOOT_COMPLETED) return
        RefreshWorker.ensurePeriodic(context)
        RefreshWorker.now(context)
    }
}
