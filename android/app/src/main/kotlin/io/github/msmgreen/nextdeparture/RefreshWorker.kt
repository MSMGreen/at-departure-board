package io.github.msmgreen.nextdeparture

import android.content.Context
import androidx.work.Constraints
import androidx.work.CoroutineWorker
import androidx.work.ExistingPeriodicWorkPolicy
import androidx.work.ExistingWorkPolicy
import androidx.work.NetworkType
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.OutOfQuotaPolicy
import androidx.work.PeriodicWorkRequestBuilder
import androidx.work.WorkManager
import androidx.work.WorkerParameters
import androidx.work.workDataOf
import java.util.concurrent.TimeUnit

class RefreshWorker(context: Context, params: WorkerParameters) : CoroutineWorker(context, params) {
    override suspend fun doWork(): Result {
        Refresher.refresh(applicationContext, force = inputData.getBoolean(KEY_FORCE, false))
        return Result.success()
    }

    companion object {
        private const val KEY_FORCE = "force"

        // The baseline: every 30 minutes, with a network (widget spec 8).
        // WorkManager re-arms it after a reboot by itself.
        fun ensurePeriodic(context: Context) {
            val request = PeriodicWorkRequestBuilder<RefreshWorker>(30, TimeUnit.MINUTES)
                .setConstraints(Constraints(requiredNetworkType = NetworkType.CONNECTED))
                .build()
            WorkManager.getInstance(context)
                .enqueueUniquePeriodicWork("periodic", ExistingPeriodicWorkPolicy.UPDATE, request)
        }

        // A tap, a settings save or a reboot: now, and past any backoff.
        fun now(context: Context) {
            val request = OneTimeWorkRequestBuilder<RefreshWorker>()
                .setExpedited(OutOfQuotaPolicy.RUN_AS_NON_EXPEDITED_WORK_REQUEST)
                .setInputData(workDataOf(KEY_FORCE to true))
                .build()
            WorkManager.getInstance(context).enqueueUniqueWork("now", ExistingWorkPolicy.REPLACE, request)
        }
    }
}
