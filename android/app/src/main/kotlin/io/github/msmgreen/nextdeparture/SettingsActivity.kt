package io.github.msmgreen.nextdeparture

import android.app.AlarmManager
import android.appwidget.AppWidgetManager
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.PowerManager
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import io.github.msmgreen.nextdeparture.core.AtParse
import io.github.msmgreen.nextdeparture.core.AtUrls
import io.github.msmgreen.nextdeparture.core.Config
import io.github.msmgreen.nextdeparture.core.ConfigCodec
import io.github.msmgreen.nextdeparture.core.Place
import io.github.msmgreen.nextdeparture.core.WatchConfig
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

// One place in phase 1: the watch, the walk time, and the setup check. Also the
// widget's configure activity (res/xml/departure_widget_info.xml).
class SettingsActivity : ComponentActivity() {
    // Bumped on every resume: each setup fix happens on another app's screen.
    private val resumes = mutableIntStateOf(0)

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // Opened to configure a new widget: say yes straight away, so backing
        // out keeps the widget rather than removing it.
        val widgetId = intent?.getIntExtra(AppWidgetManager.EXTRA_APPWIDGET_ID, AppWidgetManager.INVALID_APPWIDGET_ID)
        if (widgetId != null && widgetId != AppWidgetManager.INVALID_APPWIDGET_ID) {
            setResult(RESULT_OK, Intent().putExtra(AppWidgetManager.EXTRA_APPWIDGET_ID, widgetId))
        }
        RefreshWorker.ensurePeriodic(this)
        setContent {
            MaterialTheme {
                Surface {
                    Column(
                        // Android 15 draws apps edge to edge; keep the form off the status bar.
                        Modifier.safeDrawingPadding().padding(16.dp).verticalScroll(rememberScrollState()),
                        verticalArrangement = Arrangement.spacedBy(8.dp),
                    ) {
                        PlaceForm(Store(this@SettingsActivity))
                        HorizontalDivider()
                        SetupCheck(resumes.intValue)
                    }
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        resumes.intValue++
    }
}

@Composable
private fun PlaceForm(store: Store) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var name by remember { mutableStateOf("Home") }
    var label by remember { mutableStateOf("") }
    var stopCode by remember { mutableStateOf("") }
    var route by remember { mutableStateOf("") }
    var toward by remember { mutableStateOf("") }
    var walk by remember { mutableStateOf("5") }
    var status by remember { mutableStateOf<String?>(null) }

    LaunchedEffect(Unit) {
        val p = store.config()?.places?.firstOrNull() ?: return@LaunchedEffect
        name = p.name
        label = p.watch.label
        stopCode = p.watch.stopCode
        route = p.watch.routeShortName
        toward = p.watch.towardStopCode
        walk = p.walkMin.toString()
    }

    Text("Departure", style = MaterialTheme.typography.titleLarge)
    OutlinedTextField(name, { name = it }, Modifier.fillMaxWidth(), label = { Text("Place name") })
    OutlinedTextField(label, { label = it }, Modifier.fillMaxWidth(), label = { Text("Label, e.g. to Wynyard Quarter") })
    StopField("Stop code (the number on the pole)", stopCode) { stopCode = it }
    OutlinedTextField(route, { route = it }, Modifier.fillMaxWidth(), label = { Text("Route, or empty for any") })
    StopField("Stop you travel toward", toward) { toward = it }
    OutlinedTextField(
        walk, { walk = it.filter { c -> c in '0'..'9' } }, Modifier.fillMaxWidth(),
        label = { Text("Walk to the stop, minutes") },
    )
    Button(onClick = {
        val watch = WatchConfig(label.trim(), stopCode.trim(), route.trim(), toward.trim())
        val config = Config(places = listOf(Place(name.trim(), watch, walk.toIntOrNull() ?: -1)))
        val error = ConfigCodec.validate(config)
        if (error != null) {
            status = error.text
        } else {
            scope.launch {
                store.saveConfig(config)
                RefreshWorker.now(context)
                status = "Saved"
            }
        }
    }) { Text("Save") }
    status?.let { Text(it) }
}

@Composable
private fun StopField(title: String, value: String, onChange: (String) -> Unit) {
    val scope = rememberCoroutineScope()
    var found by remember(value) { mutableStateOf<String?>(null) }
    Row(verticalAlignment = Alignment.CenterVertically) {
        OutlinedTextField(value, onChange, Modifier.weight(1f), label = { Text(title) })
        TextButton(onClick = { scope.launch { found = checkStop(value.trim()) } }) { Text("Check") }
    }
    found?.let { Text(it, style = MaterialTheme.typography.bodySmall) }
}

// The portal's Check button: confirm a stop code against AT and show its name,
// so a typo is caught here rather than as a silently wrong departure.
private suspend fun checkStop(code: String): String = withContext(Dispatchers.IO) {
    if (code.isEmpty() || !code.all { it in '0'..'9' || it in 'a'..'z' || it in 'A'..'Z' }) {
        return@withContext "Letters and digits only"
    }
    val r = OkAt.get(AtUrls.stopByCode(code))
    when (r.status) {
        200 -> AtParse.stops(r.body)?.firstOrNull()?.stopName ?: "No such stop"
        400 -> "Stop lookup gone"
        401 -> "Check API key"
        else -> "AT answered ${r.status}; try again"
    }
}

// Reads each requirement back from the system (widget spec 11), because a
// widget whose app has been put to sleep goes quietly stale with no error.
@Composable
private fun SetupCheck(resumes: Int) = key(resumes) { // re-read on every resume
    val context = LocalContext.current
    val power = context.getSystemService(PowerManager::class.java)
    val alarms = context.getSystemService(AlarmManager::class.java)
    val widgets = AppWidgetManager.getInstance(context)
        .getAppWidgetIds(ComponentName(context, DepartureWidget::class.java))

    Text("Setup check", style = MaterialTheme.typography.titleMedium)
    CheckRow("API key built in", BuildConfig.AT_API_KEY.isNotEmpty(), null)
    CheckRow("Battery: Unrestricted", power.isIgnoringBatteryOptimizations(context.packageName)) {
        context.open(Settings.ACTION_APPLICATION_DETAILS_SETTINGS)
    }
    CheckRow("Alarms & reminders allowed", alarms.canScheduleExactAlarms()) {
        context.open(Settings.ACTION_REQUEST_SCHEDULE_EXACT_ALARM)
    }
    CheckRow("Widget on a home screen", widgets.isNotEmpty(), null)
    Text(
        "Also add this app to Battery > Background usage limits > Never sleeping apps. " +
            "Android cannot report that one back.",
        style = MaterialTheme.typography.bodySmall,
    )
}

@Composable
private fun CheckRow(what: String, ok: Boolean, fix: (() -> Unit)?) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text((if (ok) "✓  " else "✗  ") + what, Modifier.weight(1f))
        if (!ok && fix != null) TextButton(onClick = fix) { Text("Fix") }
    }
}

private fun Context.open(action: String) {
    startActivity(Intent(action, Uri.fromParts("package", packageName, null)))
}
