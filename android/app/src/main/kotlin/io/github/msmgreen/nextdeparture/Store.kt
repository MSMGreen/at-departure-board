package io.github.msmgreen.nextdeparture

import android.content.Context
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import io.github.msmgreen.nextdeparture.core.Cache
import io.github.msmgreen.nextdeparture.core.Config
import io.github.msmgreen.nextdeparture.core.ConfigCodec
import io.github.msmgreen.nextdeparture.core.ConfigResult
import kotlinx.coroutines.flow.first

// Two JSON documents - the settings and the fetch cache - in one DataStore.
// DataStore writes atomically, so a process killed mid-write keeps the old one.
private val Context.dataStore by preferencesDataStore(name = "state")

private val CONFIG = stringPreferencesKey("config")
private val CACHE = stringPreferencesKey("cache")

class Store(context: Context) {
    private val data = context.applicationContext.dataStore

    // Null until the settings have been saved once, or if they will not read
    // back - either way the widget asks to be set up.
    suspend fun config(): Config? {
        val text = data.data.first()[CONFIG] ?: return null
        return (ConfigCodec.parse(text) as? ConfigResult.Ok)?.config
    }

    suspend fun saveConfig(c: Config) {
        data.edit { it[CONFIG] = ConfigCodec.toJson(c) }
    }

    suspend fun cache(): Cache = Cache.fromJson(data.data.first()[CACHE])

    suspend fun saveCache(c: Cache) {
        data.edit { it[CACHE] = c.toJson() }
    }
}
