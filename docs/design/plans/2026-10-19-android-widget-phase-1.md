# Android Widget Phase 1 Implementation Plan

> **For agentic workers:** work through this plan task by task, in order. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A home-screen widget on two Android phones that shows the next
departure you can still catch from one configured place, with a countdown to
when you need to leave, and that is honest about how fresh it is.

**Architecture:** Two Gradle modules under `android/`. `core` is pure Kotlin on
the JVM with no Android imports: the AT URLs and parsers, the rules ported from
`lib/core/src/live.cpp`, the catchable-departure rule, freshness, the config
schema, the fetch loop for one watch (`src/fetcher.cpp`'s logic, behind an HTTP
interface), and the view model that decides every word the widget shows. It is
tested on a laptop against `test/fixtures/`, the same captured responses the
C++ suites read. `app` is thin: DataStore for the settings and the cache,
OkHttp behind core's `AtHttp`, WorkManager for the 30-minute baseline and for
taps, an `AppWidgetProvider` that maps a `WidgetView` onto `RemoteViews`, and a
Compose settings screen with a setup check.

**Tech Stack:** Kotlin 2.4.20, kotlinx-serialization-json 1.11.0, Gradle 9.7.1
(wrapper), JUnit 5 through `kotlin("test")`. In `app`: the Android Gradle
Plugin, Compose and androidx versions that Android Studio's template picks,
OkHttp 5.4.0, kotlinx-coroutines 1.11.0, WorkManager, DataStore Preferences.
`minSdk` 34, `targetSdk` 36.

**Spec:** `docs/design/specs/2026-10-18-android-widget-design.md` — phase 1 of
its Phasing section, drawing on §4 (data path), §5 (choosing the departure),
§6 (the widget), §8 (refresh), §10 (freshness), §11 (platform) and §12 (build).
`docs/at-api-notes.md` outranks both specs on how AT behaves.

## Scope

Phase 1 of the spec's four.

**In scope:** the `core` port and its tests; one place, set on the settings
screen, with a walk time; the catchable rule; the 30-minute periodic refresh
and tap to refresh; freshness and the local re-render alarm; the setup check;
the CI job.

**Not in scope:** commute mode (phase 2), places chosen by location and the
manual flip (phase 3), Live Updates and the leave-now alert (deferred). The
config schema already carries up to four places with optional coordinates, so
phases 2 and 3 add to it rather than migrate it.

**One change from the spec's first draft**, since made in the spec too.
`SCHEDULE_EXACT_ALARM` moves forward from phase 2, because the widget's local
re-render at leave-by needs it: an inexact alarm may be delivered as late as
three quarters of its delay (`AlarmManager`'s default window), so a re-render
due in ten minutes could land seven minutes late, with the countdown running
past zero the whole time. Without the permission the app falls back to an
inexact alarm, and the setup check says so.

**One tightening of the board's config rules.** The board accepts an empty
`toward_stop_code`; the widget does not. One departure needs a direction.

**How this plan was checked.** Every file under `android/core/` below was built
and its tests run while this plan was written — a fresh project, task by task
in the order given, on Gradle 9.7.1 and JDK 21. Each task's expected test count
is the count observed. Three rules were also broken on purpose to confirm the
suite notices (leave-by at exactly now, the clearing of an unanswered realtime
row, and the inconclusive-probe rule); each failed exactly one test.

The `app` tasks (8–10) could **not** be compiled where this plan was written:
Google's Maven repository was unreachable, so neither the Android Gradle Plugin
nor any androidx library could be resolved. Their code is written against the
documented APIs, and may need import or signature fixes in Android Studio.
Treat a compile error there as expected friction, not as a flaw in the design;
the decisions they carry out are all in `core`, and tested.

## Global Constraints

- **`android/core` never imports Android.** It runs on a plain JVM, which is
  what lets its tests run on a laptop and in CI. Anything that needs a
  `Context` lives in `app`.
- **`lib/core/src/` is the reference for every ported rule.** The C++ suites
  (`test/test_at_api`, `test/test_live`, `test/test_nztime`, `test/test_color`)
  are ported case for case. Where the Kotlin and the C++ disagree about a
  fixture, the Kotlin is wrong.
- **Never show a time the widget cannot prove.** A watch whose direction cannot
  be derived shows `Check settings` and no time (board spec 3a). A stale widget
  never says `live` (widget spec 10).
- **A `stoptrips` 404 is an empty window**, never a reason to re-resolve a stop.
  **`start_hour` is never 0.** **The `tripid` filter is inexact.**
  (`docs/at-api-notes.md`)
- **No one's home goes in the repository.** It is public. Real places,
  coordinates and walk times live only in each phone's settings. The stop codes
  already in `test/fixtures/` are fine.
- **The AT key lives only in `android/local.properties`** (gitignored), as
  `src/secrets.h` does for the board. It is the board's key.
- **One signing key for both phones**, kept outside the repository and backed
  up (widget spec 12). An update signed with a different key will not install
  over the old one, and uninstalling loses the settings.
- **Commit messages follow this repo's style:** sentence-case imperative
  subject, no `feat:`/`fix:` prefixes (see `git log`).
- **Installing on a phone is a human step.** Tasks that install are marked
  **PHONE**; the person driving this plan does them and reads the widget.
- `./gradlew :core:test`, run from `android/`, stays green after every task.
  Nothing outside `android/`, `.gitignore`, `.github/workflows/ci.yml`,
  `README.md`, `CONTRIBUTING.md` and the design documents changes, so
  `python -m pytest` and `pio test -e native` are untouched.

---

## File Structure

Paths below abbreviate `android/core/src/main/kotlin/io/github/msmgreen/nextdeparture/core`
as `$CORE`, the same under `src/test` as `$CORE_TEST`,
`android/app/src/main/kotlin/io/github/msmgreen/nextdeparture` as `$APP`, and
`android/app/src/main/res` as `$RES`.

| File | Responsibility | Task |
|---|---|---|
| `android/settings.gradle.kts`, `build.gradle.kts`, `gradle.properties`, `gradle/libs.versions.toml`, wrapper | The build | 1, 8 |
| `android/core/build.gradle.kts` | Pure JVM module; points the tests at `test/fixtures/` | 1 |
| `$CORE/NzTime.kt` | Pacific/Auckland, GTFS times, service-day epochs, the clock | 1 |
| `$CORE/RouteColor.kt` | `route_color`, with `#000000` as absent | 2 |
| `$CORE/AtApi.kt` | AT URLs and parsers | 2 |
| `$CORE/Live.kt` | Direction per pair, row selection, realtime merge, fetch windows, badges | 3 |
| `$CORE/Departure.kt` | The first departure you can still catch | 4 |
| `$CORE/Freshness.kt` | When the widget is stale, and when `lastOk` moves | 4 |
| `$CORE/Config.kt` | Places and watches, validation, JSON | 5 |
| `$CORE/Fetcher.kt` | One watch's fetch loop, and the persisted `Cache` | 6 |
| `$CORE/WidgetModel.kt` | Every word on the widget | 7 |
| `$CORE_TEST/*Test.kt`, `Fixtures.kt` | One suite per file above | 1–7 |
| `android/app/build.gradle.kts`, `AndroidManifest.xml` | The app, its key and signing, permissions, receivers | 8, 9 |
| `$APP/Store.kt` | Settings and cache in DataStore | 9 |
| `$APP/OkAt.kt` | `AtHttp` over OkHttp, with the key header | 9 |
| `$APP/Refresher.kt` | The one way in to a fetch: mutex, debounce, render | 9 |
| `$APP/RefreshWorker.kt` | The 30-minute baseline, and taps | 9 |
| `$APP/BootReceiver.kt` | Redraw and fetch after a reboot | 9 |
| `$APP/DepartureWidget.kt` | `WidgetView` → `RemoteViews`, taps, the render alarm | 9 |
| `$RES/layout/widget_departure.xml`, `xml/departure_widget_info.xml`, drawables, colours, strings | The widget's look | 9 |
| `$APP/SettingsActivity.kt` | One place, Check, Save, the setup check | 10 |
| `.github/workflows/ci.yml` | `:core:test`, then also `:app:assembleDebug` | 1, 8 |
| `.gitignore` | `android/local.properties`, build output, keystores | 1 |

## Risks

| # | Risk | Mitigation |
|---|---|---|
| 1 | **The app tasks were not compiled when written** | The logic they call is all in `core` and tested; expect import and signature fixes, not redesign |
| 2 | **Android Studio's template differs from what Task 8 assumes** (AGP 9 compiles Kotlin itself; plugin aliases and the Compose BOM are its choice) | Task 8 keeps the template's own plugin and dependency lines and changes only what it names |
| 3 | **One UI sleeps the app**, and the widget goes stale without an error | The setup check; *Never sleeping apps*; Task 11's overnight check |
| 4 | **After a reboot the host redraws the old `RemoteViews`**, whose `Chronometer` base is in a dead `elapsedRealtime` timebase | `BootReceiver` fetches and redraws; `onUpdate` redraws too |
| 5 | **Exact alarms not allowed** | Falls back to inexact; the setup check says so; Task 11 check 6 |
| 6 | **The responsive sizes do not match Samsung's grid** | Task 11 measures 2×1 and 4×1 and adjusts the two `SizeF` thresholds |

---
### Task 1: The build, NZ time, and CI

The smallest real thing that exercises the whole chain: a Gradle build that can
reach `test/fixtures/`, one ported rule set with its tests, and CI running them.
Time goes first because every later task needs `gtfsEpoch`.

**Files:**
- Create: `android/settings.gradle.kts`, `android/build.gradle.kts`, `android/gradle.properties`, `android/gradle/libs.versions.toml`, `android/core/build.gradle.kts`, the Gradle wrapper
- Create: `$CORE/NzTime.kt`
- Test: `$CORE_TEST/NzTimeTest.kt`
- Modify: `.gitignore`, `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: nothing
- Produces: `AKL`, `local()`, `parseGtfsTime()`, `parseIsoDate()`, `gtfsEpoch()`, `formatClock()` — used by Tasks 2, 3, 6 and 7

- [ ] **Step 1: Write the build**

`android/settings.gradle.kts`:

```kotlin
pluginManagement {
    repositories {
        gradlePluginPortal()
        mavenCentral()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        mavenCentral()
    }
}

rootProject.name = "next-departure"
include(":core")
```

`android/build.gradle.kts`:

```kotlin
plugins {
    alias(libs.plugins.kotlin.jvm) apply false
    alias(libs.plugins.kotlin.serialization) apply false
}
```

`android/gradle.properties`:

```properties
org.gradle.jvmargs=-Xmx2g
kotlin.code.style=official
```

`android/gradle/libs.versions.toml`:

```toml
[versions]
kotlin = "2.4.20"
serialization = "1.11.0"

[libraries]
kotlinx-serialization-json = { module = "org.jetbrains.kotlinx:kotlinx-serialization-json", version.ref = "serialization" }

[plugins]
kotlin-jvm = { id = "org.jetbrains.kotlin.jvm", version.ref = "kotlin" }
kotlin-serialization = { id = "org.jetbrains.kotlin.plugin.serialization", version.ref = "kotlin" }
```

`android/core/build.gradle.kts`. The `fixtures` system property is how the
tests find the board's captured responses without copying them:

```kotlin
import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    alias(libs.plugins.kotlin.jvm)
    alias(libs.plugins.kotlin.serialization)
}

// Bytecode the Android app can consume, whichever JDK runs Gradle.
java {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
}

kotlin {
    compilerOptions {
        jvmTarget.set(JvmTarget.JVM_17)
    }
}

dependencies {
    implementation(libs.kotlinx.serialization.json)
    testImplementation(kotlin("test"))
}

tasks.test {
    useJUnitPlatform()
    // The board's captured AT responses, shared with the C++ suites.
    systemProperty("fixtures", rootDir.resolve("../test/fixtures").canonicalPath)
}
```

- [ ] **Step 2: Add the wrapper**

From `android/`, with any Gradle 8 or later on `PATH`:

```bash
gradle wrapper --gradle-version 9.7.1
```

Expected: `gradlew`, `gradlew.bat` and `gradle/wrapper/` appear. (Opening
`android/` in Android Studio also offers to create them.) All of them are
committed.

- [ ] **Step 3: Ignore what the build and Android Studio write**

Append to `.gitignore`:

```gitignore

# android/: the widget (docs/design/specs/2026-10-18-android-widget-design.md).
# local.properties holds the AT key and the signing keystore's location.
android/local.properties
android/.gradle/
android/.kotlin/
android/.idea/
android/**/build/
*.jks
*.keystore
```

- [ ] **Step 4: Write the failing test**

`$CORE_TEST/NzTimeTest.kt`. The `gtfsEpoch` values are the ones
`test/test_nztime` checks, so the two implementations are held to the same
answers:

```kotlin
package io.github.msmgreen.nextdeparture.core

import java.time.Instant
import java.time.LocalDate
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class NzTimeTest {
    @Test fun `parses GTFS times including after midnight`() {
        assertEquals(15 * 3600 + 4 * 60 + 7, parseGtfsTime("15:04:07"))
        assertEquals(25 * 3600 + 600, parseGtfsTime("25:10:00"))
        assertEquals(0, parseGtfsTime("00:00:00"))
        assertNull(parseGtfsTime(""))
        assertNull(parseGtfsTime("15:04"))
        assertNull(parseGtfsTime("aa:bb:cc"))
        assertNull(parseGtfsTime("48:00:00"))
        assertNull(parseGtfsTime("15:60:00"))
    }

    @Test fun `parses ISO dates`() {
        assertEquals(LocalDate.of(2026, 9, 13), parseIsoDate("2026-09-13"))
        assertNull(parseIsoDate("2026-13-01"))
        assertNull(parseIsoDate("nope"))
    }

    // The values lib/core's test_nztime checks, so the two implementations agree.
    @Test fun `GTFS times count from noon minus twelve hours`() {
        assertEquals(1789268647, gtfsEpoch(LocalDate.of(2026, 9, 13), 15 * 3600 + 4 * 60 + 7))
        // 25:10 on the 19th is 01:10 on the 20th.
        assertEquals(1789823400, gtfsEpoch(LocalDate.of(2026, 9, 19), 25 * 3600 + 600))
        // On the day DST starts, noon is still noon...
        assertEquals(1790463600, gtfsEpoch(LocalDate.of(2026, 9, 27), 12 * 3600))
        // ...but 01:00 is 00:00, because an hour of that day does not exist.
        assertEquals(1790424000, gtfsEpoch(LocalDate.of(2026, 9, 27), 3600))
        // And on the day it ends, 01:00 is 02:00 for the same reason in reverse.
        assertEquals(1806757200, gtfsEpoch(LocalDate.of(2027, 4, 4), 3600))
    }

    // nztime.cpp carries the DST rules by hand; java.time reads them from the
    // tz database. Pin the database to the board's boundaries.
    @Test fun `the tz database agrees with the board's DST rules`() {
        fun offset(epoch: Long) = AKL.rules.getOffset(Instant.ofEpochSecond(epoch)).totalSeconds
        assertEquals(43200, offset(1790431199)) // 2026-09-27 01:59:59 NZST
        assertEquals(46800, offset(1790431200)) // 03:00:00 NZDT
        assertEquals(46800, offset(1806760799)) // 2027-04-04 02:59:59 NZDT
        assertEquals(43200, offset(1806760800)) // 02:00:00 NZST
    }

    @Test fun `formats the clock both ways`() {
        assertEquals("17:42", formatClock(1789796520, h24 = true))
        assertEquals("5:42pm", formatClock(1789796520, h24 = false))
        assertEquals("7:05", formatClock(1789758300, h24 = true))
        assertEquals("7:05am", formatClock(1789758300, h24 = false))
        assertEquals("12:00pm", formatClock(1789776000, h24 = false))
        assertEquals("12:00am", formatClock(1789732800, h24 = false))
    }
}
```

- [ ] **Step 5: Run it and watch it fail**

Run (from `android/`): `./gradlew :core:test`
Expected: FAIL to compile — `Unresolved reference 'parseGtfsTime'`.

- [ ] **Step 6: Write `NzTime.kt`**

`$CORE/NzTime.kt`:

```kotlin
package io.github.msmgreen.nextdeparture.core

import java.time.Instant
import java.time.LocalDate
import java.time.LocalTime
import java.time.ZoneId
import java.time.ZonedDateTime

// Pacific/Auckland from the platform's tz database. The board carries its own
// DST rules (lib/core/src/nztime.cpp) because the Windows C runtime got them
// wrong; java.time does not, so only the GTFS-specific rules are ported here.
val AKL: ZoneId = ZoneId.of("Pacific/Auckland")

fun local(epoch: Long): ZonedDateTime = Instant.ofEpochSecond(epoch).atZone(AKL)

private fun twoDigits(s: String, at: Int): Int? {
    val a = s[at]
    val b = s[at + 1]
    if (a !in '0'..'9' || b !in '0'..'9') return null
    return (a - '0') * 10 + (b - '0')
}

// "HH:MM:SS", hours 0..47 - GTFS uses 25:10:00 for 01:10 the next day.
fun parseGtfsTime(s: String): Int? {
    if (s.length != 8 || s[2] != ':' || s[5] != ':') return null
    val h = twoDigits(s, 0) ?: return null
    val m = twoDigits(s, 3) ?: return null
    val sec = twoDigits(s, 6) ?: return null
    if (h > 47 || m > 59 || sec > 59) return null
    return h * 3600 + m * 60 + sec
}

fun parseIsoDate(s: String): LocalDate? {
    if (s.length != 10 || s[4] != '-' || s[7] != '-') return null
    return try {
        LocalDate.parse(s)
    } catch (e: java.time.format.DateTimeParseException) {
        null
    }
}

// GTFS measures a departure as elapsed seconds from noon minus 12 hours of the
// service day, in absolute time, so noon stays noon when the clocks change.
// NZ changes its clocks at 02:00 and 03:00, so noon is never ambiguous.
fun gtfsEpoch(serviceDate: LocalDate, secs: Int): Long =
    serviceDate.atTime(LocalTime.NOON).atZone(AKL).toEpochSecond() - 12 * 3600 + secs

// "17:42", or "5:42pm" when the phone is set to 12-hour time.
fun formatClock(epoch: Long, h24: Boolean): String {
    val t = local(epoch)
    if (h24) return "%d:%02d".format(t.hour, t.minute)
    val h = if (t.hour % 12 == 0) 12 else t.hour % 12
    return "%d:%02d%s".format(h, t.minute, if (t.hour < 12) "am" else "pm")
}
```

`nztime.cpp`'s hand-written DST rules are deliberately **not** ported: they
exist because the Windows C runtime got `Pacific/Auckland` wrong, and
`java.time` does not. The test that pins the tz database to the board's
boundaries is what replaces them.

- [ ] **Step 7: Run it**

Run: `./gradlew :core:test`
Expected: PASS, 5 tests.

- [ ] **Step 8: Add the CI job**

Append to `.github/workflows/ci.yml`, after `firmware-build`:

```yaml

  widget-core:
    name: Widget logic (gradle :core:test)
    runs-on: ubuntu-latest
    timeout-minutes: 15
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-java@v4
        with:
          distribution: temurin
          java-version: '21'
      - uses: gradle/actions/setup-gradle@v4
      - run: ./gradlew :core:test
        working-directory: android
```

- [ ] **Step 9: Commit**

```bash
git add .gitignore .github/workflows/ci.yml android/
git commit -m "Start the widget's Gradle build with NZ time and its tests

android/core is plain Kotlin on the JVM, so its tests run on a laptop and in
CI. They read test/fixtures/ in place, the same captured responses the C++
suites use. nztime.cpp's hand-written DST rules are not ported: java.time
reads them from the tz database, and a test pins that to the board's
boundaries."
```

---

### Task 2: AT URLs and parsers

**Files:**
- Create: `$CORE/RouteColor.kt`, `$CORE/AtApi.kt`
- Test: `$CORE_TEST/Fixtures.kt`, `$CORE_TEST/AtApiTest.kt`, `$CORE_TEST/RouteColorTest.kt`

**Interfaces:**
- Consumes: `parseGtfsTime`, `parseIsoDate` (Task 1)
- Produces: `KEY_HEADER`, `AtUrls`, `AtParse`, `StopInfo`, `RouteInfo`, `StopTripRow`, `TripStop`, `RtEntity`, `Rgb`, `parseHex` — used by every later task

**Note on the shape.** Every parser returns `null` when the body is not the
JSON it expects at all, and an empty list when AT answered with no rows. That
is the distinction the fetcher needs: an unknown stop code is 200 with
`{"data":[]}` (a config fault), while an HTML error page from a proxy is a
transport fault to back off from. The parsers walk `JsonElement` trees rather
than decoding into classes, as `at_api.cpp` uses ArduinoJson's `| default`:
it is what lets `stop_time_update` arrive as either an object or an array.

- [ ] **Step 1: Write the failing tests**

`$CORE_TEST/Fixtures.kt`:

```kotlin
package io.github.msmgreen.nextdeparture.core

import java.io.File

// The board's captured AT responses in test/fixtures/, shared with the C++
// suites. The path comes from core/build.gradle.kts.
fun fixture(path: String): String {
    val root = System.getProperty("fixtures") ?: error("run the tests through Gradle")
    return File(root, path).readText()
}

// Kingsland, 2026-09-13, from the post-CRL fixtures.
const val T_15_00 = 1789268400L // 2026-09-13 15:00:00 NZST
```

`$CORE_TEST/AtApiTest.kt` — `test/test_at_api` case for case, against the same
fixtures, plus the array form the docs promise and the `start_hour=0` body:

```kotlin
package io.github.msmgreen.nextdeparture.core

import java.time.LocalDate
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

// test/test_at_api, case for case, against the same fixtures.
class AtApiTest {
    @Test fun urls() {
        assertEquals("https://api.at.govt.nz/gtfs/v3/stops?filter%5Bstop_code%5D=8213", AtUrls.stopByCode("8213"))
        assertEquals(
            "https://api.at.govt.nz/gtfs/v3/routes?filter%5Broute_short_name%5D=20",
            AtUrls.routesByShortName("20"),
        )
        assertEquals("https://api.at.govt.nz/gtfs/v3/routes?filter%5Broute_type%5D=2", AtUrls.railRoutes())
        assertEquals(
            "https://api.at.govt.nz/gtfs/v3/stops/122-34ecc043/stoptrips" +
                "?filter%5Bdate%5D=2026-09-19&filter%5Bstart_hour%5D=17&filter%5Bhour_range%5D=3",
            AtUrls.stoptrips("122-34ecc043", LocalDate.of(2026, 9, 19), 17, 3),
        )
        assertEquals(
            "https://api.at.govt.nz/gtfs/v3/trips/20-02005-54000-2-89a317b8/stops",
            AtUrls.tripStops("20-02005-54000-2-89a317b8"),
        )
        assertEquals(
            "https://api.at.govt.nz/realtime/legacy/tripupdates?tripid=a-1,b-2",
            AtUrls.realtime(listOf("a-1", "b-2")),
        )
        assertNull(AtUrls.realtime(emptyList()))
    }

    @Test fun `start hour accepts after-midnight hours but not zero`() {
        val d = LocalDate.of(2026, 9, 19)
        assertNull(AtUrls.stoptrips("122-34ecc043", d, 0, 3))
        assertEquals(
            "https://api.at.govt.nz/gtfs/v3/stops/122-34ecc043/stoptrips" +
                "?filter%5Bdate%5D=2026-09-19&filter%5Bstart_hour%5D=24&filter%5Bhour_range%5D=2",
            AtUrls.stoptrips("122-34ecc043", d, 24, 2),
        )
        assertNotNull(AtUrls.stoptrips("122-34ecc043", d, 47, 3))
        assertNull(AtUrls.stoptrips("122-34ecc043", d, 48, 3))
    }

    @Test fun `parses a stop and a station`() {
        val bus = AtParse.stops(fixture("post-crl/stops_8213.json"))!!.single()
        assertEquals("8213-7e021a72", bus.stopId)
        assertEquals(0, bus.locationType)
        val station = AtParse.stops(fixture("post-crl/stops_122.json"))!!.single()
        assertEquals("122-34ecc043", station.stopId)
        assertEquals(1, station.locationType)
        assertEquals("Wynyard Quarter", AtParse.stops(fixture("post-crl/stops_1060.json"))!!.single().stopName)
    }

    @Test fun `an unknown stop code is an empty list, not an error`() {
        assertEquals(emptyList(), AtParse.stops(fixture("post-crl/stops_unknown.json")))
        assertNull(AtParse.stops("<html>gateway timeout</html>"))
    }

    @Test fun `parses routes, colouring rail only`() {
        val bus = AtParse.routes(fixture("routes_20.json"))!!.single()
        assertEquals(RouteInfo("20-202", "20", 3, null), bus) // AT omits route_color for buses
        val rail = AtParse.routes(fixture("post-crl/routes_rail.json"))!!
        assertEquals(8, rail.size)
        assertEquals(Rgb(0x97, 0xC9, 0x3D), rail.single { it.routeId == "E-W-201" }.color)
        assertEquals(2, rail.single { it.routeId == "E-W-201" }.routeType)
        assertNull(rail.single { it.routeId == "HUIA-404" }.color) // #000000 is unusable
    }

    @Test fun `parses stoptrips`() {
        val rows = AtParse.stoptrips(fixture("post-crl/stoptrips_kingsland.json"))!!
        assertEquals(23, rows.size)
        assertEquals(12, rows.count { it.directionId == 0 })
        assertEquals(11, rows.count { it.directionId == 1 })
        // A station's rows carry the platform id.
        assertEquals(
            StopTripRow(
                "258-880001-51480-2-W118540-4f47826a", "E-W-201", "9305-ef07ca76", 0,
                LocalDate.of(2026, 9, 13), 15 * 3600 + 11 * 60,
            ),
            rows[0],
        )
    }

    @Test fun `after-midnight trips belong to the previous service date`() {
        val rows = AtParse.stoptrips(fixture("post-crl/stoptrips_kingsland_after_midnight.json"))!!
        assertEquals(11, rows.size)
        assertEquals(24 * 3600 + 4 * 60, rows[0].departureS) // "24:04:00"
        assertEquals(LocalDate.of(2026, 9, 19), rows[0].serviceDate)
        // 2026-09-20 00:04 NZST, checked with Python zoneinfo (Pacific/Auckland).
        assertEquals(1789819440, gtfsEpoch(rows[0].serviceDate, rows[0].departureS))
    }

    @Test fun `trip stops keep their sequence order`() {
        val stops = AtParse.tripStops(fixture("post-crl/trip_dir1_stops.json"))!!
        assertEquals(27, stops.size)
        assertEquals("9328", stops[0].stopCode) // Swanson, first
        assertEquals("9304-dcb2ed75", stops[12].stopId) // Kingsland platform
        assertEquals("9001", stops[16].stopCode) // Waitemata platform 1
        assertEquals("133-08da14b5", stops[16].parentStation)
    }

    @Test fun `parses realtime, stop_time_update as one object`() {
        val ents = AtParse.realtime(fixture("realtime_tripupdates_only.json"))!!
        assertEquals(6, ents.size)
        assertEquals(RtEntity("20-02005-61200-2-89a317b8", -427, false, "1060-00b64ee7", -427), ents[0])
        // This one's stop_time_update IS our Kingsland platform, and disagrees
        // with the trip-level delay: +66 against -28.
        assertEquals(RtEntity("247-810023-61200-2-9731921-77638b11", -28, false, "9304-dcb2ed75", 66), ents[4])
        // And this one has a stop_time_update with no departure at all.
        assertEquals("247-810007-60000-2-9729960-592012e4", ents[3].tripId)
        assertNull(ents[3].stuDepartureDelay)
        assertEquals(-25, ents[3].delay)
    }

    @Test fun `realtime also accepts the documented array form`() {
        val body = """{"response":{"entity":[{"trip_update":{"trip":{"trip_id":"t","schedule_relationship":3},
            "stop_time_update":[{"stop_id":"s","departure":{"delay":5}}]}}]}}"""
        val e = AtParse.realtime(body)!!.single()
        assertTrue(e.cancelled)
        assertNull(e.delay)
        assertEquals("s", e.stuStopId)
        assertEquals(5, e.stuDepartureDelay)
    }

    @Test fun `the 400 for start hour zero is an error body, not data`() {
        val body = fixture("post-crl/stoptrips_start_hour_0.json")
        assertNull(AtParse.stoptrips(body))
        assertTrue(AtParse.errorDetail(body)!!.contains("StartHour"))
        assertNull(AtParse.errorDetail(fixture("post-crl/stops_8213.json")))
    }
}
```

`$CORE_TEST/RouteColorTest.kt` — `test/test_color`'s `parse_hex` cases:

```kotlin
package io.github.msmgreen.nextdeparture.core

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

// test/test_color's parse_hex cases.
class RouteColorTest {
    @Test fun `accepts bare, hashed and lowercase`() {
        assertEquals(Rgb(151, 201, 61), parseHex("97C93D"))
        assertEquals(Rgb(151, 201, 61), parseHex("#97c93d"))
        assertEquals(Rgb(151, 201, 61), parseHex("  97C93D "))
    }

    @Test fun `treats black as absent`() {
        assertNull(parseHex("000000")) // HUIA: invisible on a dark ground
    }

    @Test fun `rejects garbage`() {
        for (s in listOf(null, "", "zz", "12345", "1234567", "GGGGGG")) assertNull(parseHex(s), "$s")
    }

    @Test fun `packs ARGB for Android`() {
        assertEquals(0xFF97C93D.toInt(), Rgb(0x97, 0xC9, 0x3D).argb)
    }
}
```

- [ ] **Step 2: Run them and watch them fail**

Run: `./gradlew :core:test`
Expected: FAIL to compile — `Unresolved reference 'AtUrls'`.

- [ ] **Step 3: Write `RouteColor.kt`**

```kotlin
package io.github.msmgreen.nextdeparture.core

import kotlinx.serialization.Serializable

@Serializable
data class Rgb(val r: Int, val g: Int, val b: Int) {
    val argb: Int get() = (0xFF shl 24) or (r shl 16) or (g shl 8) or b
}

// '97C93D' or '#97C93D'. Null for anything unusable - including #000000, which
// AT sends for HUIA and which would be invisible (lib/core/src/color.cpp).
fun parseHex(s: String?): Rgb? {
    val hex = s?.trim()?.trimStart('#') ?: return null
    if (hex.length != 6 || !hex.all { it in '0'..'9' || it in 'a'..'f' || it in 'A'..'F' }) return null
    val v = hex.toInt(16)
    if (v == 0) return null
    return Rgb(v shr 16 and 0xFF, v shr 8 and 0xFF, v and 0xFF)
}
```

- [ ] **Step 4: Write `AtApi.kt`**

```kotlin
package io.github.msmgreen.nextdeparture.core

import kotlinx.serialization.Serializable
import kotlinx.serialization.SerializationException
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.intOrNull
import java.time.LocalDate

// URLs and parsers for the two AT APIs. Pure: no networking here, so every
// shape below is covered by a fixture in test/fixtures/. docs/at-api-notes.md
// records why each of these looks the way it does.

const val KEY_HEADER = "Ocp-Apim-Subscription-Key"

object AtUrls {
    private const val GTFS = "https://api.at.govt.nz/gtfs/v3"
    private const val REALTIME = "https://api.at.govt.nz/realtime/legacy"

    fun stopByCode(stopCode: String) = "$GTFS/stops?filter%5Bstop_code%5D=$stopCode"

    fun routesByShortName(shortName: String) = "$GTFS/routes?filter%5Broute_short_name%5D=$shortName"

    fun railRoutes() = "$GTFS/routes?filter%5Broute_type%5D=2"

    // startHour must be 1..47. The API answers 400 for 0 (a required-tag
    // zero-value check), and accepts 24 and above: after-midnight trips are
    // filed under the previous service date as 24:xx, 25:xx (verified 2026-09-19).
    fun stoptrips(stopId: String, date: LocalDate, startHour: Int, hourRange: Int): String? {
        if (startHour !in 1..47 || hourRange < 1) return null
        return "$GTFS/stops/$stopId/stoptrips?filter%5Bdate%5D=$date" +
            "&filter%5Bstart_hour%5D=$startHour&filter%5Bhour_range%5D=$hourRange"
    }

    fun tripStops(tripId: String) = "$GTFS/trips/$tripId/stops"

    fun realtime(tripIds: List<String>): String? =
        if (tripIds.isEmpty()) null else "$REALTIME/tripupdates?tripid=${tripIds.joinToString(",")}"
}

@Serializable
data class StopInfo(val stopId: String, val stopName: String, val locationType: Int)

@Serializable
data class RouteInfo(val routeId: String, val shortName: String, val routeType: Int, val color: Rgb?)

data class StopTripRow(
    val tripId: String,
    val routeId: String,
    val stopId: String, // the platform, for a station's rows
    val directionId: Int,
    val serviceDate: LocalDate,
    val departureS: Int, // seconds from the service day's noon minus 12h
)

data class TripStop(val stopId: String, val stopCode: String, val parentStation: String)

data class RtEntity(
    val tripId: String,
    val delay: Int?, // trip level, signed; negative is early
    val cancelled: Boolean,
    val stuStopId: String,
    val stuDepartureDelay: Int?,
)

// Every parser returns null when the body is not the JSON shape it expects at
// all, and an empty list when AT answered with no rows. An unknown stop code
// is the second kind: 200 with {"data":[]} (docs/at-api-notes.md).
object AtParse {
    private val json = Json { ignoreUnknownKeys = true }

    private fun root(body: String): JsonObject? = try {
        json.parseToJsonElement(body) as? JsonObject
    } catch (e: SerializationException) {
        null
    }

    private fun JsonElement?.obj(): JsonObject? = this as? JsonObject

    private fun JsonObject.str(key: String): String =
        (this[key] as? JsonPrimitive)?.takeIf { it.isString }?.content ?: ""

    private fun JsonObject.int(key: String): Int? = (this[key] as? JsonPrimitive)?.intOrNull

    private fun attributes(body: String): List<JsonObject>? {
        val data = root(body)?.get("data") as? JsonArray ?: return null
        return data.mapNotNull { it.obj()?.get("attributes").obj() }
    }

    fun stops(body: String): List<StopInfo>? = attributes(body)?.mapNotNull { a ->
        StopInfo(a.str("stop_id"), a.str("stop_name"), a.int("location_type") ?: 0)
            .takeIf { it.stopId.isNotEmpty() }
    }

    fun routes(body: String): List<RouteInfo>? = attributes(body)?.mapNotNull { a ->
        RouteInfo(a.str("route_id"), a.str("route_short_name"), a.int("route_type") ?: 0, parseHex(a.str("route_color")))
            .takeIf { it.routeId.isNotEmpty() }
    }

    fun stoptrips(body: String): List<StopTripRow>? = attributes(body)?.mapNotNull { a ->
        val secs = parseGtfsTime(a.str("departure_time")) ?: return@mapNotNull null
        val date = parseIsoDate(a.str("service_date")) ?: return@mapNotNull null
        StopTripRow(a.str("trip_id"), a.str("route_id"), a.str("stop_id"), a.int("direction_id") ?: -1, date, secs)
            .takeIf { it.tripId.isNotEmpty() }
    }

    // Order is positional - these rows carry no stop_sequence, so never sort them.
    fun tripStops(body: String): List<TripStop>? = attributes(body)?.mapNotNull { a ->
        TripStop(a.str("stop_id"), a.str("stop_code"), a.str("parent_station"))
            .takeIf { it.stopId.isNotEmpty() }
    }

    fun realtime(body: String): List<RtEntity>? {
        val entities = root(body)?.get("response").obj()?.get("entity") as? JsonArray ?: return null
        return entities.mapNotNull { ent ->
            val tu = ent.obj()?.get("trip_update").obj() ?: return@mapNotNull null
            val trip = tu["trip"].obj()
            // The docs promise an array; the API sends one object. Accept both,
            // taking the first element of an array.
            val stuRaw = tu["stop_time_update"]
            val stu = (if (stuRaw is JsonArray) stuRaw.firstOrNull() else stuRaw).obj()
            RtEntity(
                tripId = trip?.str("trip_id") ?: "",
                delay = tu.int("delay"),
                cancelled = trip?.int("schedule_relationship") == 3,
                stuStopId = stu?.str("stop_id") ?: "",
                stuDepartureDelay = stu?.get("departure").obj()?.int("delay"),
            ).takeIf { it.tripId.isNotEmpty() }
        }
    }

    // The first error's detail from a 4xx body, for the log. AT's 400 for
    // start_hour=0 is the only one captured (stoptrips_start_hour_0.json).
    fun errorDetail(body: String): String? {
        val errors = root(body)?.get("errors") as? JsonArray ?: return null
        val first = errors.firstOrNull().obj() ?: return null
        return first.str("detail").ifEmpty { first.str("title") }.ifEmpty { null }
    }
}
```

- [ ] **Step 5: Run them**

Run: `./gradlew :core:test`
Expected: PASS, 20 tests.

- [ ] **Step 6: Commit**

```bash
git add android/core
git commit -m "Port the AT URLs and parsers, held to the board's fixtures

Parsers return null for a body that is not the JSON they expect and an
empty list for an answer with no rows, which is the line between a
transport fault and an unknown stop code."
```

---

### Task 3: The live rules

`lib/core/src/live.cpp`, rule for rule, as functions over immutable lists.
`build_board`, `select_rows` and `choose_direction` are not ported: the widget
has no board, and the fetcher stopped using the other two once direction
became a property of the `(route_id, direction_id)` pair.

**Files:**
- Create: `$CORE/Live.kt`
- Test: `$CORE_TEST/LiveTest.kt`

**Interfaces:**
- Consumes: Task 2's types, `gtfsEpoch` (Task 1)
- Produces: `WatchState`, `stateMessage`, `LiveRow` (with `predicted`), `RouteDir`, `tripServes`, `collectRouteDirs`, `selectServingRows`, `verdictForPairs`, `applyRealtime`, `carryRealtime`, `realtimeIds`, `FetchWindow`, `scheduleWindows`, `Kind`, `Badge`, `badgeFor`, and the caps `MAX_ROWS`, `MAX_ROUTE_DIRS`, `RT_PER_WATCH` — used by Tasks 4, 6 and 7

- [ ] **Step 1: Write the failing test**

`$CORE_TEST/LiveTest.kt` — `test/test_live` case for case, less the
`build_board` cases, plus the pair cap, a pinned route and "no hour of the day
asks for 0":

```kotlin
package io.github.msmgreen.nextdeparture.core

import java.time.LocalDate
import java.time.LocalDateTime
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

// test/test_live, case for case. build_board, select_rows and choose_direction
// are not ported: the widget has no board, and the fetcher never used the other
// two once direction became per (route, direction) pair.
class LiveTest {
    private fun stop(id: String, code: String, parent: String) = TripStop(id, code, parent)

    private fun row(trip: String, route: String, dir: Int, secs: Int) =
        StopTripRow(trip, route, "9305-ef07ca76", dir, LocalDate.of(2026, 9, 13), secs)

    private fun live(trip: String, sched: Long, hasRt: Boolean = false, delay: Int = 0, stopId: String = "") =
        LiveRow(trip, stopId, sched, hasRt, delay)

    @Test fun `a trip serves a target that comes after us`() {
        // The real dir-1 trip: Kingsland platform 9304, then Waitemata platform
        // 9001, whose parent is station 133.
        val stops = listOf(
            stop("9328-f6f84eac", "9328", "127-2affe079"),
            stop("9304-dcb2ed75", "9304", "122-34ecc043"),
            stop("9001-11111111", "9001", "133-08da14b5"),
        )
        assertTrue(tripServes(stops, "9304-dcb2ed75", "133", "133-08da14b5"))
    }

    @Test fun `a trip going the other way does not serve it`() {
        val stops = listOf(
            stop("9004-22222222", "9004", "133-08da14b5"),
            stop("9305-ef07ca76", "9305", "122-34ecc043"),
            stop("9328-f6f84eac", "9328", "127-2affe079"),
        )
        assertFalse(tripServes(stops, "9305-ef07ca76", "133", "133-08da14b5"))
    }

    @Test fun `a bus target matches on stop code alone`() {
        val stops = listOf(stop("8213-7e021a72", "8213", ""), stop("1060-00b64ee7", "1060", ""))
        assertTrue(tripServes(stops, "8213-7e021a72", "1060", "1060-00b64ee7"))
    }

    @Test fun `a trip that never calls at our stop serves nothing`() {
        val stops = listOf(stop("1111-aaaa", "1111", ""), stop("1060-00b64ee7", "1060", ""))
        assertFalse(tripServes(stops, "8213-7e021a72", "1060", "1060-00b64ee7"))
    }

    @Test fun `route dirs are collected per route and direction`() {
        val rows = listOf(
            row("ew0", "E-W-201", 0, 15 * 3600),
            row("ow0", "O-W-201", 0, 15 * 3600 + 60),
            row("ew1", "E-W-201", 1, 15 * 3600 + 120),
            row("ow1", "O-W-201", 1, 15 * 3600 + 180),
            row("ew0b", "E-W-201", 0, 15 * 3600 + 240), // same pair again
        )
        val pairs = collectRouteDirs(rows)
        assertEquals(4, pairs.size)
        assertEquals(RouteDir("E-W-201", 0, "ew0", "9305-ef07ca76"), pairs[0]) // first trip of that pair
        assertEquals("O-W-201", pairs[1].routeId)
        assertEquals("ew1", pairs[2].tripId)
    }

    @Test fun `the pair cap keeps the first pairs seen`() {
        val rows = (0 until 8).map { row("t$it", "R$it-1", 0, 15 * 3600 + it) }
        assertEquals(listOf("R0-1", "R1-1", "R2-1", "R3-1", "R4-1", "R5-1"), collectRouteDirs(rows).map { it.routeId })
    }

    @Test fun `only the serving line is shown`() {
        // The bug: O-W does not reach Waitemata, E-W does. Rows of the line that
        // does not go your way must never appear.
        val rows = listOf(row("ow1", "O-W-201", 1, 15 * 3600 + 60), row("ew1", "E-W-201", 1, 15 * 3600 + 120))
        val pairs = collectRouteDirs(rows).map { it.copy(serves = it.routeId == "E-W-201") }
        assertEquals(listOf("ew1"), selectServingRows(rows, pairs, emptyList(), T_15_00).map { it.tripId })
    }

    @Test fun `a pinned route filters the serving rows`() {
        val rows = listOf(
            row("a", "E-W-201", 1, 15 * 3600 + 13 * 60),
            row("c", "O-W-201", 1, 15 * 3600 + 20 * 60),
            row("d", "E-W-201", 1, 15 * 3600 + 30 * 60),
        )
        val pairs = collectRouteDirs(rows).map { it.copy(serves = true) }
        val out = selectServingRows(rows, pairs, listOf("E-W-201"), T_15_00)
        assertEquals(listOf("a", "d"), out.map { it.tripId })
        assertEquals(T_15_00 + 13 * 60, out[0].schedEpoch)
    }

    @Test fun `recent departures are kept because delays can be large`() {
        // Scheduled 10 minutes ago, but a bus can run 7 minutes late; dropping
        // it here would lose a service that is still to come.
        val rows = listOf(row("a", "E-W-201", 1, 14 * 3600 + 50 * 60), row("b", "E-W-201", 1, 13 * 3600))
        val pairs = collectRouteDirs(rows).map { it.copy(serves = true) }
        assertEquals(listOf("a"), selectServingRows(rows, pairs, emptyList(), T_15_00).map { it.tripId })
    }

    @Test fun `verdict is check config only when a route runs both ways in vain`() {
        val pairs = listOf(RouteDir("O-W-201", 0, "x", "s"), RouteDir("O-W-201", 1, "y", "s"))
        // One line, both directions, neither reaches the target: the target is
        // wrong for this stop.
        assertEquals(WatchState.CheckConfig, verdictForPairs(pairs))
        // One line, one direction, not reaching it: nothing is due your way.
        assertEquals(WatchState.Ok, verdictForPairs(pairs.take(1)))
        // Anything serving at all is Ok.
        assertEquals(WatchState.Ok, verdictForPairs(listOf(pairs[0], pairs[1].copy(serves = true))))
    }

    @Test fun `no pairs at all is an empty window`() {
        assertEquals(WatchState.Ok, verdictForPairs(emptyList()))
    }

    @Test fun `realtime prefers our own stop time update`() {
        val rows = listOf(live("t1", 0, stopId = "9304-dcb2ed75"), live("t2", 0, stopId = "9304-dcb2ed75"))
        val ents = listOf(
            RtEntity("t1", -28, false, "9304-dcb2ed75", 66),
            RtEntity("t2", -427, false, "1060-00b64ee7", 999),
            RtEntity("stranger", 12345, false, "", null), // the tripid filter is inexact
        )
        val out = applyRealtime(rows, ents, listOf("t1", "t2"))
        assertTrue(out[0].hasRt)
        assertEquals(66, out[0].delay) // our stop wins
        assertEquals(-427, out[1].delay) // someone else's is ignored
    }

    @Test fun `realtime marks cancellations`() {
        val out = applyRealtime(listOf(live("t1", 0)), listOf(RtEntity("t1", null, true, "", null)), listOf("t1"))
        assertTrue(out[0].cancelled)
    }

    @Test fun `a requested trip the feed forgets stops being live`() {
        val rows = listOf(
            LiveRow("asked", "", 0, hasRt = true, delay = 240, cancelled = true),
            LiveRow("not-asked", "", 0, hasRt = true, delay = 60),
        )
        val out = applyRealtime(rows, listOf(RtEntity("someone-else", 12, false, "", null)), listOf("asked"))
        assertEquals(LiveRow("asked", "", 0), out[0]) // no longer claimed as live
        assertEquals(rows[1], out[1]) // never asked about: untouched
    }

    @Test fun `realtime ids ask only about services still to come`() {
        val rows = listOf(live("gone", T_15_00 - 3600), live("soon", T_15_00 + 300), live("later", T_15_00 + 900))
        assertEquals(listOf("soon", "later"), realtimeIds(rows, T_15_00))
        assertEquals(listOf("soon"), realtimeIds(rows, T_15_00, cap = 1))
    }

    @Test fun `a late bus survives a schedule refresh`() {
        // Scheduled 15:00, realtime says +300. At 15:02 the 15-minute schedule
        // refresh rebuilds the rows. The bus is still 3 minutes out and must stay.
        val rows = listOf(row("late", "20-202", 0, 15 * 3600), row("next", "20-202", 0, 15 * 3600 + 1200))
        val pairs = collectRouteDirs(rows).map { it.copy(serves = true) }
        var w = selectServingRows(rows, pairs, emptyList(), T_15_00 - 600)
        assertEquals(2, w.size)
        w = applyRealtime(w, listOf(RtEntity("late", 300, false, "", null)), listOf("late", "next"))
        assertTrue(w[0].hasRt)

        val now = T_15_00 + 120
        w = carryRealtime(w, selectServingRows(rows, pairs, emptyList(), now))
        assertTrue(w[0].hasRt)
        assertEquals(300, w[0].delay)
        assertFalse(w[1].hasRt) // nothing was said about it
        assertEquals(listOf("late", "next"), realtimeIds(w, now))
        assertEquals(180L, w[0].predicted - now) // still three minutes out
    }

    @Test fun `carry realtime matches by trip id, not position`() {
        val old = listOf(LiveRow("a", "", 0), LiveRow("b", "", 0, hasRt = true, delay = -45, cancelled = true))
        val out = carryRealtime(old, listOf(LiveRow("b", "", 0), LiveRow("c", "", 0)))
        assertEquals(LiveRow("b", "", 0, hasRt = true, delay = -45, cancelled = true), out[0])
        assertEquals(LiveRow("c", "", 0), out[1]) // a new trip starts with no realtime
    }

    @Test fun `a never-heard-of row five minutes past is still asked about`() {
        assertEquals(listOf("maybe-late"), realtimeIds(listOf(live("maybe-late", T_15_00 - 300)), T_15_00))
    }

    @Test fun `a never-heard-of row twenty minutes past is not`() {
        assertEquals(emptyList(), realtimeIds(listOf(live("gone", T_15_00 - 1200)), T_15_00))
    }

    @Test fun `a heard-of row that has left is not asked about`() {
        // Left three minutes ago.
        assertEquals(emptyList(), realtimeIds(listOf(live("left", T_15_00 - 300, hasRt = true, delay = 120)), T_15_00))
    }

    private fun at(y: Int, m: Int, d: Int, hour: Int) = LocalDateTime.of(y, m, d, hour, 0).atZone(AKL)

    private fun window(y: Int, m: Int, d: Int, start: Int) = FetchWindow(LocalDate.of(y, m, d), start, 3)

    @Test fun `schedule windows cover three hours from now`() {
        assertEquals(listOf(window(2026, 9, 19, 17)), scheduleWindows(at(2026, 9, 19, 17)))
    }

    @Test fun `schedule windows run past midnight on the same service date`() {
        // The API does not clamp at midnight: 23:00 for three hours returns
        // 23:04 through 25:25 against one date (verified live 2026-09-19).
        assertEquals(listOf(window(2026, 9, 19, 23)), scheduleWindows(at(2026, 9, 19, 23)))
    }

    @Test fun `schedule windows at midnight ask yesterday for its late services`() {
        assertEquals(listOf(window(2026, 9, 19, 24), window(2026, 9, 20, 1)), scheduleWindows(at(2026, 9, 20, 0)))
    }

    @Test fun `schedule windows in the small hours`() {
        assertEquals(listOf(window(2026, 9, 19, 26), window(2026, 9, 20, 2)), scheduleWindows(at(2026, 9, 20, 2)))
        // From 04:00 one window is enough again.
        assertEquals(listOf(window(2026, 9, 20, 4)), scheduleWindows(at(2026, 9, 20, 4)))
    }

    @Test fun `schedule windows roll back over a month and a year end`() {
        assertEquals(listOf(window(2026, 9, 30, 24), window(2026, 10, 1, 1)), scheduleWindows(at(2026, 10, 1, 0)))
        assertEquals(listOf(window(2026, 12, 31, 25), window(2027, 1, 1, 1)), scheduleWindows(at(2027, 1, 1, 1)))
    }

    @Test fun `no hour of the day ever asks for start hour zero`() {
        for (h in 0..23) {
            for (w in scheduleWindows(at(2026, 9, 20, h))) {
                assertTrue(w.startHour in 1..47, "hour $h asked for ${w.startHour}")
            }
        }
    }

    @Test fun `badges come from the rail table or the route id`() {
        val rail = AtParse.routes(fixture("post-crl/routes_rail.json"))!!
        assertEquals(Badge("E-W", Kind.Train, Rgb(0x97, 0xC9, 0x3D)), badgeFor("E-W-201", rail))
        assertEquals(Badge("20", Kind.Bus, null), badgeFor("20-202", rail))
    }

    @Test fun `state messages`() {
        assertEquals("", stateMessage(WatchState.Ok))
        assertEquals("Check settings", stateMessage(WatchState.CheckConfig))
        assertEquals("Check API key", stateMessage(WatchState.CheckKey))
        assertEquals("Starting", stateMessage(WatchState.Starting))
        assertEquals("Stop lookup gone", stateMessage(WatchState.StopLookupGone))
    }
}
```

- [ ] **Step 2: Run it and watch it fail**

Run: `./gradlew :core:test`
Expected: FAIL to compile — `Unresolved reference 'LiveRow'`.

- [ ] **Step 3: Write `Live.kt`**

`selectServingRows` takes the first `cap` qualifying rows in API order and then
sorts them, exactly as the C++ stops filling at `cap` before its insertion
sort; `sortedBy` is stable, as that sort is.

```kotlin
package io.github.msmgreen.nextdeparture.core

import kotlinx.serialization.Serializable
import java.time.ZonedDateTime

// Everything between "the API answered" and "the widget draws", ported from
// lib/core/src/live.cpp rule for rule. Pure, so the rules that decide what the
// widget claims are tested on a laptop. Where a comment here is terse, the C++
// it came from says why at length.

enum class WatchState { Starting, Ok, CheckConfig, CheckKey, StopLookupGone }

// What the widget says instead of a time. "check config" on the board reads
// "Check settings" here, because that is where the phone's config lives.
fun stateMessage(s: WatchState): String = when (s) {
    WatchState.Ok -> ""
    WatchState.Starting -> "Starting"
    WatchState.CheckConfig -> "Check settings"
    WatchState.CheckKey -> "Check API key"
    WatchState.StopLookupGone -> "Stop lookup gone"
}

const val MAX_ROWS = 16 // scheduled departures kept per watch
const val MAX_ROUTE_DIRS = 6 // a stop served by more keeps the first six pairs
const val RT_PER_WATCH = 6 // trips asked about per realtime request

// A service scheduled this long ago may still be ahead of us: delays of -427 s
// are real, and so are late ones.
private const val KEEP_PAST_S = 1800L

// How long past its scheduled time a trip realtime has never mentioned is
// still asked about. It may be late rather than gone.
private const val NEVER_HEARD_S = 900L

@Serializable
data class LiveRow(
    val tripId: String,
    val stopId: String,
    val schedEpoch: Long,
    val hasRt: Boolean = false,
    val delay: Int = 0,
    val cancelled: Boolean = false,
) {
    // When it will actually leave, as far as anyone knows.
    val predicted: Long get() = schedEpoch + if (hasRt) delay else 0
}

// One (route_id, direction_id) combination seen in the window, with a trip to
// ask about and whether that trip reaches the target.
data class RouteDir(
    val routeId: String,
    val directionId: Int,
    val tripId: String, // representative trip, for trips/{id}/stops
    val stopId: String, // our stop as that trip calls it (the platform)
    val serves: Boolean = false,
)

// Does this trip call at our stop and then reach the target? Platforms carry a
// parent_station, so a station target matches either the code or the parent.
fun tripServes(stops: List<TripStop>, ourStopId: String, targetCode: String, targetStopId: String): Boolean {
    val ours = stops.indexOfFirst { it.stopId == ourStopId }
    if (ours < 0) return false
    return stops.drop(ours + 1).any {
        it.stopCode == targetCode || (targetStopId.isNotEmpty() && it.parentStation == targetStopId)
    }
}

// Distinct (route_id, direction_id) pairs in the rows, in first-seen order,
// each with the first trip seen for that pair. Direction is a property of the
// pair, not of a trip: at a station served by two lines, one line can reach
// the target and the other not (board spec 3a).
fun collectRouteDirs(rows: List<StopTripRow>, cap: Int = MAX_ROUTE_DIRS): List<RouteDir> {
    val out = mutableListOf<RouteDir>()
    for (r in rows) {
        if (r.directionId !in 0..1) continue
        if (out.any { it.directionId == r.directionId && it.routeId == r.routeId }) continue
        if (out.size >= cap) break
        out += RouteDir(r.routeId, r.directionId, r.tripId, r.stopId)
    }
    return out
}

// Rows belonging to a pair whose `serves` is true, filtered by route, scheduled
// recently enough to still matter, sorted by scheduled time (stably).
fun selectServingRows(
    rows: List<StopTripRow>,
    pairs: List<RouteDir>,
    routeIds: List<String>,
    now: Long,
    cap: Int = MAX_ROWS,
): List<LiveRow> = rows.asSequence()
    .filter { r -> pairs.any { it.serves && it.directionId == r.directionId && it.routeId == r.routeId } }
    .filter { routeIds.isEmpty() || it.routeId in routeIds }
    .map { LiveRow(it.tripId, it.stopId, gtfsEpoch(it.serviceDate, it.departureS)) }
    .filter { it.schedEpoch >= now - KEEP_PAST_S }
    .take(cap)
    .sortedBy { it.schedEpoch }
    .toList()

// Ok when at least one pair serves; CheckConfig when none does AND some route
// had both directions present; otherwise Ok with nothing to show. One direction
// of one line simply means nothing is due your way right now.
fun verdictForPairs(pairs: List<RouteDir>): WatchState {
    if (pairs.any { it.serves }) return WatchState.Ok
    val bothWays = pairs.any { a -> pairs.any { b -> a.routeId == b.routeId && a.directionId != b.directionId } }
    return if (bothWays) WatchState.CheckConfig else WatchState.Ok
}

// Applies delays and cancellations to rows whose trip_id we asked for; entities
// for any other trip are discarded (the tripid filter is inexact). A row we DID
// ask about and heard nothing back for has its realtime cleared: an old delay
// must never keep being shown as live.
fun applyRealtime(rows: List<LiveRow>, ents: List<RtEntity>, requested: Collection<String>): List<LiveRow> =
    rows.map { row ->
        val ent = ents.firstOrNull { it.tripId == row.tripId }
        when {
            ent == null && row.tripId in requested -> row.copy(hasRt = false, delay = 0, cancelled = false)
            ent == null -> row
            // The per-stop update usually describes the vehicle's next stop,
            // not ours; prefer it only when it really is ours.
            ent.stuDepartureDelay != null && ent.stuStopId == row.stopId ->
                row.copy(cancelled = ent.cancelled, hasRt = true, delay = ent.stuDepartureDelay)
            ent.delay != null -> row.copy(cancelled = ent.cancelled, hasRt = true, delay = ent.delay)
            else -> row.copy(cancelled = ent.cancelled)
        }
    }

// A schedule refresh rebuilds every row from scratch, which would forget what
// realtime already said. Without this a bus running late is dropped as
// "departed" at every 15-minute refresh.
fun carryRealtime(old: List<LiveRow>, fresh: List<LiveRow>): List<LiveRow> = fresh.map { row ->
    val was = old.firstOrNull { it.tripId == row.tripId } ?: return@map row
    row.copy(hasRt = was.hasRt, delay = was.delay, cancelled = was.cancelled)
}

// Trip ids worth asking realtime about: those still to come, at most cap. A row
// realtime has spoken about is still to come until sched + delay is a minute
// gone; a row it never has may be late rather than gone, so it is asked about
// for 15 minutes past its scheduled time.
fun realtimeIds(rows: List<LiveRow>, now: Long, cap: Int = RT_PER_WATCH): List<String> = rows.asSequence()
    .filter { it.tripId.isNotEmpty() }
    .filter { if (it.hasRt) it.schedEpoch + it.delay >= now - 60 else it.schedEpoch >= now - NEVER_HEARD_S }
    .map { it.tripId }
    .take(cap)
    .toList()

data class FetchWindow(val date: java.time.LocalDate, val startHour: Int, val hourRange: Int)

private const val LOOKAHEAD_H = 3
private const val SMALL_HOURS_END = 4 // no service date runs later than 27:00

// The stoptrips request(s) covering the next three hours. A service date runs
// past midnight (its late trips are 24:xx, 25:xx) and the API does not clamp
// hour_range at 24, so from 04:00 one request covers it. In the small hours the
// trains still running belong to yesterday's service date, so a second request
// asks yesterday from 24 + hour; today's own is asked from max(hour, 1).
fun scheduleWindows(now: ZonedDateTime): List<FetchWindow> {
    val today = now.toLocalDate()
    if (now.hour >= SMALL_HOURS_END) return listOf(FetchWindow(today, now.hour, LOOKAHEAD_H))
    return listOf(
        FetchWindow(today.minusDays(1), 24 + now.hour, LOOKAHEAD_H),
        FetchWindow(today, maxOf(now.hour, 1), LOOKAHEAD_H),
    )
}

enum class Kind { Bus, Train }

@Serializable
data class Badge(val text: String, val kind: Kind, val color: Rgb?)

// A route in the rail list is a train, badged and coloured from the feed. Any
// other route is a bus, badged with its route_id up to the last '-' ("20-202"
// -> "20"), and uncoloured: AT does not colour buses (docs/at-api-notes.md).
fun badgeFor(routeId: String, rail: List<RouteInfo>): Badge {
    val r = rail.firstOrNull { it.routeId == routeId }
    if (r != null) return Badge(r.shortName, Kind.Train, r.color)
    val dash = routeId.lastIndexOf('-')
    return Badge((if (dash >= 0) routeId.substring(0, dash) else routeId).take(7), Kind.Bus, null)
}
```

- [ ] **Step 4: Run it**

Run: `./gradlew :core:test`
Expected: PASS, 48 tests.

- [ ] **Step 5: Commit**

```bash
git add android/core
git commit -m "Port the live rules: direction per pair, rows, realtime, windows"
```

---

### Task 4: The catchable departure, and freshness

The two rules the board does not have (widget spec 5 and 10).

**Files:**
- Create: `$CORE/Departure.kt`, `$CORE/Freshness.kt`
- Test: `$CORE_TEST/DepartureTest.kt`, `$CORE_TEST/FreshnessTest.kt`

**Interfaces:**
- Consumes: `LiveRow` (Task 3)
- Produces: `Departure`, `Choice`, `chooseDeparture`; `SCHEDULE_PERIOD_S`, `RT_FAST_S`, `STALE_AFTER_S`, `WIDGET_STALE_AFTER_S`, `isStale`, `staleAt`, `lastOkAfter` — used by Tasks 6 and 7

- [ ] **Step 1: Write the failing tests**

`$CORE_TEST/DepartureTest.kt`:

```kotlin
package io.github.msmgreen.nextdeparture.core

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class DepartureTest {
    private val now = T_15_00

    private fun row(trip: String, sched: Long, delay: Int? = null, cancelled: Boolean = false) =
        LiveRow(trip, "", sched, hasRt = delay != null, delay = delay ?: 0, cancelled = cancelled)

    @Test fun `leave-by exactly now is catchable, a second earlier is not`() {
        assertEquals("a", chooseDeparture(listOf(row("a", now + 300)), now, walkS = 300).departure?.row?.tripId)
        assertNull(chooseDeparture(listOf(row("a", now + 299)), now, walkS = 300).departure)
    }

    @Test fun `an early-running bus moves leave-by earlier`() {
        val d = chooseDeparture(listOf(row("a", now + 600, delay = -180)), now, walkS = 300).departure!!
        assertEquals(now + 120, d.leaveBy)
    }

    @Test fun `order is by when it will actually leave, not by timetable`() {
        val rows = listOf(row("late", now + 600, delay = 600), row("next", now + 900))
        assertEquals("next", chooseDeparture(rows, now, walkS = 0).departure!!.row.tripId)
    }

    @Test fun `a row without realtime uses its schedule`() {
        val d = chooseDeparture(listOf(row("a", now + 600)), now, walkS = 60).departure!!
        assertEquals(now + 540, d.leaveBy)
    }

    @Test fun `the cancelled service you would have caught is reported`() {
        val rows = listOf(row("gone", now + 300, cancelled = true), row("next", now + 600))
        val c = chooseDeparture(rows, now, walkS = 0)
        assertEquals("next", c.departure!!.row.tripId)
        assertEquals("gone", c.cancelled!!.tripId)
    }

    @Test fun `a cancellation after the one you catch is not`() {
        val rows = listOf(row("next", now + 300), row("gone", now + 600, cancelled = true))
        assertNull(chooseDeparture(rows, now, walkS = 0).cancelled)
    }

    @Test fun `a cancellation you could not have reached anyway is not`() {
        val rows = listOf(row("gone", now + 60, cancelled = true), row("next", now + 600))
        assertNull(chooseDeparture(rows, now, walkS = 300).cancelled)
    }

    @Test fun `a late bus is still the one to catch`() {
        // The board's late-bus case: scheduled 15:00, +300, now 15:02.
        val rows = listOf(row("late", now, delay = 300), row("next", now + 1200))
        val d = chooseDeparture(rows, now + 120, walkS = 0).departure!!
        assertEquals("late", d.row.tripId)
        assertEquals(now + 300, d.leaveBy)
    }

    @Test fun `nothing reachable is no departure`() {
        assertEquals(Choice(null, null), chooseDeparture(listOf(row("a", now - 60)), now, walkS = 0))
        assertEquals(Choice(null, null), chooseDeparture(emptyList(), now, walkS = 0))
    }
}
```

`$CORE_TEST/FreshnessTest.kt`:

```kotlin
package io.github.msmgreen.nextdeparture.core

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class FreshnessTest {
    @Test fun `stale after two minutes, and not at two minutes`() {
        assertFalse(isStale(T_15_00, T_15_00 + 120))
        assertTrue(isStale(T_15_00, T_15_00 + 121))
        assertEquals(T_15_00 + 121, staleAt(T_15_00))
    }

    @Test fun `never fetched is stale`() {
        assertTrue(isStale(0, T_15_00))
    }

    @Test fun `news on a fresh schedule moves last ok`() {
        assertEquals(T_15_00, lastOkAfter(previous = 1, now = T_15_00, news = true, schedOkAt = T_15_00 - 600))
    }

    @Test fun `no news leaves last ok where it was`() {
        assertEquals(1L, lastOkAfter(previous = 1, now = T_15_00, news = false, schedOkAt = T_15_00))
    }

    @Test fun `realtime on a sixteen-minute-old schedule is still stale`() {
        // The schedule refresh failed and realtime answered: the rows may be
        // missing services, so the widget must not claim to be fresh.
        val before = T_15_00 - 300
        val after = lastOkAfter(previous = before, now = T_15_00, news = true, schedOkAt = T_15_00 - 16 * 60)
        assertEquals(before, after)
        assertTrue(isStale(after, T_15_00))
    }

    @Test fun `a schedule that never arrived is no basis for freshness`() {
        assertEquals(0L, lastOkAfter(previous = 0, now = T_15_00, news = true, schedOkAt = 0))
    }
}
```

- [ ] **Step 2: Run them and watch them fail**

Run: `./gradlew :core:test`
Expected: FAIL to compile — `Unresolved reference 'chooseDeparture'`.

- [ ] **Step 3: Write `Departure.kt`**

```kotlin
package io.github.msmgreen.nextdeparture.core

// The one rule the board does not have. The board shows four lanes and lets you
// judge; a one-line widget has to judge for you (widget spec 5).

data class Departure(val row: LiveRow, val leaveBy: Long)

// cancelled is the cancelled service you would otherwise have caught, so the
// widget can say so rather than silently showing a later one (board spec 8:
// cancellations are surfaced, never dropped).
data class Choice(val departure: Departure?, val cancelled: LiveRow?)

// The first row you can still reach, ordered by when it will actually leave
// rather than by timetable: a bus running early leaves sooner than its slot.
fun chooseDeparture(rows: List<LiveRow>, now: Long, walkS: Int): Choice {
    val reachable = rows.filter { it.predicted - walkS >= now }.sortedBy { it.predicted }
    val next = reachable.firstOrNull { !it.cancelled }
    val cancelled = reachable.firstOrNull()?.takeIf { it.cancelled }
    return Choice(next?.let { Departure(it, it.predicted - walkS) }, cancelled)
}
```

- [ ] **Step 4: Write `Freshness.kt`**

```kotlin
package io.github.msmgreen.nextdeparture.core

// How fresh the widget may claim to be (widget spec 10). The board computes
// stale_s = now - last_ok - poll_interval_s and is stale past STALE_AFTER_S
// (lib/core/src/model.h, freshness.h). A widget does not poll; the only honest
// interval it has is commute mode's 30 s, so it is stale after two minutes.

const val SCHEDULE_PERIOD_S = 15 * 60
const val RT_FAST_S = 30
const val STALE_AFTER_S = 90
const val WIDGET_STALE_AFTER_S = RT_FAST_S + STALE_AFTER_S

fun isStale(lastOk: Long, now: Long): Boolean = lastOk == 0L || now - lastOk > WIDGET_STALE_AFTER_S

// The first second at which a fresh view stops being fresh.
fun staleAt(lastOk: Long): Long = lastOk + WIDGET_STALE_AFTER_S + 1

// last_ok after a fetch. It moves only when there was news - realtime answered
// 200, or there was nothing upcoming to ask it about - AND the schedule under
// it is younger than SCHEDULE_PERIOD_S: the widget is only as fresh as its
// schedule, as board_last_ok holds for the board.
fun lastOkAfter(previous: Long, now: Long, news: Boolean, schedOkAt: Long): Long =
    if (news && schedOkAt > 0 && now - schedOkAt < SCHEDULE_PERIOD_S) now else previous
```

- [ ] **Step 5: Run them**

Run: `./gradlew :core:test`
Expected: PASS, 63 tests.

- [ ] **Step 6: Commit**

```bash
git add android/core
git commit -m "Choose the departure you can still catch, and say when that is stale

Ordered by when a service will actually leave, less the walk to the stop.
The widget is stale two minutes after its last news: the board's 90 s
plus the only interval a phone keeps, commute mode's 30 s."
```

---

### Task 5: The config

**Files:**
- Create: `$CORE/Config.kt`
- Test: `$CORE_TEST/ConfigTest.kt`

**Interfaces:**
- Consumes: nothing
- Produces: `WatchConfig` (with `key`), `Place`, `Config`, `ConfigError`, `ConfigResult`, `ConfigCodec`, `MAX_PLACES`, `CONFIG_VERSION` — used by Tasks 6, 7, 9 and 10

- [ ] **Step 1: Write the failing test**

```kotlin
package io.github.msmgreen.nextdeparture.core

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotEquals
import kotlin.test.assertTrue

class ConfigTest {
    private val home = Place("Home", WatchConfig("to Wynyard Quarter", "8213", "20", "1060"), walkMin = 4)
    private val ok = Config(places = listOf(home))

    private fun error(c: Config) = ConfigCodec.validate(c)

    private fun withWatch(w: WatchConfig) = Config(places = listOf(home.copy(watch = w)))

    @Test fun `round-trips through JSON with the board's field names`() {
        val text = ConfigCodec.toJson(ok)
        for (k in listOf("\"v\":1", "\"stop_code\"", "\"route_short_name\"", "\"toward_stop_code\"", "\"walk_min\"")) {
            assertTrue(k in text, "$k in $text")
        }
        assertEquals(ConfigResult.Ok(ok), ConfigCodec.parse(text))
    }

    @Test fun `defaults fill what is left out`() {
        val text = """{"v":1,"places":[{"name":"Work","watch":{"label":"home","stop_code":"1060","toward_stop_code":"8213"}}]}"""
        val c = (ConfigCodec.parse(text) as ConfigResult.Ok).config
        assertEquals("", c.places[0].watch.routeShortName) // any route
        assertEquals(0, c.places[0].walkMin)
        assertEquals(200, c.places[0].radiusM)
    }

    @Test fun `garbage and other versions are told apart from a bad field`() {
        assertEquals(ConfigResult.Bad(ConfigError.BadJson), ConfigCodec.parse(""))
        assertEquals(ConfigResult.Bad(ConfigError.BadJson), ConfigCodec.parse("{\"v\":1"))
        assertEquals(ConfigResult.Bad(ConfigError.BadJson), ConfigCodec.parse("{\"v\":1}"))
        assertEquals(ConfigResult.Bad(ConfigError.BadVersion), ConfigCodec.parse(ConfigCodec.toJson(ok.copy(v = 2))))
    }

    @Test fun `place counts`() {
        assertEquals(ConfigError.NoPlaces, error(Config(places = emptyList())))
        assertEquals(null, error(Config(places = List(4) { home })))
        assertEquals(ConfigError.TooManyPlaces, error(Config(places = List(5) { home })))
    }

    @Test fun `names and labels`() {
        assertEquals(ConfigError.Name, error(Config(places = listOf(home.copy(name = " ")))))
        assertEquals(ConfigError.Name, error(Config(places = listOf(home.copy(name = "x".repeat(13))))))
        assertEquals(null, error(withWatch(home.watch.copy(label = ""))))
        assertEquals(ConfigError.Label, error(withWatch(home.watch.copy(label = "x".repeat(25)))))
    }

    @Test fun `stop codes are plain ASCII letters and digits`() {
        for (bad in listOf("", "82 13", "8213;", "٨٢١٣", "x".repeat(12))) {
            assertEquals(ConfigError.StopCode, error(withWatch(home.watch.copy(stopCode = bad))), bad)
        }
    }

    @Test fun `the toward stop is required, unlike on the board`() {
        assertEquals(ConfigError.Toward, error(withWatch(home.watch.copy(towardStopCode = ""))))
    }

    @Test fun `routes may carry a dash, as rail short names do`() {
        assertEquals(null, error(withWatch(home.watch.copy(routeShortName = "E-W"))))
        assertEquals(null, error(withWatch(home.watch.copy(routeShortName = ""))))
        assertEquals(ConfigError.Route, error(withWatch(home.watch.copy(routeShortName = "20 "))))
    }

    @Test fun `walk time, radius and coordinates`() {
        assertEquals(ConfigError.WalkMin, error(Config(places = listOf(home.copy(walkMin = 31)))))
        assertEquals(ConfigError.Radius, error(Config(places = listOf(home.copy(radiusM = 99)))))
        assertEquals(ConfigError.Coordinates, error(Config(places = listOf(home.copy(lat = -36.8)))))
        assertEquals(ConfigError.Coordinates, error(Config(places = listOf(home.copy(lat = 91.0, lon = 174.7)))))
        assertEquals(null, error(Config(places = listOf(home.copy(lat = -36.8, lon = 174.7)))))
    }

    @Test fun `the cache key changes with anything that changes the departures`() {
        assertNotEquals(home.watch.key, home.watch.copy(towardStopCode = "133").key)
        assertNotEquals(home.watch.key, home.watch.copy(routeShortName = "").key)
        assertEquals(home.watch.key, home.watch.copy(label = "renamed").key)
    }
}
```

- [ ] **Step 2: Run it and watch it fail**

Run: `./gradlew :core:test`
Expected: FAIL to compile — `Unresolved reference 'Place'`.

- [ ] **Step 3: Write `Config.kt`**

```kotlin
package io.github.msmgreen.nextdeparture.core

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.SerializationException
import kotlinx.serialization.json.Json

// Each phone's configuration (widget spec 3). One JSON document with a "v"
// field, as the board stores its config in NVS (config portal spec 4), and with
// the board's field names for the watch. Coordinates live here and only here:
// this repository is public, and a place is someone's home.

const val CONFIG_VERSION = 1
const val MAX_PLACES = 4 // matches the board's MAX_WATCHES

@Serializable
data class WatchConfig(
    val label: String, // e.g. "to Wynyard Quarter"
    @SerialName("stop_code") val stopCode: String, // the number on the pole
    @SerialName("route_short_name") val routeShortName: String = "", // "" means any route
    @SerialName("toward_stop_code") val towardStopCode: String, // never a direction (board spec 3a)
) {
    // What a cached schedule belongs to. A change to any of these starts over.
    val key: String get() = "$stopCode|$routeShortName|$towardStopCode"
}

@Serializable
data class Place(
    val name: String,
    val watch: WatchConfig,
    @SerialName("walk_min") val walkMin: Int = 0,
    val lat: Double? = null, // set from phase 3, for geofences
    val lon: Double? = null,
    @SerialName("radius_m") val radiusM: Int = 200,
)

@Serializable
data class Config(val v: Int = CONFIG_VERSION, val places: List<Place>)

enum class ConfigError(val text: String) {
    BadJson("could not read the settings"),
    BadVersion("settings version is not supported"),
    NoPlaces("at least one place is required"),
    TooManyPlaces("at most four places"),
    Name("a place name is 1 to 12 characters"),
    Label("a label is at most 24 characters"),
    StopCode("a stop code is 1 to 11 letters or digits"),
    Route("a route is at most 11 letters, digits or dashes"),
    Toward("the toward stop is 1 to 11 letters or digits"),
    WalkMin("walk time is 0 to 30 minutes"),
    Radius("radius is 100 to 1000 m"),
    Coordinates("set both latitude and longitude, or neither"),
}

sealed interface ConfigResult {
    data class Ok(val config: Config) : ConfigResult
    data class Bad(val error: ConfigError) : ConfigResult
}

object ConfigCodec {
    private val json = Json { ignoreUnknownKeys = true; encodeDefaults = true }

    private fun isCode(s: String) = s.length in 1..11 && s.all { it in '0'..'9' || it in 'a'..'z' || it in 'A'..'Z' }

    private fun isRoute(s: String) =
        s.length <= 11 && s.all { it in '0'..'9' || it in 'a'..'z' || it in 'A'..'Z' || it == '-' }

    // Validation rejects rather than truncates: a truncated stop code would be
    // a silently wrong departure. Stop codes must be plain ASCII alphanumeric
    // before they reach a URL, as the portal requires (src/portal.cpp). Unlike
    // the board, the toward stop is required: one departure needs a direction.
    fun validate(c: Config): ConfigError? {
        if (c.v != CONFIG_VERSION) return ConfigError.BadVersion
        if (c.places.isEmpty()) return ConfigError.NoPlaces
        if (c.places.size > MAX_PLACES) return ConfigError.TooManyPlaces
        for (p in c.places) {
            if (p.name.isBlank() || p.name.length > 12) return ConfigError.Name
            if (p.watch.label.length > 24) return ConfigError.Label
            if (!isCode(p.watch.stopCode)) return ConfigError.StopCode
            if (!isRoute(p.watch.routeShortName)) return ConfigError.Route
            if (!isCode(p.watch.towardStopCode)) return ConfigError.Toward
            if (p.walkMin !in 0..30) return ConfigError.WalkMin
            if (p.radiusM !in 100..1000) return ConfigError.Radius
            if ((p.lat == null) != (p.lon == null)) return ConfigError.Coordinates
            if (p.lat != null && (p.lat !in -90.0..90.0 || p.lon!! !in -180.0..180.0)) return ConfigError.Coordinates
        }
        return null
    }

    fun parse(text: String): ConfigResult {
        val c = try {
            json.decodeFromString(Config.serializer(), text)
        } catch (e: SerializationException) {
            return ConfigResult.Bad(ConfigError.BadJson)
        } catch (e: IllegalArgumentException) {
            return ConfigResult.Bad(ConfigError.BadJson)
        }
        return validate(c)?.let { ConfigResult.Bad(it) } ?: ConfigResult.Ok(c)
    }

    fun toJson(c: Config): String = json.encodeToString(Config.serializer(), c)
}
```

- [ ] **Step 4: Run it**

Run: `./gradlew :core:test`
Expected: PASS, 73 tests.

- [ ] **Step 5: Commit**

```bash
git add android/core
git commit -m "Add the phone's config: places, each with one watch and a walk time

The watch keeps the board's field names and encoding. Unlike the board,
the toward stop is required: one departure needs a direction."
```

---

### Task 6: The fetcher

`src/fetcher.cpp` for one watch, as a function from a cache and the clock to
the next cache. The network is the `AtHttp` interface, so the tests play AT
with the fixtures; the app plays it with OkHttp in Task 9. The board keeps its
state in RAM for months; a phone's process is killed between almost every run,
so everything that must outlive one fetch is in `Cache`, and `Cache` is
`@Serializable`.

**Files:**
- Create: `$CORE/Fetcher.kt`
- Test: `$CORE_TEST/FetcherTest.kt`

**Interfaces:**
- Consumes: everything from Tasks 1–5
- Produces: `HttpResult`, `TRANSPORT_ERROR`, `AtHttp`, `WatchCache`, `Cache` (with `toJson`/`fromJson`), `Fetcher.refresh(cache, watch, now, force)` — used by Tasks 7, 9 and 10

**What carries over from the C++, and what does not:**

| `fetcher.cpp` | Here |
|---|---|
| Resolve once, rate-limited retries for 400/401 (`RESOLVE_RETRY_S`) | Same, per watch |
| Schedule every 15 min and on rollover; inconclusive keeps the old rows and retries in a minute | Same |
| Probe every `(route, direction)` pair; a failed probe is not evidence | Same |
| Backoff on 429, 5xx, transport and parse errors, doubling to 5 min | Same; a tap (`force`) skips the wait |
| A conclusive schedule refresh counts as news | **No.** News is a realtime 200, or nothing upcoming to ask about (`lastOkAfter`, Task 4) |
| `MAX_PINNED = 2`, `MAX_SCHED_ROWS = 48` | No fixed buffers; every route id a short name returns is kept |
| One realtime request for every watch | One watch: the current place's |

- [ ] **Step 1: Write the failing test**

Two journeys from the fixtures: Kingsland station toward Waitematā (two lines,
both ways, only E-W direction 1 serves — the bug the board's
`at-api-notes.md` records), and Kingsland Avenue toward Wynyard Quarter (a
pinned bus route at a stop three routes serve). Then every error surface of
board spec 8.

```kotlin
package io.github.msmgreen.nextdeparture.core

import java.time.LocalDate
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

// The fetch loop end to end, with AT played by the captured fixtures.
class FetcherTest {
    // Answers by exact URL and records every request; anything unexpected is a 500.
    private class FakeAt(val answers: MutableMap<String, HttpResult>) : AtHttp {
        val asked = mutableListOf<String>()

        override fun get(url: String): HttpResult {
            asked += url
            return answers[url] ?: HttpResult(500, "")
        }
    }

    private fun ok(body: String) = HttpResult(200, body)

    private fun stopsJson(vararg stops: Triple<String, String, String>) = stops.joinToString(",", """{"data":[""", "]}") {
        """{"type":"stop","attributes":{"stop_id":"${it.first}","stop_code":"${it.second}","parent_station":"${it.third}"}}"""
    }

    private val day = LocalDate.of(2026, 9, 13)

    // --- Kingsland toward Waitemata: two lines, both ways, only E-W/1 serves ---

    private val train = WatchConfig("to Waitemata", "122", "", "133")
    private val ew1 = "258-880002-52680-2-W125580-df1fa82c" // first E-W dir-1 trip, 15:13

    private fun kingsland() = FakeAt(
        mutableMapOf(
            AtUrls.railRoutes() to ok(fixture("post-crl/routes_rail.json")),
            AtUrls.stopByCode("122") to ok(fixture("post-crl/stops_122.json")),
            AtUrls.stopByCode("133") to ok(
                """{"data":[{"type":"stop","attributes":{"stop_id":"133-08da14b5","stop_name":"Waitemata Train Station","location_type":1}}]}""",
            ),
            AtUrls.stoptrips("122-34ecc043", day, 15, 3)!! to ok(fixture("post-crl/stoptrips_kingsland.json")),
            // E-W dir 1 is the real captured trip, which reaches Waitemata.
            AtUrls.tripStops(ew1) to ok(fixture("post-crl/trip_dir1_stops.json")),
            // E-W dir 0 passes Waitemata before Kingsland.
            AtUrls.tripStops("258-880001-51480-2-W118540-4f47826a") to ok(
                stopsJson(Triple("9001-x", "9001", "133-08da14b5"), Triple("9305-ef07ca76", "9305", "122-34ecc043")),
            ),
            // O-W runs to Onehunga via Newmarket, either way never Waitemata.
            AtUrls.tripStops("259-860001-54420-2-H112430-40bf48e5") to ok(
                stopsJson(Triple("9305-ef07ca76", "9305", "122-34ecc043"), Triple("9306-x", "9306", "104-ff3dff29")),
            ),
            AtUrls.tripStops("259-860002-54600-2-H113410-a4151fb4") to ok(
                stopsJson(Triple("9304-dcb2ed75", "9304", "122-34ecc043"), Triple("9291-x", "9291", "136-2d5b76e2")),
            ),
        ),
    )

    private fun realtimeFor(at: FakeAt, ids: List<String>, body: String) {
        at.answers[AtUrls.realtime(ids)!!] = ok(body)
    }

    private fun trainIds(c: Cache) = c.watch!!.rows.take(RT_PER_WATCH).map { it.tripId }

    @Test fun `a first refresh resolves, derives direction and shows only the serving line`() {
        val at = kingsland()
        val c = Fetcher(at).refresh(Cache(), train, T_15_00)
        val w = c.watch!!

        assertEquals(WatchState.Ok, w.state)
        assertEquals("122-34ecc043", w.stopId)
        assertEquals("133-08da14b5", w.targetStopId)
        assertEquals(8, w.rows.size) // E-W dir 1 only: O-W never reaches Waitemata
        assertEquals(ew1, w.rows[0].tripId)
        assertEquals(gtfsEpoch(day, 15 * 3600 + 13 * 60), w.rows[0].schedEpoch)
        assertEquals("9304-dcb2ed75", w.rows[0].stopId) // the platform
        assertEquals(Badge("E-W", Kind.Train, Rgb(0x97, 0xC9, 0x3D)), w.badge)
        assertEquals(8, c.rail!!.size)
        // rail, two stops, one stoptrips, four pairs probed; realtime was a 500.
        assertEquals(9, at.asked.size)
        assertEquals(T_15_00 + SCHEDULE_PERIOD_S, w.nextSchedAt)
        assertEquals(T_15_00, w.schedOkAt)
    }

    @Test fun `realtime lands on the rows it was asked about`() {
        val at = kingsland()
        val f = Fetcher(at)
        val first = f.refresh(Cache(), train, T_15_00) // realtime 500s: no news yet
        assertTrue(isStale(first.watch!!.lastOk, T_15_00))
        assertEquals(60, first.watch.backoffS)

        realtimeFor(
            at, trainIds(first),
            """{"response":{"entity":[{"trip_update":{"trip":{"trip_id":"$ew1"},"delay":120}}]}}""",
        )
        at.asked.clear()
        val c = f.refresh(first, train, T_15_00 + 30, force = true)
        val w = c.watch!!
        assertEquals(listOf(AtUrls.realtime(trainIds(first))), at.asked) // schedule is not due
        assertTrue(w.rows[0].hasRt)
        assertEquals(120, w.rows[0].delay)
        assertFalse(w.rows[1].hasRt)
        assertEquals(T_15_00 + 30, w.lastOk)
        assertEquals(0, w.backoffS)
    }

    @Test fun `a backoff is waited out unless the refresh is a tap`() {
        val at = kingsland()
        val f = Fetcher(at)
        val first = f.refresh(Cache(), train, T_15_00)
        at.asked.clear()
        f.refresh(first, train, T_15_00 + 30)
        assertEquals(emptyList(), at.asked)
        f.refresh(first, train, T_15_00 + 30, force = true)
        assertEquals(1, at.asked.size)
    }

    @Test fun `the backoff doubles to a five-minute cap`() {
        val at = kingsland()
        val f = Fetcher(at)
        var c = f.refresh(Cache(), train, T_15_00)
        val seen = mutableListOf(c.watch!!.backoffS)
        repeat(4) { c = f.refresh(c, train, c.watch!!.backoffUntil); seen += c.watch!!.backoffS }
        assertEquals(listOf(60, 120, 240, 300, 300), seen)
    }

    @Test fun `a failed probe with no pair serving is inconclusive and changes nothing`() {
        val at = kingsland()
        at.answers.remove(AtUrls.tripStops(ew1)) // the serving pair's probe 500s
        val c = Fetcher(at).refresh(Cache(), train, T_15_00)
        val w = c.watch!!
        assertEquals(WatchState.Starting, w.state) // not "check settings"
        assertEquals(emptyList(), w.rows)
        assertEquals(T_15_00 + 60, w.nextSchedAt)
        assertEquals(0L, w.schedOkAt)
    }

    @Test fun `a toward stop no direction reaches is check settings`() {
        val at = kingsland()
        at.answers[AtUrls.stopByCode("1060")] = ok(fixture("post-crl/stops_1060.json"))
        val c = Fetcher(at).refresh(Cache(), train.copy(towardStopCode = "1060"), T_15_00)
        assertEquals(WatchState.CheckConfig, c.watch!!.state)
        assertEquals(emptyList(), c.watch.rows)
    }

    @Test fun `an empty window is a 404 and an empty widget, not an error`() {
        val at = kingsland()
        at.answers[AtUrls.stoptrips("122-34ecc043", day, 15, 3)!!] = HttpResult(404, "")
        val w = Fetcher(at).refresh(Cache(), train, T_15_00).watch!!
        assertEquals(WatchState.Ok, w.state)
        assertTrue(w.resolved) // and never a reason to re-resolve
        assertEquals(emptyList(), w.rows)
        assertEquals(T_15_00, w.lastOk) // nothing to ask realtime about is news
    }

    @Test fun `in the small hours yesterday's late services are asked for too`() {
        val at = kingsland()
        val t0040 = gtfsEpoch(LocalDate.of(2026, 9, 20), 40 * 60)
        at.answers[AtUrls.stoptrips("122-34ecc043", LocalDate.of(2026, 9, 19), 24, 3)!!] =
            ok(fixture("post-crl/stoptrips_kingsland_after_midnight.json"))
        at.answers[AtUrls.stoptrips("122-34ecc043", LocalDate.of(2026, 9, 20), 1, 3)!!] = HttpResult(404, "")
        at.answers[AtUrls.tripStops("258-880002-85980-2-W201630-da85575f")] = ok(fixture("post-crl/trip_dir1_stops.json"))
        val w = Fetcher(at).refresh(Cache(), train, t0040).watch!!
        assertEquals(WatchState.Ok, w.state)
        assertTrue(w.rows.isNotEmpty())
        assertTrue(w.rows.all { it.schedEpoch >= t0040 - 1800 })
        assertTrue(at.asked.none { "start_hour%5D=0&" in it })
    }

    // --- Kingsland Avenue toward Wynyard Quarter: a pinned bus route ---

    private val bus = WatchConfig("to Wynyard Quarter", "8213", "20", "1060")

    private fun kingslandAvenue() = FakeAt(
        mutableMapOf(
            AtUrls.railRoutes() to ok(fixture("post-crl/routes_rail.json")),
            AtUrls.stopByCode("8213") to ok(fixture("post-crl/stops_8213.json")),
            AtUrls.stopByCode("1060") to ok(fixture("post-crl/stops_1060.json")),
            AtUrls.routesByShortName("20") to ok(fixture("routes_20.json")),
            AtUrls.stoptrips("8213-7e021a72", day, 15, 3)!! to ok(fixture("post-crl/stoptrips_8213.json")),
            AtUrls.tripStops("20-02005-54000-2-89a317b8") to ok(
                stopsJson(Triple("8213-7e021a72", "8213", ""), Triple("1060-00b64ee7", "1060", "")),
            ),
        ),
    )

    @Test fun `a pinned route drops the other routes before any pair is probed`() {
        val at = kingslandAvenue()
        val first = Fetcher(at).refresh(Cache(), bus, T_15_00)
        val w = first.watch!!
        assertEquals(WatchState.Ok, w.state)
        assertEquals(listOf("20-202"), w.routeIds)
        assertEquals(8, w.rows.size) // 22R and 22N never appear
        assertEquals(Badge("20", Kind.Bus, null), w.badge)
        assertEquals(1, at.asked.count { "/trips/" in it }) // one pair, not three
    }

    @Test fun `realtime for trips we never asked about is discarded`() {
        val at = kingslandAvenue()
        val f = Fetcher(at)
        val first = f.refresh(Cache(), bus, T_15_00)
        // The captured feed is from another day: none of its trips are ours.
        realtimeFor(at, first.watch!!.rows.take(RT_PER_WATCH).map { it.tripId }, fixture("realtime_tripupdates_only.json"))
        val w = f.refresh(first, bus, T_15_00 + 30, force = true).watch!!
        assertTrue(w.rows.none { it.hasRt })
        assertEquals(T_15_00 + 30, w.lastOk) // a 200 is news even when it says nothing
    }

    // --- the error surfaces of board spec 8 ---

    @Test fun `an unknown stop code is check settings, retried after a minute`() {
        val at = kingslandAvenue()
        at.answers[AtUrls.stopByCode("8213")] = ok(fixture("post-crl/stops_unknown.json"))
        val f = Fetcher(at)
        val c = f.refresh(Cache(), bus, T_15_00)
        assertEquals(WatchState.CheckConfig, c.watch!!.state)
        at.asked.clear()
        f.refresh(c, bus, T_15_00 + 59)
        assertEquals(emptyList(), at.asked)
        f.refresh(c, bus, T_15_00 + 60)
        assertEquals(AtUrls.stopByCode("8213"), at.asked.first())
    }

    @Test fun `a 400 from the stop lookup means the filter was withdrawn`() {
        val at = kingslandAvenue()
        at.answers[AtUrls.stopByCode("8213")] = HttpResult(400, "")
        assertEquals(WatchState.StopLookupGone, Fetcher(at).refresh(Cache(), bus, T_15_00).watch!!.state)
    }

    @Test fun `a 400 from the route lookup is a config fault instead`() {
        val at = kingslandAvenue()
        at.answers[AtUrls.routesByShortName("20")] = HttpResult(400, "")
        assertEquals(WatchState.CheckConfig, Fetcher(at).refresh(Cache(), bus, T_15_00).watch!!.state)
    }

    @Test fun `a 401 anywhere is check API key, without backing off`() {
        val at = kingslandAvenue()
        at.answers[AtUrls.stopByCode("8213")] = HttpResult(401, "")
        val w = Fetcher(at).refresh(Cache(), bus, T_15_00).watch!!
        assertEquals(WatchState.CheckKey, w.state)
        assertEquals(0, w.backoffS)
        assertEquals(T_15_00 + 60, w.nextResolveAt)
    }

    @Test fun `a changed watch starts over`() {
        val at = kingslandAvenue()
        val f = Fetcher(at)
        val c = f.refresh(Cache(), bus, T_15_00)
        at.asked.clear()
        val other = bus.copy(routeShortName = "")
        val w = f.refresh(c, other, T_15_00 + 5).watch!!
        assertEquals(other.key, w.key)
        assertEquals(AtUrls.stopByCode("8213"), at.asked.first()) // resolved again
    }

    @Test fun `the cache survives the process dying`() {
        val c = Fetcher(kingsland()).refresh(Cache(), train, T_15_00)
        assertEquals(c, Cache.fromJson(c.toJson()))
        assertEquals(Cache(), Cache.fromJson("not json"))
        assertEquals(Cache(), Cache.fromJson(null))
        assertNull(Cache.fromJson("{}").watch)
    }
}
```

- [ ] **Step 2: Run it and watch it fail**

Run: `./gradlew :core:test`
Expected: FAIL to compile — `Unresolved reference 'HttpResult'`.

- [ ] **Step 3: Write `Fetcher.kt`**

```kotlin
package io.github.msmgreen.nextdeparture.core

import kotlinx.serialization.Serializable
import kotlinx.serialization.SerializationException
import kotlinx.serialization.json.Json

// The board's fetch task (src/fetcher.cpp) for one watch, as a function from
// the cache and the clock to the next cache. The network is behind AtHttp, so
// a test can answer with the captured fixtures; the app answers with OkHttp.
// The process is killed between almost every run, so everything that must
// outlive one fetch is in the Cache, which the app persists.

// The HTTP status, or TRANSPORT_ERROR when nothing came back at all.
data class HttpResult(val status: Int, val body: String)

const val TRANSPORT_ERROR = -1

fun interface AtHttp {
    fun get(url: String): HttpResult
}

private const val BACKOFF_CAP_S = 300
private const val RESOLVE_RETRY_S = 60L // a 400/401 must not become a request storm
private const val SCHEDULE_RETRY_S = 60L // after an inconclusive refresh: soon

@Serializable
data class WatchCache(
    val key: String, // WatchConfig.key this cache was built for
    val state: WatchState = WatchState.Starting,
    val resolved: Boolean = false,
    val stopId: String = "",
    val targetStopId: String = "",
    val routeIds: List<String> = emptyList(), // empty: any route
    val nextResolveAt: Long = 0,
    val badge: Badge? = null,
    val rows: List<LiveRow> = emptyList(),
    val schedDate: String = "", // local date of the last refresh, for rollover
    val nextSchedAt: Long = 0,
    val schedOkAt: Long = 0, // last conclusive schedule refresh
    val lastOk: Long = 0, // see lastOkAfter
    val backoffS: Int = 0,
    val backoffUntil: Long = 0,
)

@Serializable
data class Cache(
    val rail: List<RouteInfo>? = null, // the rail route table, fetched once
    val watch: WatchCache? = null,
) {
    fun toJson(): String = json.encodeToString(serializer(), this)

    companion object {
        private val json = Json { ignoreUnknownKeys = true }

        // A cache that will not read back is a cache to rebuild, never an error.
        fun fromJson(text: String?): Cache = try {
            if (text.isNullOrEmpty()) Cache() else json.decodeFromString(serializer(), text)
        } catch (e: SerializationException) {
            Cache()
        } catch (e: IllegalArgumentException) {
            Cache()
        }
    }
}

class Fetcher(private val http: AtHttp, private val log: (String) -> Unit = {}) {

    // Folded over every request in one refresh, to decide backoff once.
    private class Pass {
        var backoff = false
        var news = false
    }

    // One refresh. force (a tap) skips the backoff and resolve waits; it never
    // forces a schedule refresh that is not due.
    fun refresh(cache: Cache, watch: WatchConfig, now: Long, force: Boolean = false): Cache {
        var w = cache.watch?.takeIf { it.key == watch.key } ?: WatchCache(watch.key)
        if (!force && now < w.backoffUntil) return cache.copy(watch = w)

        val pass = Pass()
        val rail = cache.rail ?: loadRail(pass)
        if (!w.resolved && (force || now >= w.nextResolveAt)) w = resolve(w, watch, rail.orEmpty(), now, pass)
        if (w.resolved) {
            val today = local(now).toLocalDate().toString()
            if (now >= w.nextSchedAt || w.schedDate != today) {
                w = refreshSchedule(w, watch, rail.orEmpty(), now, pass)
            }
        }
        if (w.state == WatchState.Ok) w = refreshRealtime(w, now, pass)

        w = w.copy(lastOk = lastOkAfter(w.lastOk, now, pass.news, w.schedOkAt))
        // Spec 8: 429, 5xx and anything that never reached the server double
        // the wait to a 5-minute cap, with the last good data left in place.
        w = if (pass.backoff) {
            val prev = if (w.backoffS > 0) w.backoffS else RT_FAST_S
            val next = if (prev > BACKOFF_CAP_S / 2) BACKOFF_CAP_S else prev * 2
            w.copy(backoffS = next, backoffUntil = now + next)
        } else {
            w.copy(backoffS = 0, backoffUntil = 0)
        }
        return Cache(rail, w)
    }

    private fun get(url: String, pass: Pass): HttpResult {
        val r = http.get(url)
        if (r.status == TRANSPORT_ERROR || r.status == 429 || r.status >= 500) pass.backoff = true
        log("GET ${url.substringAfter("api.at.govt.nz")} -> ${r.status}")
        return r
    }

    // The rail route table decides Kind, and supplies the badge and colour for a
    // watch that pins no route - which is how a rail watch survives a rename.
    private fun loadRail(pass: Pass): List<RouteInfo>? {
        val r = get(AtUrls.railRoutes(), pass)
        if (r.status != 200) return null
        val routes = AtParse.routes(r.body)
        if (routes == null) pass.backoff = true
        return routes
    }

    private sealed interface Lookup {
        data class Found(val stop: StopInfo) : Lookup
        data object Unknown : Lookup // 200 with an empty data array
        data class Failed(val status: Int) : Lookup
    }

    private fun lookupStop(code: String, pass: Pass): Lookup {
        val r = get(AtUrls.stopByCode(code), pass)
        if (r.status != 200) return Lookup.Failed(r.status)
        val stops = AtParse.stops(r.body) ?: return Lookup.Failed(TRANSPORT_ERROR).also { pass.backoff = true }
        return stops.firstOrNull()?.let { Lookup.Found(it) } ?: Lookup.Unknown
    }

    private fun checkKey(w: WatchCache, now: Long) = w.copy(
        state = WatchState.CheckKey,
        nextResolveAt = now + RESOLVE_RETRY_S,
        nextSchedAt = maxOf(w.nextSchedAt, now + RESOLVE_RETRY_S),
    )

    // The stop, the stop it points toward, and the route ids behind a pinned
    // short name. The status mapping is the whole of board spec 8 for this step.
    private fun resolve(w: WatchCache, watch: WatchConfig, rail: List<RouteInfo>, now: Long, pass: Pass): WatchCache {
        val failed = { status: Int ->
            when (status) {
                // The undocumented filter[stop_code] has been withdrawn (spec 8).
                400 -> w.copy(state = WatchState.StopLookupGone, nextResolveAt = now + RESOLVE_RETRY_S)
                401 -> checkKey(w, now)
                else -> w // transient: try again next time, under backoff
            }
        }
        val unknown = w.copy(state = WatchState.CheckConfig, nextResolveAt = now + RESOLVE_RETRY_S)

        val stop = when (val l = lookupStop(watch.stopCode, pass)) {
            is Lookup.Found -> l.stop
            Lookup.Unknown -> return unknown
            is Lookup.Failed -> return failed(l.status)
        }
        val target = when (val l = lookupStop(watch.towardStopCode, pass)) {
            is Lookup.Found -> l.stop
            Lookup.Unknown -> return unknown
            is Lookup.Failed -> return failed(l.status)
        }

        var routeIds = emptyList<String>()
        var badge = w.badge
        if (watch.routeShortName.isNotEmpty()) {
            val r = get(AtUrls.routesByShortName(watch.routeShortName), pass)
            // A 400 here is a short name AT will not accept - a config fault,
            // not the stop-code filter being withdrawn.
            if (r.status == 400) return unknown
            if (r.status != 200) return failed(r.status)
            val routes = AtParse.routes(r.body) ?: return w.also { pass.backoff = true }
            if (routes.isEmpty()) return unknown
            // Every returned route_id: a short name spans both sides of a rename.
            routeIds = routes.map { it.routeId }
            badge = badgeFor(routes[0].routeId, rail).let {
                if (routes[0].routeType == 2) it.copy(kind = Kind.Train) else it
            }
        }
        log("resolve ${watch.stopCode} -> ${stop.stopId}  toward ${watch.towardStopCode} -> ${target.stopId}")
        return w.copy(
            state = WatchState.Starting, // rows arrive with the schedule
            resolved = true,
            stopId = stop.stopId,
            targetStopId = target.stopId,
            routeIds = routeIds,
            badge = badge,
        )
    }

    // One schedule refresh. When nothing conclusive came back, the cached rows
    // and state stay exactly as they were and it is retried in a minute.
    private fun refreshSchedule(w0: WatchCache, watch: WatchConfig, rail: List<RouteInfo>, now: Long, pass: Pass): WatchCache {
        val lt = local(now)
        // Stamped before the requests, so a failed refresh retries on
        // nextSchedAt rather than looking like a rollover on every run.
        val w = w0.copy(schedDate = lt.toLocalDate().toString())
        val inconclusive = w.copy(nextSchedAt = now + SCHEDULE_RETRY_S)

        val rows = mutableListOf<StopTripRow>()
        for (win in scheduleWindows(lt)) {
            val url = AtUrls.stoptrips(w.stopId, win.date, win.startHour, win.hourRange) ?: continue
            val r = get(url, pass)
            // 404 is "no services in this window" - the same status as an
            // unknown stop, and never a reason to re-resolve one. A real answer.
            if (r.status == 404) continue
            if (r.status == 401) return checkKey(w, now)
            if (r.status != 200) return inconclusive
            rows += AtParse.stoptrips(r.body) ?: return inconclusive.also { pass.backoff = true }
        }

        // A pinned watch never shows another route's rows, so drop them before
        // the pairs are collected, or other routes could fill the pair cap.
        val wanted = if (w.routeIds.isEmpty()) rows else rows.filter { it.routeId in w.routeIds }

        // Direction is derived every refresh, per (route_id, direction_id) pair,
        // never per trip (board spec 3a; the Kingsland E-W / O-W lesson).
        var failed = 0
        val pairs = collectRouteDirs(wanted).map { p ->
            val r = get(AtUrls.tripStops(p.tripId), pass)
            if (r.status == 401) return checkKey(w, now)
            val stops = if (r.status == 200) AtParse.tripStops(r.body) else null
            if (stops == null) {
                failed++ // serves stays false, and that is not proof of anything
                p
            } else {
                p.copy(serves = tripServes(stops, p.stopId, watch.towardStopCode, w.targetStopId))
            }
        }
        log("dirs ${watch.stopCode}: " + pairs.joinToString("  ") { "${it.routeId}/${it.directionId} ${if (it.serves) "yes" else "no"}" })

        // Inconclusive is not evidence: a failed pair may be the very one that
        // reaches the target. Conclusive means every pair spoke, or one serves.
        if (failed > 0 && pairs.none { it.serves }) return inconclusive

        val verdict = verdictForPairs(pairs)
        val next = if (verdict != WatchState.Ok) {
            // A service runs both ways past this stop and neither way gets you
            // there. A watch that cannot prove its direction shows no time.
            w.copy(state = verdict, rows = emptyList())
        } else {
            val fresh = carryRealtime(w.rows, selectServingRows(wanted, pairs, w.routeIds, now))
            val first = fresh.firstOrNull()?.let { row -> wanted.first { it.tripId == row.tripId } }
            w.copy(
                state = WatchState.Ok, // no rows is an empty window, not an error
                rows = fresh,
                badge = first?.let { badgeFor(it.routeId, rail) } ?: w.badge,
            )
        }
        return next.copy(nextSchedAt = now + SCHEDULE_PERIOD_S, schedOkAt = now)
    }

    private fun refreshRealtime(w: WatchCache, now: Long, pass: Pass): WatchCache {
        val ids = realtimeIds(w.rows, now)
        if (ids.isEmpty()) {
            pass.news = true // nothing upcoming to ask about; not a failure
            return w
        }
        val r = get(AtUrls.realtime(ids)!!, pass)
        if (r.status == 401) return checkKey(w, now)
        if (r.status != 200) return w
        val ents = AtParse.realtime(r.body) ?: return w.also { pass.backoff = true }
        pass.news = true
        // Realtime only reports trips already under way, so fewer entities than
        // ids is normal. The same id list goes to applyRealtime, so a trip we
        // asked about and heard nothing for loses its realtime.
        return w.copy(rows = applyRealtime(w.rows, ents, ids))
    }
}
```

- [ ] **Step 4: Run it**

Run: `./gradlew :core:test`
Expected: PASS, 89 tests.

- [ ] **Step 5: Commit**

```bash
git add android/core
git commit -m "Port the fetch loop for one watch, with its state in a cache

The board keeps its state in RAM for months; a phone's process dies
between runs, so everything a refresh needs next time is in a
serializable Cache. AT is behind an interface, and the tests play it
with the board's fixtures."
```

---

### Task 7: The widget model

Every word the widget shows, decided where it can be tested.

**Files:**
- Create: `$CORE/WidgetModel.kt`
- Test: `$CORE_TEST/WidgetModelTest.kt`

**Interfaces:**
- Consumes: `Place` (Task 5), `WatchCache` (Task 6), `chooseDeparture`, `isStale`, `staleAt` (Task 4), `formatClock` (Task 1)
- Produces: `WidgetView`, `widgetView(place, cache, now, h24)` — used by Task 9

- [ ] **Step 1: Write the failing test**

```kotlin
package io.github.msmgreen.nextdeparture.core

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class WidgetModelTest {
    private val now = T_15_00
    private val home = Place("Home", WatchConfig("to Waitemata", "122", "", "133"), walkMin = 5)
    private val badge = Badge("E-W", Kind.Train, Rgb(0x97, 0xC9, 0x3D))

    private fun cache(vararg rows: LiveRow, lastOk: Long = now, state: WatchState = WatchState.Ok) =
        WatchCache(home.watch.key, state = state, badge = badge, rows = rows.toList(), lastOk = lastOk)

    private val late = LiveRow("t", "", now + 900, hasRt = true, delay = 120) // 15:15, running 2 late

    @Test fun `a fresh live departure counts down to leave-by`() {
        val v = widgetView(home, cache(late), now, h24 = true)
        assertEquals("Home", v.place)
        assertEquals("E-W to Waitemata", v.title)
        assertEquals(now + 900 + 120 - 300, v.countdownTo)
        assertEquals("15:17 · 2 min late · live", v.detail)
        assertNull(v.message)
        assertNull(v.asOf)
        assertEquals(false, v.stale)
        assertEquals(Rgb(0x97, 0xC9, 0x3D), v.badgeColor)
        assertEquals(now + 121, v.nextRenderAt) // redrawn the moment it goes stale
    }

    @Test fun `stale keeps the last good data but never says live`() {
        val v = widgetView(home, cache(late), now + 200, h24 = true)
        assertEquals("15:17 · 2 min late", v.detail)
        assertEquals("as of 15:00", v.asOf)
        assertEquals(true, v.stale)
        assertEquals(now + 900 + 120 - 300 + 1, v.nextRenderAt) // just after leave-by
    }

    @Test fun `no realtime is marked sched`() {
        val v = widgetView(home, cache(LiveRow("t", "", now + 900)), now, h24 = true)
        assertEquals("15:15 · sched", v.detail)
    }

    @Test fun `early, on time and the 12-hour clock`() {
        val early = LiveRow("t", "", now + 900, hasRt = true, delay = -180)
        assertEquals("3:12pm · 3 min early · live", widgetView(home, cache(early), now, h24 = false).detail)
        val onTime = LiveRow("t", "", now + 900, hasRt = true, delay = 20)
        assertEquals("15:15 · on time · live", widgetView(home, cache(onTime), now, h24 = true).detail)
    }

    @Test fun `a cancellation you would have caught is noted`() {
        val gone = LiveRow("gone", "", now + 600, cancelled = true)
        val v = widgetView(home, cache(gone, late), now, h24 = true)
        assertEquals("15:10 cancelled", v.note)
        assertEquals("15:17 · 2 min late · live", v.detail)
    }

    @Test fun `nothing catchable says so, or asks for a refresh once stale`() {
        val left = LiveRow("t", "", now + 60) // closer than the 5-minute walk
        assertEquals("None in the next 3 h", widgetView(home, cache(left), now, h24 = true).message)
        assertEquals("Tap to refresh", widgetView(home, cache(left, lastOk = now - 600), now, h24 = true).message)
    }

    @Test fun `a state that cannot show times shows its message`() {
        val v = widgetView(home, cache(late, state = WatchState.CheckKey), now, h24 = true)
        assertEquals("Check API key", v.message)
        assertNull(v.countdownTo)
        assertEquals("Starting", widgetView(home, null, now, h24 = true).message)
    }

    @Test fun `the title falls back to the route short name, then the label alone`() {
        val noBadge = cache(late).copy(badge = null)
        assertEquals("to Waitemata", widgetView(home, noBadge, now, h24 = true).title)
        val pinned = home.copy(watch = home.watch.copy(routeShortName = "20", label = "to Wynyard"))
        assertEquals("20 to Wynyard", widgetView(pinned, noBadge, now, h24 = true).title)
    }
}
```

- [ ] **Step 2: Run it and watch it fail**

Run: `./gradlew :core:test`
Expected: FAIL to compile — `Unresolved reference 'widgetView'`.

- [ ] **Step 3: Write `WidgetModel.kt`**

```kotlin
package io.github.msmgreen.nextdeparture.core

import kotlin.math.abs

// What the widget shows, decided here so that every word of it is tested on a
// laptop. The app only maps a WidgetView onto RemoteViews (widget spec 6).

data class WidgetView(
    val place: String, // "Home"
    val title: String, // "20 to Wynyard Quarter"
    val countdownTo: Long?, // leave-by epoch for the Chronometer; null with a message
    val message: String?, // shown instead of a countdown
    val detail: String, // "7:42 · 2 min late · live"
    val note: String?, // "7:36 cancelled"
    val asOf: String?, // "as of 7:31" once stale; null while fresh
    val stale: Boolean, // dim everything
    val badgeColor: Rgb?,
    val nextRenderAt: Long?, // when this view stops being true without a fetch
)

private fun delayText(delay: Int): String {
    val m = (abs(delay) + 30) / 60
    return when {
        m == 0 -> "on time"
        delay > 0 -> "$m min late"
        else -> "$m min early"
    }
}

fun widgetView(place: Place, w: WatchCache?, now: Long, h24: Boolean): WidgetView {
    val title = listOf(w?.badge?.text ?: place.watch.routeShortName, place.watch.label)
        .filter { it.isNotEmpty() }
        .joinToString(" ")
    val lastOk = w?.lastOk ?: 0L
    val stale = isStale(lastOk, now)
    val asOf = if (stale && lastOk > 0) "as of ${formatClock(lastOk, h24)}" else null
    // A fresh view must be redrawn the moment it goes stale, so "live" never
    // outlasts its evidence.
    val staleRender = if (stale) null else staleAt(lastOk)

    fun message(text: String, note: String? = null) = WidgetView(
        place.name, title, null, text, "", note, asOf, stale, w?.badge?.color, staleRender,
    )

    if (w == null) return message(stateMessage(WatchState.Starting))
    if (w.state != WatchState.Ok) return message(stateMessage(w.state))

    val choice = chooseDeparture(w.rows, now, place.walkMin * 60)
    val note = choice.cancelled?.let { "${formatClock(it.schedEpoch, h24)} cancelled" }
    val d = choice.departure ?: return message(if (stale) "Tap to refresh" else "None in the next 3 h", note)

    val marker = when {
        !d.row.hasRt -> "sched"
        !stale -> "live"
        else -> null // the old delay is still the best guess, but never "live"
    }
    val detail = listOfNotNull(
        formatClock(d.row.predicted, h24),
        if (d.row.hasRt) delayText(d.row.delay) else null,
        marker,
    ).joinToString(" · ")
    // Redraw just after leave-by, when the next catchable departure takes over.
    val next = listOfNotNull(d.leaveBy + 1, staleRender).min()
    return WidgetView(place.name, title, d.leaveBy, null, detail, note, asOf, stale, w.badge?.color, next)
}
```

`nextRenderAt` is the earliest of two moments: just after leave-by, when the
next catchable departure takes over, and the first stale second, so `live`
never outlasts its evidence. Task 9 arms an alarm for it.

- [ ] **Step 4: Run the whole suite**

Run: `./gradlew :core:test`
Expected: PASS, 97 tests.

- [ ] **Step 5: Commit**

```bash
git add android/core
git commit -m "Decide every word the widget shows in core, where it is tested"
```

---
### Task 8: The app module — **PHONE**

Not compiled when this plan was written (see Scope). Android Studio generates
the module, so the Android Gradle Plugin, Compose and androidx versions are
whatever its template picks on the day; this task changes only what it names.

**Files:**
- Create (by Android Studio): `android/app/`, with its `build.gradle.kts`, manifest, theme and launcher icon
- Modify: `android/settings.gradle.kts`, `android/gradle/libs.versions.toml`, `android/build.gradle.kts` (the wizard does this), `.github/workflows/ci.yml`
- Create (not committed): `android/local.properties` entries, a keystore outside the repository

**Interfaces:**
- Consumes: `:core`
- Produces: an installable app whose launcher activity is `SettingsActivity` (still the template's screen), and `BuildConfig.AT_API_KEY` — used by Tasks 9 and 10

- [ ] **Step 1: Generate the module**

Open `android/` in Android Studio. **File → New → New Module… → Phone & Tablet
→ Empty Activity**, with:

| Field | Value |
|---|---|
| Module name | `app` |
| Package name | `io.github.msmgreen.nextdeparture` |
| Minimum SDK | API 34 |
| Build configuration language | Kotlin DSL |

Then rename the generated `MainActivity` to **`SettingsActivity`**
(Refactor → Rename, so the manifest follows). Task 9's widget opens it by that
name; Task 10 replaces its contents.

- [ ] **Step 2: Let the build reach Google's repository**

The wizard may already have done this. `android/settings.gradle.kts` becomes:

```kotlin
pluginManagement {
    repositories {
        google()
        gradlePluginPortal()
        mavenCentral()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "next-departure"
include(":core", ":app")
```

- [ ] **Step 3: Add the libraries to the catalog**

To `android/gradle/libs.versions.toml`, beside what the wizard added. OkHttp
and coroutines are on Maven Central and were checked when this plan was
written; take WorkManager and DataStore at the current stable from their
release pages (developer.android.com/jetpack/androidx/releases/work and
…/datastore):

```toml
[versions]
okhttp = "5.4.0"
coroutines = "1.11.0"
work = "…"       # current stable
datastore = "…"  # current stable

[libraries]
okhttp = { module = "com.squareup.okhttp3:okhttp", version.ref = "okhttp" }
kotlinx-coroutines-android = { module = "org.jetbrains.kotlinx:kotlinx-coroutines-android", version.ref = "coroutines" }
androidx-work-runtime-ktx = { module = "androidx.work:work-runtime-ktx", version.ref = "work" }
androidx-datastore-preferences = { module = "androidx.datastore:datastore-preferences", version.ref = "datastore" }
```

- [ ] **Step 4: Edit `android/app/build.gradle.kts`**

Keep the template's `plugins { }` block and its Compose dependencies exactly as
generated. Add the `local.properties` reader above `android { }`, and inside it
set or add the lines shown:

```kotlin
import java.util.Properties

// android/local.properties is gitignored. It holds the AT key - as
// src/secrets.h does for the board - and where the signing keystore lives.
// Both phones need builds signed with the same key, or an update will not
// install over the last one (widget spec 12).
val local = Properties().apply {
    rootProject.file("local.properties").takeIf { it.exists() }?.inputStream()?.use { load(it) }
}

android {
    namespace = "io.github.msmgreen.nextdeparture"

    defaultConfig {
        applicationId = "io.github.msmgreen.nextdeparture"
        minSdk = 34
        targetSdk = 36
        // Empty when local.properties has no key; the setup check says so.
        buildConfigField("String", "AT_API_KEY", "\"${local.getProperty("AT_API_KEY", "")}\"")
    }

    signingConfigs {
        local.getProperty("SIGNING_STORE_FILE")?.let { path ->
            create("phones") {
                storeFile = file(path)
                storePassword = local.getProperty("SIGNING_STORE_PASSWORD")
                keyAlias = local.getProperty("SIGNING_KEY_ALIAS")
                keyPassword = local.getProperty("SIGNING_KEY_PASSWORD")
            }
        }
    }

    buildTypes {
        // Debug builds are what go on the phones: debuggable, logging, and
        // signed with the shared key whenever local.properties names one.
        // CI and a fresh clone fall back to the machine's debug key.
        debug {
            signingConfigs.findByName("phones")?.let { signingConfig = it }
        }
    }

    // core is compiled for Java 17; match it.
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    buildFeatures {
        buildConfig = true
        compose = true
    }
}

dependencies {
    implementation(project(":core"))
    implementation(libs.okhttp)
    implementation(libs.kotlinx.coroutines.android)
    implementation(libs.androidx.work.runtime.ktx)
    implementation(libs.androidx.datastore.preferences)
    // ...and the template's dependencies, unchanged.
}
```

If the template has a `kotlin { compilerOptions { jvmTarget … } }` block, set
it to `JvmTarget.JVM_17` to match `compileOptions`.

- [ ] **Step 5: Make the signing key and record the secrets**

Once, on the machine that builds for the phones, **outside the repository**:

```bash
keytool -genkeypair -v -keystore ~/next-departure.jks -alias phones \
  -keyalg RSA -keysize 4096 -validity 36500
```

Back the `.jks` file and its passwords up somewhere that is not this
repository. Then add to `android/local.properties` (Android Studio created the
file with `sdk.dir`):

```properties
AT_API_KEY=<the key from src/secrets.h>
SIGNING_STORE_FILE=/absolute/path/to/next-departure.jks
SIGNING_STORE_PASSWORD=<store password>
SIGNING_KEY_ALIAS=phones
SIGNING_KEY_PASSWORD=<key password>
```

`SIGNING_STORE_FILE` must be absolute: Gradle does not expand `~`.

- [ ] **Step 6: Build it**

Run (from `android/`): `./gradlew :core:test :app:assembleDebug`
Expected: core PASS, 97 tests; `BUILD SUCCESSFUL`, with
`app/build/outputs/apk/debug/app-debug.apk`.

- [ ] **Step 7: Install it on one phone — PHONE**

Enable USB debugging (Settings → About phone → Software information → tap
Build number seven times; then Developer options → USB debugging), connect, and
run `./gradlew :app:installDebug`. Expected: an app called by the template's
name opens to the template's screen.

- [ ] **Step 8: Build the app in CI too**

The CI job's `run` line becomes:

```yaml
      - run: ./gradlew :core:test :app:assembleDebug
        working-directory: android
```

GitHub's Ubuntu runners ship an Android SDK and set `ANDROID_HOME`, which
configuring `:app` needs even for `:core:test`. If CI stops at configuration
with "SDK location not found", add `- uses: android-actions/setup-android@v3`
before the `run` step.

- [ ] **Step 9: Commit**

```bash
git add android/ .github/workflows/ci.yml
git status --short android/local.properties  # must print nothing: it is ignored
git commit -m "Add the widget's app module, with the AT key from local.properties

Debug builds are what go on the phones, signed with a shared key named
in local.properties so either phone takes an update built anywhere."
```

---

### Task 9: The widget — **PHONE**

Everything between core and the home screen. Not compiled when this plan was
written (see Scope).

These files are one task because they call each other: a tap enqueues
`RefreshWorker`, which runs `Refresher`, which saves through `Store` and then
asks `DepartureWidget` to redraw. Until Task 10 adds the settings form there is
no config, so the widget shows **Tap to set up**; the phone check here is
that much, plus the reboot path.

**Files:**
- Create: `$APP/Store.kt`, `$APP/OkAt.kt`, `$APP/Refresher.kt`, `$APP/RefreshWorker.kt`, `$APP/BootReceiver.kt`, `$APP/DepartureWidget.kt`
- Create: `$RES/layout/widget_departure.xml`, `$RES/xml/departure_widget_info.xml`, `$RES/drawable/widget_background.xml`, `$RES/drawable/badge.xml`, `$RES/values/colors.xml`, `$RES/values-night/colors.xml`, `$RES/values/strings_widget.xml`
- Modify: `android/app/src/main/AndroidManifest.xml`

**Interfaces:**
- Consumes: `Cache`, `Fetcher`, `AtHttp` (Task 6), `widgetView`, `WidgetView` (Task 7), `ConfigCodec` (Task 5), `BuildConfig.AT_API_KEY` (Task 8)
- Produces: `Store`, `OkAt`, `Refresher.refresh(context, force)`, `RefreshWorker.ensurePeriodic` / `RefreshWorker.now`, `DepartureWidget.renderAll` — used by Task 10

- [ ] **Step 1: The store and the network**

`$APP/Store.kt`:

```kotlin
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
```

`$APP/OkAt.kt`:

```kotlin
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
```

- [ ] **Step 2: The one way in to a fetch**

`$APP/Refresher.kt`:

```kotlin
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
            val place = store.config()?.places?.firstOrNull() ?: return@withLock
            val cache = store.cache()
            val now = System.currentTimeMillis() / 1000
            val last = cache.watch
            val recent = last != null && last.key == place.watch.key && now - last.lastOk < DEBOUNCE_S
            if (!force && recent) return@withLock
            val next = withContext(Dispatchers.IO) { fetcher.refresh(cache, place.watch, now, force) }
            store.saveCache(next)
        }
        DepartureWidget.renderAll(context)
    }
}
```

`$APP/RefreshWorker.kt`. `ExistingPeriodicWorkPolicy.UPDATE` keeps the
schedule's timing when the app re-enqueues it, which it does from every entry
point so a cleared WorkManager database heals itself:

```kotlin
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
```

`$APP/BootReceiver.kt`:

```kotlin
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
```

- [ ] **Step 3: The widget**

`$APP/DepartureWidget.kt`:

```kotlin
package io.github.msmgreen.nextdeparture

import android.app.AlarmManager
import android.app.PendingIntent
import android.appwidget.AppWidgetManager
import android.appwidget.AppWidgetProvider
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.os.SystemClock
import android.text.format.DateFormat
import android.util.SizeF
import android.view.View
import android.widget.RemoteViews
import io.github.msmgreen.nextdeparture.core.WidgetView
import io.github.msmgreen.nextdeparture.core.widgetView
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch

// Maps a WidgetView onto RemoteViews. Every decision about what to say was made
// in core's widgetView and is tested there; nothing here chooses words.
class DepartureWidget : AppWidgetProvider() {

    override fun onReceive(context: Context, intent: Intent) {
        when (intent.action) {
            ACTION_REFRESH -> RefreshWorker.now(context)
            ACTION_RENDER -> renderAsync(context)
            else -> super.onReceive(context, intent)
        }
    }

    override fun onUpdate(context: Context, manager: AppWidgetManager, ids: IntArray) {
        RefreshWorker.ensurePeriodic(context)
        renderAsync(context)
    }

    private fun renderAsync(context: Context) {
        val pending = goAsync()
        CoroutineScope(Dispatchers.Default).launch {
            try {
                renderAll(context)
            } finally {
                pending.finish()
            }
        }
    }

    companion object {
        const val ACTION_REFRESH = "io.github.msmgreen.nextdeparture.REFRESH"
        const val ACTION_RENDER = "io.github.msmgreen.nextdeparture.RENDER"

        // Draws every instance from the stored cache, without the network, and
        // arms the next local re-render (widget spec 6).
        suspend fun renderAll(context: Context) {
            val manager = AppWidgetManager.getInstance(context)
            val ids = manager.getAppWidgetIds(ComponentName(context, DepartureWidget::class.java))
            if (ids.isEmpty()) return
            val store = Store(context)
            val place = store.config()?.places?.firstOrNull()
            if (place == null) {
                manager.updateAppWidget(ids, unconfigured(context))
                return
            }
            val now = System.currentTimeMillis() / 1000
            val watch = store.cache().watch?.takeIf { it.key == place.watch.key }
            val view = widgetView(place, watch, now, DateFormat.is24HourFormat(context))
            scheduleRender(context, view.nextRenderAt)
            // 2x1 keeps the title and the countdown; 4x1 has room for the rest.
            val sized = RemoteViews(
                mapOf(
                    SizeF(130f, 40f) to remoteViews(context, view, compact = true),
                    SizeF(250f, 40f) to remoteViews(context, view, compact = false),
                ),
            )
            manager.updateAppWidget(ids, sized)
        }

        private fun remoteViews(context: Context, v: WidgetView, compact: Boolean): RemoteViews {
            val r = RemoteViews(context.packageName, R.layout.widget_departure)
            val text = context.getColor(if (v.stale) R.color.widget_text_dim else R.color.widget_text)
            val quiet = context.getColor(R.color.widget_text_dim)

            r.setTextViewText(R.id.place, v.place)
            r.setTextViewText(R.id.title, v.title)
            r.setTextViewText(R.id.as_of, v.asOf ?: "")
            r.setTextViewText(R.id.detail, v.detail)
            r.setTextViewText(R.id.note, v.note ?: "")
            for (id in listOf(R.id.place, R.id.title, R.id.leave_label, R.id.countdown, R.id.message, R.id.detail)) {
                r.setTextColor(id, text)
            }
            r.setTextColor(R.id.as_of, quiet)
            r.setTextColor(R.id.note, quiet)
            r.setInt(R.id.badge, "setColorFilter", v.badgeColor?.argb ?: context.getColor(R.color.widget_badge))

            val to = v.countdownTo // a property of another module: no smart cast
            if (to != null) {
                // The base is in the elapsedRealtime timebase, so the countdown
                // ticks with no process running (widget spec 6).
                val base = SystemClock.elapsedRealtime() + (to * 1000 - System.currentTimeMillis())
                r.setChronometer(R.id.countdown, base, null, true)
                r.setChronometerCountDown(R.id.countdown, true)
                show(r, R.id.leave_label, R.id.countdown)
                hide(r, R.id.message)
            } else {
                r.setChronometer(R.id.countdown, SystemClock.elapsedRealtime(), null, false)
                r.setTextViewText(R.id.message, v.message ?: "")
                show(r, R.id.message)
                hide(r, R.id.leave_label, R.id.countdown)
            }
            if (compact) hide(r, R.id.as_of, R.id.detail, R.id.note)
            else if (v.note == null) hide(r, R.id.note) else show(r, R.id.note)

            r.setOnClickPendingIntent(R.id.root, broadcast(context, ACTION_REFRESH))
            return r
        }

        private fun unconfigured(context: Context): RemoteViews {
            val r = RemoteViews(context.packageName, R.layout.widget_departure)
            r.setTextViewText(R.id.place, context.getString(R.string.app_name))
            r.setTextViewText(R.id.message, context.getString(R.string.tap_to_set_up))
            show(r, R.id.message)
            hide(r, R.id.leave_label, R.id.countdown, R.id.title, R.id.as_of, R.id.detail, R.id.note)
            val settings = Intent(context, SettingsActivity::class.java)
            r.setOnClickPendingIntent(R.id.root, PendingIntent.getActivity(context, 0, settings, PendingIntent.FLAG_IMMUTABLE))
            return r
        }

        private fun show(r: RemoteViews, vararg ids: Int) = ids.forEach { r.setViewVisibility(it, View.VISIBLE) }

        private fun hide(r: RemoteViews, vararg ids: Int) = ids.forEach { r.setViewVisibility(it, View.GONE) }

        private fun broadcast(context: Context, action: String): PendingIntent = PendingIntent.getBroadcast(
            context,
            action.hashCode(),
            Intent(context, DepartureWidget::class.java).setAction(action),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )

        // Exact when allowed. An inexact alarm may arrive as late as three
        // quarters of its delay, which would leave the countdown running past
        // zero for minutes. Neither kind fires while the phone dozes; both fire
        // when it wakes, which is the only time anyone looks.
        private fun scheduleRender(context: Context, at: Long?) {
            val alarms = context.getSystemService(AlarmManager::class.java)
            val pi = broadcast(context, ACTION_RENDER)
            if (at == null) {
                alarms.cancel(pi)
            } else if (alarms.canScheduleExactAlarms()) {
                alarms.setExact(AlarmManager.RTC, at * 1000, pi)
            } else {
                alarms.set(AlarmManager.RTC, at * 1000, pi)
            }
        }
    }
}
```

`$RES/layout/widget_departure.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<!-- One departure (widget spec 6). Text and visibility are set from a
     WidgetView in DepartureWidget.kt; nothing here is shown as typed. -->
<LinearLayout xmlns:android="http://schemas.android.com/apk/res/android"
    android:id="@+id/root"
    android:layout_width="match_parent"
    android:layout_height="match_parent"
    android:background="@drawable/widget_background"
    android:gravity="center_vertical"
    android:orientation="horizontal"
    android:padding="12dp">

    <!-- Tinted with the route colour: rail lines carry one, buses do not. -->
    <ImageView
        android:id="@+id/badge"
        android:layout_width="6dp"
        android:layout_height="match_parent"
        android:layout_marginEnd="10dp"
        android:importantForAccessibility="no"
        android:src="@drawable/badge" />

    <LinearLayout
        android:layout_width="0dp"
        android:layout_height="wrap_content"
        android:layout_weight="1"
        android:orientation="vertical">

        <LinearLayout
            android:layout_width="match_parent"
            android:layout_height="wrap_content"
            android:orientation="horizontal">

            <TextView
                android:id="@+id/place"
                android:layout_width="wrap_content"
                android:layout_height="wrap_content"
                android:maxLines="1"
                android:textSize="13sp"
                android:textStyle="bold" />

            <TextView
                android:id="@+id/title"
                android:layout_width="0dp"
                android:layout_height="wrap_content"
                android:layout_marginStart="6dp"
                android:layout_weight="1"
                android:ellipsize="end"
                android:maxLines="1"
                android:textSize="13sp" />

            <TextView
                android:id="@+id/as_of"
                android:layout_width="wrap_content"
                android:layout_height="wrap_content"
                android:layout_marginStart="6dp"
                android:maxLines="1"
                android:textSize="12sp" />
        </LinearLayout>

        <LinearLayout
            android:layout_width="match_parent"
            android:layout_height="wrap_content"
            android:gravity="center_vertical"
            android:orientation="horizontal">

            <TextView
                android:id="@+id/leave_label"
                android:layout_width="wrap_content"
                android:layout_height="wrap_content"
                android:text="@string/leave_in"
                android:textSize="14sp" />

            <Chronometer
                android:id="@+id/countdown"
                android:layout_width="wrap_content"
                android:layout_height="wrap_content"
                android:layout_marginStart="6dp"
                android:textSize="26sp"
                android:textStyle="bold" />

            <TextView
                android:id="@+id/message"
                android:layout_width="wrap_content"
                android:layout_height="wrap_content"
                android:ellipsize="end"
                android:maxLines="1"
                android:textSize="18sp"
                android:visibility="gone" />

            <TextView
                android:id="@+id/detail"
                android:layout_width="0dp"
                android:layout_height="wrap_content"
                android:layout_marginStart="8dp"
                android:layout_weight="1"
                android:ellipsize="end"
                android:gravity="end"
                android:maxLines="1"
                android:textSize="13sp" />
        </LinearLayout>

        <TextView
            android:id="@+id/note"
            android:layout_width="wrap_content"
            android:layout_height="wrap_content"
            android:maxLines="1"
            android:textSize="12sp"
            android:visibility="gone" />
    </LinearLayout>
</LinearLayout>
```

`$RES/xml/departure_widget_info.xml`. `configuration_optional` lets the widget
be placed without opening settings first; `SettingsActivity` also answers
`RESULT_OK` at once (Task 10), for a launcher that opens it anyway:

```xml
<?xml version="1.0" encoding="utf-8"?>
<!-- updatePeriodMillis is 0: the system's own timer floors at 30 minutes and
     cannot be relied on; WorkManager and the render alarm do the work. -->
<appwidget-provider xmlns:android="http://schemas.android.com/apk/res/android"
    android:configure="io.github.msmgreen.nextdeparture.SettingsActivity"
    android:description="@string/widget_description"
    android:initialLayout="@layout/widget_departure"
    android:minWidth="130dp"
    android:minHeight="40dp"
    android:minResizeWidth="130dp"
    android:minResizeHeight="40dp"
    android:resizeMode="horizontal|vertical"
    android:targetCellWidth="4"
    android:targetCellHeight="1"
    android:updatePeriodMillis="0"
    android:widgetCategory="home_screen"
    android:widgetFeatures="reconfigurable|configuration_optional" />
```

`$RES/drawable/widget_background.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<shape xmlns:android="http://schemas.android.com/apk/res/android" android:shape="rectangle">
    <solid android:color="@color/widget_bg" />
    <corners android:radius="@android:dimen/system_app_widget_background_radius" />
</shape>
```

`$RES/drawable/badge.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<!-- White, so setColorFilter can paint it any route colour. -->
<shape xmlns:android="http://schemas.android.com/apk/res/android" android:shape="rectangle">
    <solid android:color="#FFFFFFFF" />
    <corners android:radius="3dp" />
</shape>
```

`$RES/values/colors.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <color name="widget_bg">#F2F7F7F2</color>
    <color name="widget_text">#FF1B1B1B</color>
    <color name="widget_text_dim">#FF8A8A85</color>
    <color name="widget_badge">#FF6B7A8F</color>
</resources>
```

`$RES/values-night/colors.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <color name="widget_bg">#E6202124</color>
    <color name="widget_text">#FFF1F1EE</color>
    <color name="widget_text_dim">#FF8E8E8A</color>
    <color name="widget_badge">#FF8FA0B5</color>
</resources>
```

`$RES/values/strings_widget.xml` (the template's `strings.xml` keeps
`app_name`; set it to `Next Departure`):

```xml
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <string name="widget_description">The next departure you can still catch</string>
    <string name="leave_in">Leave in</string>
    <string name="tap_to_set_up">Tap to set up</string>
</resources>
```

- [ ] **Step 4: The manifest**

In `android/app/src/main/AndroidManifest.xml`, keep the template's
`<application>` attributes and add the permissions, the configure action on
`SettingsActivity`, and the two receivers:

```xml
    <uses-permission android:name="android.permission.INTERNET" />
    <uses-permission android:name="android.permission.RECEIVE_BOOT_COMPLETED" />
    <!-- Re-render the widget on time (widget spec 6). The user grants it, under
         Alarms & reminders; without it the render alarm is inexact. -->
    <uses-permission android:name="android.permission.SCHEDULE_EXACT_ALARM" />

    <application ...>
        <!-- The template's activity, renamed, gains the configure action. -->
        <activity
            android:name=".SettingsActivity"
            android:exported="true">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
            <intent-filter>
                <action android:name="android.appwidget.action.APPWIDGET_CONFIGURE" />
            </intent-filter>
        </activity>

        <receiver
            android:name=".DepartureWidget"
            android:exported="false">
            <intent-filter>
                <action android:name="android.appwidget.action.APPWIDGET_UPDATE" />
            </intent-filter>
            <meta-data
                android:name="android.appwidget.provider"
                android:resource="@xml/departure_widget_info" />
        </receiver>

        <!-- System broadcasts reach unexported receivers. -->
        <receiver
            android:name=".BootReceiver"
            android:exported="false">
            <intent-filter>
                <action android:name="android.intent.action.BOOT_COMPLETED" />
            </intent-filter>
        </receiver>
    </application>
```

- [ ] **Step 5: Build it**

Run: `./gradlew :core:test :app:assembleDebug`
Expected: `BUILD SUCCESSFUL`. Fix whatever does not compile without changing
what it does; any change of behaviour belongs in `core`, with a test.

- [ ] **Step 6: Check it on the phone — PHONE**

`./gradlew :app:installDebug`, then on the phone:

1. Long-press the home screen → Widgets → Next Departure → place it at 4×1.
   Expected: `Next Departure  Tap to set up`.
2. Tap it. Expected: the app opens.
3. Restart the phone. Expected: the widget is there and still says
   `Tap to set up`; `adb logcat -s NextDeparture` shows nothing alarming
   (there is no config, so no requests).

- [ ] **Step 7: Commit**

```bash
git add android/app
git commit -m "Add the widget: a WidgetView drawn into RemoteViews

The countdown is a Chronometer, so it ticks with no process running. A
local render alarm redraws it just after leave-by and at the first stale
second. Taps, reboots and the 30-minute baseline all fetch through one
mutex."
```

---

### Task 10: Settings and the setup check — **PHONE**

Not compiled when this plan was written (see Scope).

**Files:**
- Modify: `$APP/SettingsActivity.kt` (replace the template's contents)

**Interfaces:**
- Consumes: `Store`, `OkAt`, `RefreshWorker` (Task 9), `ConfigCodec`, `Config`, `Place`, `WatchConfig` (Task 5), `AtUrls`, `AtParse` (Task 2)
- Produces: the phone's config; the end of phase 1's code

- [ ] **Step 1: Write `SettingsActivity.kt`**

Replace the template's file with:

```kotlin
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
                        Modifier.padding(16.dp).verticalScroll(rememberScrollState()),
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
```

`ConfigCodec.validate` is the only judge of a valid config; the form never
second-guesses it. **Check** is the portal's button: it confirms a stop code
against AT and shows the stop's name before anything is saved.

- [ ] **Step 2: Build it**

Run: `./gradlew :core:test :app:assembleDebug`
Expected: `BUILD SUCCESSFUL`.

- [ ] **Step 3: Configure it on the phone — PHONE**

`./gradlew :app:installDebug`, open the app, and:

1. Type a stop code you use and press **Check**. Expected: the stop's name.
   Type `99999999` and **Check**. Expected: `No such stop`.
2. Fill in the place, the label, the toward stop (Check it too) and the walk
   time, and **Save**. Expected: `Saved`, and within a few seconds the widget
   shows the place, the route, `Leave in` with a ticking countdown, and the
   departure time with `live` or `sched`.
3. Save with an empty toward stop. Expected: the form says
   `the toward stop is 1 to 11 letters or digits`, and nothing changes.
4. Work down the setup check, pressing **Fix** on each ✗ and coming back.
   Expected: each turns to ✓ on return. Then add the app to
   *Battery → Background usage limits → Never sleeping apps* by hand.

- [ ] **Step 4: Commit**

```bash
git add android/app
git commit -m "Add the settings form and the setup check

Check confirms a stop code against AT before it is saved, as the
portal's button does. The setup check reads each requirement back from
the system, because an app One UI has put to sleep leaves a widget that
goes quietly stale."
```

---

### Task 11: On both phones — **PHONE**

The phase-1 checks from widget spec 13, run on both phones. Neither this task
nor the next writes code unless a check fails, and a failed check is fixed in
`core` with a test wherever the fault is a decision rather than plumbing.

**Files:** none, unless a check fails.

- [ ] **Step 1: Install on the second phone**

Same build, same machine (or any machine whose `local.properties` names the
same keystore): `./gradlew :app:installDebug`. Configure it with that phone's
own place, and work through its setup check, including *Never sleeping apps*.

- [ ] **Step 2: Run the checks, on each phone**

Watch with `adb logcat -s NextDeparture`; every request logs one `GET` line.

1. **Tap refreshes.** Tap the widget. Expected: a `GET …/tripupdates` line and
   a redraw within about 2 s.
2. **Leave-by hands over, offline.** A minute before the countdown reaches
   zero, turn on airplane mode. Expected: at zero the next catchable departure
   takes over with no network.
3. **Stale is shown.** Stay in airplane mode. Expected: two minutes after the
   last fetch the widget dims, `live` disappears, and `as of 7:31` appears.
   Turn airplane mode off and tap: it is fresh again.
4. **Reboot.** Restart the phone. Expected: within a minute of unlocking the
   countdown is correct again, not a huge or negative number.
5. **Exact alarms refused.** Turn off *Alarms & reminders* for the app.
   Expected: the setup check shows ✗; the widget still hands over at leave-by,
   possibly late. Turn it back on.
6. **The night.** Leave the phone idle overnight. Expected: at the first look
   in the morning, `as of` is within about an hour — the 30-minute job may run
   anywhere in each window, so two runs can be nearly an hour apart. Much
   older means it did not run while the phone dozed: recheck *Unrestricted*
   and *Never sleeping apps*, then
   `adb shell dumpsys jobscheduler | grep -A5 nextdeparture`.
7. **2×1.** Resize the widget to 2×1. Expected: the title and the countdown
   only. If Samsung's grid picks the wrong layout at either size, adjust the
   two `SizeF` thresholds in `DepartureWidget.renderAll`, measuring the
   widget's real size from `AppWidgetManager.getAppWidgetOptions`.
8. **Dark mode.** Switch the phone to dark mode. Expected: legible, with the
   dimmed state still distinguishable from the fresh one.

- [ ] **Step 3: A week later**

Settings → Battery → the app. Expected: negligible use. Record the figure for
Task 12.

---

### Task 12: Write down how it landed

**Files:**
- Modify: `README.md`, `CONTRIBUTING.md`, `docs/design/specs/2026-10-18-android-widget-design.md` (status line), this plan (the section below)

- [ ] **Step 1: `README.md`**

Add a short `## On your phone` section after "Point it at your own stops": what
the widget is (one departure you can still catch, with a leave-by countdown),
that it is a personal app installed over USB rather than from the Play Store,
the `android/local.properties` keys, `./gradlew :app:installDebug`, and a link
to the spec. Add `./gradlew :core:test` (from `android/`) to the Development
commands.

- [ ] **Step 2: `CONTRIBUTING.md`**

Under "Getting set up", add the widget's suite beside the other two:

```bash
cd android && ./gradlew :core:test   # the widget logic, on a JVM
```

- [ ] **Step 3: The spec's status line**

`Status: designed, not built` becomes
`Status: phase 1 built (docs/design/plans/2026-10-19-android-widget-phase-1.md); phases 2-4 designed, not built`.

- [ ] **Step 4: Fill in "How it landed" below**

What failed to compile in Tasks 8–10 and what it took; any check in Task 11
that failed and how it was fixed; the `SizeF` thresholds if they changed; the
week's battery figure.

- [ ] **Step 5: Final verification**

Run: `./gradlew :core:test :app:assembleDebug` (from `android/`) → PASS.
Run: `python -m pytest -q` → all pass (on Windows; see CONTRIBUTING).
Run: `pio test -e native` → all pass.

- [ ] **Step 6: Commit**

```bash
git add README.md CONTRIBUTING.md docs/
git commit -m "Record how the widget's first phase landed"
```

---

## How it landed

*To be written by Task 12.*

## Deferred

Out of scope for this plan, per the spec's Phasing and §15:

- Commute mode: windows, boundary alarms, the foreground service, screen-on
  refresh, the notification, `Not today` (phase 2)
- Places by location: geofences, the most-recently-entered rule, the manual
  flip (phase 3)
- Live Updates in the Now Bar, the leave-now alert, several watches per place,
  the nearest stop by GPS (§15)
