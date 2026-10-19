package io.github.msmgreen.nextdeparture

import android.content.Context
import android.util.Log
import io.github.msmgreen.nextdeparture.core.Fetcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext

const val TAG = "NextDeparture" // adb logcat -s NextDeparture

// A fetch that is not a tap is skipped when the last good one for the same
// place finished this recently (widget spec 8).
private const val DEBOUNCE_S = 15

// Every fetch, from every entry point, comes through here: one mutex, so a tap
// during a periodic run makes one set of requests, not two.
object Refresher {
    private val mutex = Mutex()
    private val fetcher = Fetcher(OkAt) { Log.d(TAG, it) }

    suspend fun refresh(context: Context, force: Boolean) {
        mutex.withLock {
            val store = Store(context)
            val now = System.currentTimeMillis() / 1000
            // Only the current place is fetched; another becomes current at a
            // window boundary, and is fetched then (widget spec 4).
            val place = store.place(now) ?: return@withLock
            val cache = store.cache()
            val last = cache.watch
            val recent = last != null && last.key == place.watch.key && now - last.lastOk < DEBOUNCE_S
            if (!force && recent) return@withLock
            val next = withContext(Dispatchers.IO) { fetcher.refresh(cache, place.watch, now, force) }
            store.saveCache(next)
        }
        DepartureWidget.renderAll(context)
        CommuteService.update(context)
    }
}
