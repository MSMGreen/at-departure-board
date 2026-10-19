package io.github.msmgreen.nextdeparture

import io.github.msmgreen.nextdeparture.core.AtHttp
import io.github.msmgreen.nextdeparture.core.HttpResult
import io.github.msmgreen.nextdeparture.core.KEY_HEADER
import io.github.msmgreen.nextdeparture.core.TRANSPORT_ERROR
import okhttp3.OkHttpClient
import okhttp3.Request
import java.io.IOException
import java.util.concurrent.TimeUnit

// AtHttp over OkHttp. Blocking: every caller is already off the main thread.
// The key comes from android/local.properties via BuildConfig, as src/secrets.h
// supplies it to the board.
object OkAt : AtHttp {
    private val client = OkHttpClient.Builder()
        .connectTimeout(10, TimeUnit.SECONDS)
        .readTimeout(15, TimeUnit.SECONDS)
        .build()

    override fun get(url: String): HttpResult {
        val request = Request.Builder()
            .url(url)
            .header(KEY_HEADER, BuildConfig.AT_API_KEY)
            .header("Accept", "application/json")
            .build()
        return try {
            client.newCall(request).execute().use { HttpResult(it.code, it.body.string()) }
        } catch (e: IOException) {
            HttpResult(TRANSPORT_ERROR, "")
        }
    }
}
