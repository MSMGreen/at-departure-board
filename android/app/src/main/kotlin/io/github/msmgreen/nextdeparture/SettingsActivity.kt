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
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawingPadding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.FilterChip
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
import androidx.compose.runtime.mutableStateListOf
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
import io.github.msmgreen.nextdeparture.core.CommuteWindow
import io.github.msmgreen.nextdeparture.core.Config
import io.github.msmgreen.nextdeparture.core.ConfigCodec
import io.github.msmgreen.nextdeparture.core.MAX_PLACES
import io.github.msmgreen.nextdeparture.core.MAX_WINDOWS
import io.github.msmgreen.nextdeparture.core.Place
import io.github.msmgreen.nextdeparture.core.WEEKDAYS
import io.github.msmgreen.nextdeparture.core.WatchConfig
import io.github.msmgreen.nextdeparture.core.placeAfterRemoval
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

// Places, each with its watch and walk time; commute windows; and the setup
// check. Also the widget's configure activity (res/xml/departure_widget_info.xml).
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
                        ConfigForm(Store(this@SettingsActivity))
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

// The form's copy of a place, as typed. base keeps what this form does not
// edit - coordinates and radius, which phase 3 sets.
private data class PlaceDraft(
    val name: String = "",
    val label: String = "",
    val stop: String = "",
    val route: String = "",
    val toward: String = "",
    val walk: String = "5",
    val base: Place? = null,
) {
    fun toPlace(): Place = (base ?: Place("", WatchConfig("", "", "", ""))).copy(
        name = name.trim(),
        watch = WatchConfig(label.trim(), stop.trim(), route.trim(), toward.trim()),
        walkMin = walk.toIntOrNull() ?: -1, // -1 fails validation, as it should
    )

    companion object {
        fun of(p: Place) = PlaceDraft(
            p.name, p.watch.label, p.watch.stopCode, p.watch.routeShortName, p.watch.towardStopCode, p.walkMin.toString(), p,
        )
    }
}

private data class WindowDraft(val days: Int = WEEKDAYS, val start: String = "07:30", val end: String = "08:30", val place: Int = 0) {
    fun toWindow() = CommuteWindow(days, start.trim(), end.trim(), place)
}

private val DAY_LETTERS = listOf("M", "T", "W", "T", "F", "S", "S") // bit 0 is Monday

// Places and commute windows, saved as one config. ConfigCodec.validate is the
// only judge of it; the form never second-guesses it.
@Composable
private fun ConfigForm(store: Store) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val places = remember { mutableStateListOf(PlaceDraft(name = "Home")) }
    val windows = remember { mutableStateListOf<WindowDraft>() }
    var status by remember { mutableStateOf<String?>(null) }

    LaunchedEffect(Unit) {
        val c = store.config() ?: return@LaunchedEffect
        places.clear()
        places.addAll(c.places.map(PlaceDraft::of))
        windows.clear()
        windows.addAll(c.windows.map { WindowDraft(it.days, it.start, it.end, it.defaultPlace) })
    }

    fun removePlace(i: Int) {
        places.removeAt(i)
        for (j in windows.indices) windows[j] = windows[j].copy(place = placeAfterRemoval(windows[j].place, i))
    }

    Text("Places", style = MaterialTheme.typography.titleLarge)
    places.forEachIndexed { i, p ->
        PlaceCard(p, { places[i] = it }, if (places.size > 1) ({ removePlace(i) }) else null)
    }
    if (places.size < MAX_PLACES) TextButton(onClick = { places.add(PlaceDraft()) }) { Text("Add a place") }

    HorizontalDivider()
    Text("Commute windows", style = MaterialTheme.typography.titleLarge)
    Text(
        "In a window the widget shows that window's place, and refreshes every 30 s while the screen is on.",
        style = MaterialTheme.typography.bodySmall,
    )
    val names = places.mapIndexed { i, p -> p.name.trim().ifEmpty { "Place ${i + 1}" } }
    windows.forEachIndexed { i, w ->
        WindowCard(w, names, { windows[i] = it }, { windows.removeAt(i) })
    }
    if (windows.size < MAX_WINDOWS) TextButton(onClick = { windows.add(WindowDraft()) }) { Text("Add a window") }

    Button(onClick = {
        val config = Config(places = places.map { it.toPlace() }, windows = windows.map { it.toWindow() })
        val error = ConfigCodec.validate(config)
        if (error != null) {
            status = error.text
        } else {
            scope.launch {
                store.saveConfig(config)
                RefreshWorker.now(context)
                CommuteAlarm.arm(context)
                status = "Saved"
            }
        }
    }) { Text("Save") }
    status?.let { Text(it) }
}

@Composable
private fun PlaceCard(p: PlaceDraft, onChange: (PlaceDraft) -> Unit, onRemove: (() -> Unit)?) {
    OutlinedTextField(p.name, { onChange(p.copy(name = it)) }, Modifier.fillMaxWidth(), label = { Text("Place name") })
    OutlinedTextField(p.label, { onChange(p.copy(label = it)) }, Modifier.fillMaxWidth(), label = { Text("Label, e.g. to Wynyard Quarter") })
    StopField("Stop code (the number on the pole)", p.stop) { onChange(p.copy(stop = it)) }
    OutlinedTextField(p.route, { onChange(p.copy(route = it)) }, Modifier.fillMaxWidth(), label = { Text("Route, or empty for any") })
    StopField("Stop you travel toward", p.toward) { onChange(p.copy(toward = it)) }
    Row(verticalAlignment = Alignment.CenterVertically) {
        OutlinedTextField(
            p.walk, { onChange(p.copy(walk = it.filter { c -> c in '0'..'9' })) }, Modifier.weight(1f),
            label = { Text("Walk to the stop, minutes") },
        )
        if (onRemove != null) TextButton(onClick = onRemove) { Text("Remove") }
    }
    HorizontalDivider()
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun WindowCard(w: WindowDraft, places: List<String>, onChange: (WindowDraft) -> Unit, onRemove: () -> Unit) {
    FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
        DAY_LETTERS.forEachIndexed { d, letter ->
            FilterChip(
                selected = w.days and (1 shl d) != 0,
                onClick = { onChange(w.copy(days = w.days xor (1 shl d))) },
                label = { Text(letter) },
            )
        }
    }
    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        OutlinedTextField(w.start, { onChange(w.copy(start = it)) }, Modifier.weight(1f), label = { Text("From, e.g. 07:30") })
        OutlinedTextField(w.end, { onChange(w.copy(end = it)) }, Modifier.weight(1f), label = { Text("To") })
    }
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text("Shows")
        // A tap moves to the next place: there are at most four.
        TextButton(onClick = { onChange(w.copy(place = (w.place + 1) % places.size)) }) {
            Text(places.getOrElse(w.place) { places.first() })
        }
        Spacer(Modifier.weight(1f))
        TextButton(onClick = onRemove) { Text("Remove") }
    }
    HorizontalDivider()
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
    CheckRow("Notifications allowed, for commute mode", CommuteService.notificationsOn(context)) {
        context.startActivity(
            Intent(Settings.ACTION_APP_NOTIFICATION_SETTINGS).putExtra(Settings.EXTRA_APP_PACKAGE, context.packageName),
        )
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
