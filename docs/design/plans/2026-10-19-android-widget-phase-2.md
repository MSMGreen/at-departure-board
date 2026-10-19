# Android Widget Phase 2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Commute mode. During configured windows (say weekdays 07:30–08:30 and
16:00–18:00) the phone shows the window's place, refreshes every 30 s while the
screen is on and not at all while it is off, and posts a lock-screen
notification that does the widget's job, with **Not today** for a day at home.

**Architecture:** As in phase 1, every decision is a pure function in
`android/core`, tested on a laptop: the windows in the config, which window is
open and when the next boundary falls (across both DST changes), which place
is current, what the notification says, and how long the screen-on loop waits.
`android/app` adds one exact alarm for the next window boundary, a receiver
that starts or stops a `specialUse` foreground service, and that service, which
registers for `SCREEN_ON` / `SCREEN_OFF` at runtime and fetches through the
existing `Refresher`. The settings screen gains several places and the windows.

**Tech Stack:** As phase 1: Kotlin 2.4.20, kotlinx-serialization-json 1.11.0,
Gradle 9.7.1, JUnit 5 through `kotlin("test")`, AGP 9.4.1, Compose BOM
2026.09.00. No new libraries. `minSdk` 34, `targetSdk` 36.

**Spec:** `docs/design/specs/2026-10-18-android-widget-design.md`, phase 2 of
its Phasing section: §3 (commute windows), §7 rules 3 and 4, §9 (commute
mode), §10 (401, 429/5xx), §11 (permissions, setup check), §13 (window tests,
phone checks 4–7, 10, 12). Phase 1's plan,
`docs/design/plans/2026-10-19-android-widget-phase-1.md`, is done; this plan
builds on its code.

## Scope

**In scope:** windows in the config; window arithmetic; the current place by
window; the boundary alarm; the commute service, its screen-on loop and its
notification with **Refresh** and **Not today**; the tap-to-start fallback;
re-arming on boot, update, save and every periodic run; several places and
the windows on the settings screen; the notifications row in the setup check.

**Not in scope:** geofences, the most-recently-entered rule and the manual flip
(phase 3). Two things the spec lists under commute mode belong to them and wait
for phase 3:

- the notification's **Switch place** action, which is the manual flip; and
- the place name's **`(by time)`** suffix (§10). It marks a place chosen by
  window *because location is unavailable*; until phase 3 there is no location,
  so every window would carry it and it would say nothing.

**Decisions this plan makes where the spec is silent:**

- A window names its place by index, `default_place`, into `places`. Removing a
  place moves each window's index with its place, or to the first place when
  its own is the one removed (`placeAfterRemoval`).
- At a window's end the app forces one fetch. The window's place gives way to
  the first place, and without a fetch the widget would show `Starting` for up
  to the 30 minutes until the next periodic run.
- The boundary receiver decides from the clock, not from which alarm fired, so
  a late alarm, an overlap or a settings change in between all come out right.
- `Not today` also returns the widget to the first place for the rest of the
  day: it closes the windows, so rule 3 no longer applies.

**How this plan was checked.** Every file below was built while this plan was
written, task by task in the order given, test first: each red step's error is
the one observed, and each task's test count is the count observed. Unlike
phase 1, the `app` tasks were compiled too (`:app:assembleDebug`, and
`:app:lintDebug` after Task 5, with no errors). Nothing in `app` has yet run on
a phone; Task 7 is that.

## Global Constraints

- **`android/core` never imports Android.** Anything that needs a `Context`
  lives in `app`.
- **Window times are local `Pacific/Auckland` wall-clock times, `start < end`.
  A window may not cross midnight.** Window arithmetic is a pure function in
  `core`, tested across both DST changes (spec §9).
- **Up to four windows** (`MAX_WINDOWS`), each with a day mask that defaults to
  Monday–Friday.
- **Inside a window the phone refreshes whenever the screen is on, and not at
  all while it is off** (spec §9). Screen on: fetch at once, then every 30 s
  (`RT_FAST_S`). The service holds **no wake lock**.
- **One exact alarm at a time**, at the next window start or end
  (`setExactAndAllowWhileIdle`). On boot, update or settings save the app
  **never starts the service directly**: it arms the alarm, for now if already
  inside a window.
- **The service is `specialUse`**, with a `PROPERTY_SPECIAL_USE_FGS_SUBTYPE`
  description.
- **The notification:** channel `Commute`, low importance, silent, ongoing.
  Title `Leave in 4 min · 20 → Wynyard Quarter`; text
  `7:42 · 2 min late · live as of 7:31`; a countdown to `leave_by`. The title
  after the dot is the widget's own title (the route and the watch's label), so
  the two never disagree.
- **401 stops the loop** until the next window; **429/5xx back off** to a
  5-minute cap with the last good data kept (phase 1's `Fetcher` already does
  both; the loop honours them).
- **`Not today` stops the service and suppresses every window until local
  midnight.**
- **If the service cannot start**, post `Commute mode didn't start — tap to
  start it`; the tap is a user-initiated start.
- **No one's home goes in the repository.** Places and windows live only in
  each phone's settings.
- **Commit messages follow this repo's style:** sentence-case imperative
  subject, no `feat:`/`fix:` prefixes.
- **Installing on a phone is a human step.** Task 7 is marked **PHONE**.
- `./gradlew :core:test :app:assembleDebug`, run from `android/`, stays green
  after every task.

## Review Focus

The inputs the spec implies but its own test list does not pin, most likely to
bite first. Each has a test in the task that owns the code.

1. **A window removed, or `Not today`, while the service runs** — the service
   stops at once rather than at the old window's end. `CommuteAlarm.arm` stops
   it whenever no window is open; the rule it relies on, that `Not today`
   closes every window until midnight and no longer, is tested in Task 2.
2. **Overlapping windows** (07:00–09:00 and 08:00–10:00) — commute mode stays
   on across the first window's end, and the next boundary is always strictly
   later, so an alarm never re-arms itself. Task 2.
3. **A start time in the spring-forward gap** (02:30 on 27 September 2026) —
   the window starts late, at 03:30, and is never lost. Task 2.
4. **Removing a place a window points at** — that window falls back to the
   first place, and every other window keeps its own place. Task 1.
5. **A 401 inside a window** — the loop stops, rather than asking AT every 30 s
   for an hour with a key it has already refused. Task 3.

---

## File Structure

Paths abbreviate `android/core/src/main/kotlin/io/github/msmgreen/nextdeparture/core`
as `$CORE`, the same under `src/test` as `$CORE_TEST`,
`android/app/src/main/kotlin/io/github/msmgreen/nextdeparture` as `$APP`, and
`android/app/src/main/res` as `$RES`.

| File | Responsibility | Task |
|---|---|---|
| `$CORE/Config.kt` | `CommuteWindow`, `parseClock`, `placeAfterRemoval`, window validation | 1 |
| `$CORE/Windows.kt` | `activeWindow`, `nextBoundary`, `endOfLocalDay` | 2 |
| `$CORE/Commute.kt` | `currentPlace`, `commuteNotice`, `commuteDelay` | 3 |
| `$CORE_TEST/ConfigWindowsTest.kt`, `WindowsTest.kt`, `CommuteTest.kt` | One suite per file above | 1–3 |
| `$APP/Store.kt` | Not today's end; the current place | 4 |
| `$APP/Refresher.kt`, `$APP/DepartureWidget.kt` | Fetch and draw the current place, not the first | 4, 5 |
| `$APP/CommuteAlarm.kt` | The one boundary alarm | 5 |
| `$APP/BoundaryReceiver.kt` | Start or stop the service at a boundary | 5 |
| `$APP/CommuteService.kt` | The service, the screen-on loop, the notification | 5 |
| `$APP/BootReceiver.kt`, `$APP/RefreshWorker.kt` | Re-arm on boot, update and every periodic run | 5 |
| `$RES/values/strings_commute.xml`, `$RES/drawable/ic_commute.xml` | The notification's words and icon | 5 |
| `android/app/src/main/AndroidManifest.xml` | Permissions, the service, the receivers | 5 |
| `$APP/SettingsActivity.kt` | Places and windows; the notifications check | 6 |

## Risks

| # | Risk | Mitigation |
|---|---|---|
| 1 | **Nothing in `app` has run on a phone** | Task 7 runs spec §13's commute checks by hand, with windows set two minutes ahead |
| 2 | **One UI sleeps the app**, and the morning window never starts | The setup check; Task 7's overnight check (spec §13 check 10) |
| 3 | **The foreground-service start is refused** from the alarm | The tap-to-start notification (spec §9); Task 7 notes whether it ever appears |
| 4 | **Exact alarms not allowed** | Inexact boundaries; the setup check says so; Task 7 check 8 |
| 5 | **A long screen-on session** | At most 120 realtime requests an hour while someone is looking (spec appendix) |

---

### Task 1: Commute windows in the config

**Files:**
- Modify: `$CORE/Config.kt`
- Create: `$CORE_TEST/ConfigWindowsTest.kt`

**Interfaces:**
- Consumes: `Config`, `Place`, `WatchConfig`, `ConfigCodec`, `ConfigError` (phase 1)
- Produces: `CommuteWindow(days: Int = WEEKDAYS, start: String, end: String, defaultPlace: Int = 0)` with `startTime`/`endTime: LocalTime`; `Config.windows: List<CommuteWindow> = emptyList()`; `WEEKDAYS`, `MAX_WINDOWS`; `parseClock(String): LocalTime?`; `placeAfterRemoval(place: Int, removed: Int): Int`; `ConfigError.TooManyWindows`, `WindowDays`, `WindowTime`, `WindowPlace` — used by Tasks 2, 3 and 6

- [ ] **Step 1: Write the failing test**

`$CORE_TEST/ConfigWindowsTest.kt`:

```kotlin
package io.github.msmgreen.nextdeparture.core

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class ConfigWindowsTest {
    private val home = Place("Home", WatchConfig("to Wynyard Quarter", "8213", "20", "1060"), walkMin = 4)
    private val work = Place("Work", WatchConfig("home", "1060", "20", "8213"), walkMin = 2)
    private val morning = CommuteWindow(start = "07:30", end = "08:30", defaultPlace = 0)
    private val evening = CommuteWindow(start = "16:00", end = "18:00", defaultPlace = 1)
    private val ok = Config(places = listOf(home, work), windows = listOf(morning, evening))

    private fun error(vararg w: CommuteWindow) = ConfigCodec.validate(ok.copy(windows = w.toList()))

    @Test fun `round-trips with the spec's field names`() {
        val text = ConfigCodec.toJson(ok)
        for (k in listOf("\"windows\"", "\"days\":31", "\"start\":\"07:30\"", "\"end\":\"08:30\"", "\"default_place\":1")) {
            assertTrue(k in text, "$k in $text")
        }
        assertEquals(ConfigResult.Ok(ok), ConfigCodec.parse(text))
    }

    @Test fun `a phase 1 config reads back with no windows`() {
        val text = """{"v":1,"places":[{"name":"Home","watch":{"label":"","stop_code":"8213","toward_stop_code":"1060"}}]}"""
        assertEquals(emptyList(), (ConfigCodec.parse(text) as ConfigResult.Ok).config.windows)
    }

    @Test fun `weekdays by default, Monday in bit 0`() {
        assertEquals(0b0011111, morning.days)
        assertEquals(WEEKDAYS, morning.days)
    }

    @Test fun `at most four windows`() {
        assertEquals(null, error(*Array(4) { morning }))
        assertEquals(ConfigError.TooManyWindows, error(*Array(5) { morning }))
    }

    @Test fun `a window has at least one day and no more than seven`() {
        assertEquals(ConfigError.WindowDays, error(morning.copy(days = 0)))
        assertEquals(ConfigError.WindowDays, error(morning.copy(days = 0b10000000)))
        assertEquals(null, error(morning.copy(days = 0b1111111)))
    }

    @Test fun `times are HH colon MM, and the end is later the same day`() {
        for (bad in listOf("7:30", "07.30", "24:00", "07:60", "", "0730 ", "٠٧:٣٠")) {
            assertEquals(ConfigError.WindowTime, error(morning.copy(start = bad)), bad)
        }
        assertEquals(ConfigError.WindowTime, error(morning.copy(start = "08:30", end = "08:30")))
        assertEquals(ConfigError.WindowTime, error(morning.copy(start = "23:00", end = "01:00"))) // no crossing midnight
        assertEquals(null, error(morning.copy(start = "00:00", end = "23:59")))
    }

    @Test fun `removing a place keeps each window on the same place, or the first`() {
        // Places Home, Work, Gym; Work (1) is removed.
        assertEquals(0, placeAfterRemoval(0, removed = 1))
        assertEquals(0, placeAfterRemoval(1, removed = 1)) // its own place is gone
        assertEquals(1, placeAfterRemoval(2, removed = 1)) // Gym moves down one
        assertEquals(0, placeAfterRemoval(0, removed = 0))
    }

    @Test fun `a window's place is one of the places`() {
        assertEquals(ConfigError.WindowPlace, error(morning.copy(defaultPlace = 2)))
        assertEquals(ConfigError.WindowPlace, error(morning.copy(defaultPlace = -1)))
    }
}
```

- [ ] **Step 2: Run it to make sure it fails**

Run (from `android/`): `./gradlew :core:test`
Expected: FAIL at `:core:compileTestKotlin` with `Unresolved reference 'CommuteWindow'`.

- [ ] **Step 3: Add windows to the config**

In `$CORE/Config.kt`, add the import beside the others:

```kotlin
import java.time.LocalTime
```

Below `MAX_PLACES`:

```kotlin
const val MAX_WINDOWS = 4
const val WEEKDAYS = 0b0011111 // Monday to Friday; bit 0 is Monday
```

Replace `data class Config(...)` with the window, its two helpers and the new
`Config`:

```kotlin
// A commute window (widget spec 3, 9): local Pacific/Auckland wall-clock times,
// [start, end) on the days in the mask, never crossing midnight.
@Serializable
data class CommuteWindow(
    val days: Int = WEEKDAYS,
    val start: String, // "07:30"
    val end: String, // "08:30"
    @SerialName("default_place") val defaultPlace: Int = 0, // index into places
) {
    // Only for a validated window: validate() rejects a time that will not parse.
    val startTime: LocalTime get() = parseClock(start)!!
    val endTime: LocalTime get() = parseClock(end)!!
}

// A window's default place after the place at removed is deleted: the same
// place where it survives, else the first.
fun placeAfterRemoval(place: Int, removed: Int): Int = when {
    place == removed -> 0
    place > removed -> place - 1
    else -> place
}

// "07:30", strictly: two ASCII digits, a colon, two digits.
fun parseClock(s: String): LocalTime? {
    if (s.length != 5 || s[2] != ':') return null
    if (!s.removeRange(2, 3).all { it in '0'..'9' }) return null
    val h = s.substring(0, 2).toInt()
    val m = s.substring(3, 5).toInt()
    if (h > 23 || m > 59) return null
    return LocalTime.of(h, m)
}

// Windows arrived in phase 2 with a default, so a phase 1 config still reads
// back and "v" stays 1.
@Serializable
data class Config(
    val v: Int = CONFIG_VERSION,
    val places: List<Place>,
    val windows: List<CommuteWindow> = emptyList(),
)
```

Add four errors at the end of `ConfigError`:

```kotlin
    TooManyWindows("at most four commute windows"),
    WindowDays("a window needs at least one day"),
    WindowTime("a window is HH:MM to a later HH:MM the same day"),
    WindowPlace("a window's place must be one of the places"),
```

And in `ConfigCodec.validate`, after the loop over places and before
`return null`:

```kotlin
        if (c.windows.size > MAX_WINDOWS) return ConfigError.TooManyWindows
        for (w in c.windows) {
            if (w.days !in 1..0b1111111) return ConfigError.WindowDays
            val start = parseClock(w.start) ?: return ConfigError.WindowTime
            val end = parseClock(w.end) ?: return ConfigError.WindowTime
            if (!start.isBefore(end)) return ConfigError.WindowTime
            if (w.defaultPlace !in c.places.indices) return ConfigError.WindowPlace
        }
```

- [ ] **Step 4: Run the tests**

Run: `./gradlew :core:test`
Expected: PASS, 105 tests (phase 1's 97 and these 8).

- [ ] **Step 5: Commit**

```bash
git add android/core
git commit -m "Add commute windows to the config

Up to four, each a local [start, end) on the days in a mask, never
crossing midnight, naming its place by index. A phase 1 config reads
back with none, so v stays 1."
```

---

### Task 2: Window arithmetic

**Files:**
- Create: `$CORE/Windows.kt`, `$CORE_TEST/WindowsTest.kt`

**Interfaces:**
- Consumes: `CommuteWindow` (Task 1), `AKL`, `local` (phase 1's `NzTime.kt`)
- Produces: `activeWindow(windows: List<CommuteWindow>, now: Long, quietUntil: Long = 0): CommuteWindow?`, `nextBoundary(windows: List<CommuteWindow>, now: Long): Long?`, `endOfLocalDay(now: Long): Long` — used by Tasks 3, 4 and 5

- [ ] **Step 1: Write the failing test**

`$CORE_TEST/WindowsTest.kt`. 19 October 2026 is a Monday; NZDT starts on
27 September 2026 and ends on 4 April 2027 (spec §13):

```kotlin
package io.github.msmgreen.nextdeparture.core

import java.time.ZonedDateTime
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class WindowsTest {
    private fun at(y: Int, mo: Int, d: Int, h: Int, mi: Int, s: Int = 0) =
        ZonedDateTime.of(y, mo, d, h, mi, s, 0, AKL).toEpochSecond()

    private val morning = CommuteWindow(start = "07:30", end = "08:30", defaultPlace = 0)
    private val evening = CommuteWindow(start = "16:00", end = "18:00", defaultPlace = 1)
    private val both = listOf(morning, evening)

    // 2026-10-19 is a Monday; 2026-10-24 a Saturday.
    @Test fun `open from start until end, on the days in the mask`() {
        assertEquals(morning, activeWindow(both, at(2026, 10, 19, 7, 30)))
        assertEquals(morning, activeWindow(both, at(2026, 10, 19, 8, 29, 59)))
        assertNull(activeWindow(both, at(2026, 10, 19, 8, 30)))
        assertNull(activeWindow(both, at(2026, 10, 19, 7, 29, 59)))
        assertEquals(evening, activeWindow(both, at(2026, 10, 19, 17, 0)))
        assertNull(activeWindow(both, at(2026, 10, 24, 7, 45))) // Saturday
        assertEquals(morning.copy(days = 0b0100000), activeWindow(listOf(morning.copy(days = 0b0100000)), at(2026, 10, 24, 7, 45)))
    }

    @Test fun `no windows, never open`() {
        assertNull(activeWindow(emptyList(), at(2026, 10, 19, 8, 0)))
        assertNull(nextBoundary(emptyList(), at(2026, 10, 19, 8, 0)))
    }

    @Test fun `Not today closes every window until it runs out`() {
        val quiet = endOfLocalDay(at(2026, 10, 19, 6, 0))
        assertNull(activeWindow(both, at(2026, 10, 19, 7, 45), quiet))
        assertNull(activeWindow(both, at(2026, 10, 19, 17, 0), quiet))
        assertEquals(morning, activeWindow(both, at(2026, 10, 20, 7, 45), quiet)) // and no longer
    }

    @Test fun `the end of the local day is the next local midnight`() {
        assertEquals(at(2026, 10, 20, 0, 0), endOfLocalDay(at(2026, 10, 19, 23, 59)))
        assertEquals(at(2026, 10, 20, 0, 0), endOfLocalDay(at(2026, 10, 19, 0, 0)))
        // 27 September 2026 is 23 hours long: NZDT starts at 02:00.
        assertEquals(at(2026, 9, 27, 0, 30) + 22 * 3600 + 1800, endOfLocalDay(at(2026, 9, 27, 0, 30)))
    }

    @Test fun `the next boundary is strictly later`() {
        assertEquals(at(2026, 10, 19, 7, 30), nextBoundary(both, at(2026, 10, 19, 6, 0)))
        assertEquals(at(2026, 10, 19, 8, 30), nextBoundary(both, at(2026, 10, 19, 7, 30))) // at a start: its end
        assertEquals(at(2026, 10, 19, 16, 0), nextBoundary(both, at(2026, 10, 19, 8, 30)))
    }

    @Test fun `the next boundary after Friday evening is Monday morning`() {
        assertEquals(at(2026, 10, 26, 7, 30), nextBoundary(both, at(2026, 10, 23, 18, 0)))
    }

    @Test fun `a window on one day of the week is found a week ahead`() {
        val monday = listOf(morning.copy(days = 0b0000001))
        assertEquals(at(2026, 10, 26, 7, 30), nextBoundary(monday, at(2026, 10, 19, 8, 30)))
    }

    @Test fun `overlapping windows hand over at each boundary`() {
        val early = CommuteWindow(start = "07:00", end = "09:00")
        val late = CommuteWindow(start = "08:00", end = "10:00")
        val w = listOf(early, late)
        assertEquals(at(2026, 10, 19, 8, 0), nextBoundary(w, at(2026, 10, 19, 7, 0)))
        assertEquals(at(2026, 10, 19, 9, 0), nextBoundary(w, at(2026, 10, 19, 8, 0)))
        assertEquals(late, activeWindow(w, at(2026, 10, 19, 9, 0))) // still open after the first ends
        assertEquals(at(2026, 10, 19, 10, 0), nextBoundary(w, at(2026, 10, 19, 9, 0)))
    }

    @Test fun `across the start of NZDT, local times stay local`() {
        // Friday 25 September 2026, NZST; Monday 28 September, NZDT.
        val next = nextBoundary(both, at(2026, 9, 25, 18, 0))!!
        assertEquals(at(2026, 9, 28, 7, 30), next)
        assertEquals("07:30", "%02d:%02d".format(local(next).hour, local(next).minute))
    }

    @Test fun `across the end of NZDT, local times stay local`() {
        // Friday 2 April 2027, NZDT; Monday 5 April, NZST.
        val next = nextBoundary(both, at(2027, 4, 2, 18, 0))!!
        assertEquals(at(2027, 4, 5, 7, 30), next)
        assertEquals(7, local(next).hour)
    }

    @Test fun `a start in the spring-forward gap is late, never lost`() {
        // 02:30 on 27 September 2026 does not exist; it becomes 03:30 NZDT.
        val sunday = listOf(CommuteWindow(days = 0b1000000, start = "02:30", end = "04:00"))
        assertEquals(at(2026, 9, 27, 3, 30), nextBoundary(sunday, at(2026, 9, 26, 12, 0)))
        assertNull(activeWindow(sunday, at(2026, 9, 27, 3, 15)))
        assertEquals(sunday[0], activeWindow(sunday, at(2026, 9, 27, 3, 45)))
    }
}
```

- [ ] **Step 2: Run it to make sure it fails**

Run: `./gradlew :core:test`
Expected: FAIL at `:core:compileTestKotlin` with `Unresolved reference 'activeWindow'`.

- [ ] **Step 3: Write `Windows.kt`**

`$CORE/Windows.kt`:

```kotlin
package io.github.msmgreen.nextdeparture.core

import java.time.LocalDate
import java.time.LocalTime

// Commute windows as pure functions of the clock (widget spec 9), so that the
// boundaries are tested across both DST changes on a laptop. Times are local
// wall-clock times, and a window never crosses midnight, so the window open at
// any moment is one of that local day's.

private fun CommuteWindow.on(date: LocalDate): Boolean = days and (1 shl (date.dayOfWeek.value - 1)) != 0

// A wall-clock time on a date, as an epoch. In the spring-forward gap java.time
// moves the time on by the gap's length, so a 02:30 start becomes 03:30: late,
// never lost. In the autumn overlap it takes the earlier of the two.
private fun epochAt(date: LocalDate, t: LocalTime): Long = date.atTime(t).atZone(AKL).toEpochSecond()

// The window open at now, if any. quietUntil is Not today's end: until then no
// window is open.
fun activeWindow(windows: List<CommuteWindow>, now: Long, quietUntil: Long = 0): CommuteWindow? {
    if (now < quietUntil) return null
    val date = local(now).toLocalDate()
    return windows.firstOrNull { w ->
        w.on(date) && now >= epochAt(date, w.startTime) && now < epochAt(date, w.endTime)
    }
}

// The first window start or end strictly after now, looking a week ahead; null
// when there are no windows. Strictly after, so an alarm that fires exactly on
// a boundary arms the next one rather than itself again.
fun nextBoundary(windows: List<CommuteWindow>, now: Long): Long? {
    val today = local(now).toLocalDate()
    return (0L..7L).asSequence()
        .map { today.plusDays(it) }
        .flatMap { date -> windows.filter { it.on(date) }.flatMap { listOf(epochAt(date, it.startTime), epochAt(date, it.endTime)) } }
        .filter { it > now }
        .minOrNull()
}

// The next local midnight: when Not today runs out.
fun endOfLocalDay(now: Long): Long = local(now).toLocalDate().plusDays(1).atStartOfDay(AKL).toEpochSecond()
```

- [ ] **Step 4: Run the tests**

Run: `./gradlew :core:test`
Expected: PASS, 116 tests.

- [ ] **Step 5: Commit**

```bash
git add android/core
git commit -m "Work out commute windows from the clock, across both DST changes

Which window is open, and the next start or end strictly after now, so
an alarm firing on a boundary arms the next one. A start in the
spring-forward gap is late, never lost."
```

---

### Task 3: The current place, the notification's words, the loop's pace

**Files:**
- Create: `$CORE/Commute.kt`, `$CORE_TEST/CommuteTest.kt`

**Interfaces:**
- Consumes: `Config`, `Place` (phase 1, Task 1), `activeWindow`, `endOfLocalDay` (Task 2), `WidgetView`, `WatchCache`, `WatchState`, `formatClock`, `RT_FAST_S` (phase 1)
- Produces: `currentPlace(c: Config, now: Long, quietUntil: Long = 0): Place`; `CommuteNotice(title: String, text: String, countdownTo: Long?)`; `commuteNotice(v: WidgetView, lastOk: Long, now: Long, h24: Boolean): CommuteNotice`; `commuteDelay(w: WatchCache?, now: Long): Long?` — used by Tasks 4 and 5

- [ ] **Step 1: Write the failing test**

`$CORE_TEST/CommuteTest.kt`:

```kotlin
package io.github.msmgreen.nextdeparture.core

import java.time.ZonedDateTime
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNull

class CommuteTest {
    private fun at(y: Int, mo: Int, d: Int, h: Int, mi: Int, s: Int = 0) =
        ZonedDateTime.of(y, mo, d, h, mi, s, 0, AKL).toEpochSecond()

    private val home = Place("Home", WatchConfig("to Wynyard Quarter", "8213", "20", "1060"), walkMin = 4)
    private val work = Place("Work", WatchConfig("home", "1060", "20", "8213"), walkMin = 2)
    private val config = Config(
        places = listOf(home, work),
        windows = listOf(
            CommuteWindow(start = "07:30", end = "08:30", defaultPlace = 0),
            CommuteWindow(start = "16:00", end = "18:00", defaultPlace = 1),
        ),
    )

    // Monday 19 October 2026.
    @Test fun `a window shows its default place`() {
        assertEquals(work, currentPlace(config, at(2026, 10, 19, 17, 0)))
        assertEquals(home, currentPlace(config, at(2026, 10, 19, 7, 45)))
    }

    @Test fun `outside every window, the first place`() {
        assertEquals(home, currentPlace(config, at(2026, 10, 19, 12, 0)))
        assertEquals(home, currentPlace(config, at(2026, 10, 19, 18, 0)))
        assertEquals(home, currentPlace(config.copy(windows = emptyList()), at(2026, 10, 19, 17, 0)))
    }

    @Test fun `Not today means home all day`() {
        val quiet = endOfLocalDay(at(2026, 10, 19, 6, 0))
        assertEquals(home, currentPlace(config, at(2026, 10, 19, 17, 0), quiet))
    }

    private val now = at(2026, 10, 19, 7, 31)

    private fun view(
        countdownTo: Long? = now + 4 * 60 + 12,
        message: String? = null,
        detail: String = "7:42 · 2 min late · live",
        note: String? = null,
        asOf: String? = null,
        stale: Boolean = false,
    ) = WidgetView("Home", "20 to Wynyard Quarter", countdownTo, message, detail, note, asOf, stale, null, null)

    @Test fun `the title counts whole minutes to leave-by`() {
        assertEquals("Leave in 4 min · 20 to Wynyard Quarter", commuteNotice(view(), now, now, true).title)
        assertEquals("Leave in 1 min · 20 to Wynyard Quarter", commuteNotice(view(countdownTo = now + 60), now, now, true).title)
        assertEquals("Leave now · 20 to Wynyard Quarter", commuteNotice(view(countdownTo = now + 59), now, now, true).title)
    }

    @Test fun `the text says how old the data is, live or not`() {
        val n = commuteNotice(view(), at(2026, 10, 19, 7, 31), now, true)
        assertEquals("7:42 · 2 min late · live as of 7:31", n.text)
        assertEquals(now + 4 * 60 + 12, n.countdownTo)
    }

    @Test fun `with nothing to count down to, the message leads`() {
        val n = commuteNotice(view(countdownTo = null, message = "Check settings", detail = ""), 0, now, true)
        assertEquals("Check settings · 20 to Wynyard Quarter", n.title)
        assertEquals("", n.text)
        assertNull(n.countdownTo)
    }

    @Test fun `a cancellation is surfaced`() {
        val n = commuteNotice(view(note = "7:36 cancelled"), at(2026, 10, 19, 7, 31), now, true)
        assertEquals("7:42 · 2 min late · live as of 7:31 · 7:36 cancelled", n.text)
    }

    @Test fun `the loop waits 30 s, longer while backing off, and stops on a 401`() {
        assertEquals(30L, commuteDelay(null, now))
        assertEquals(30L, commuteDelay(WatchCache("k", state = WatchState.Ok), now))
        assertEquals(120L, commuteDelay(WatchCache("k", backoffS = 120, backoffUntil = now + 120), now))
        assertEquals(30L, commuteDelay(WatchCache("k", backoffS = 60, backoffUntil = now - 5), now))
        assertNull(commuteDelay(WatchCache("k", state = WatchState.CheckKey), now))
    }
}
```

- [ ] **Step 2: Run it to make sure it fails**

Run: `./gradlew :core:test`
Expected: FAIL at `:core:compileTestKotlin` with `Unresolved reference 'currentPlace'`.

- [ ] **Step 3: Write `Commute.kt`**

`$CORE/Commute.kt`:

```kotlin
package io.github.msmgreen.nextdeparture.core

// Commute mode's decisions (widget spec 7, 9), made here so they are tested on
// a laptop: which place is current, what the notification says, and how long
// the screen-on loop waits.

// Widget spec 7, rules 3 and 4: the open window's default place, else the first
// place. Not today closes the windows, so a day at home shows home. Rules 1 and
// 2 - the manual flip and the place most recently entered - come with
// geofences in phase 3, ahead of these.
fun currentPlace(c: Config, now: Long, quietUntil: Long = 0): Place =
    activeWindow(c.windows, now, quietUntil)?.let { c.places.getOrNull(it.defaultPlace) } ?: c.places.first()

data class CommuteNotice(
    val title: String, // "Leave in 4 min · 20 to Wynyard Quarter"
    val text: String, // "7:42 · 2 min late · live as of 7:31"
    val countdownTo: Long?, // leave-by, for the notification's chronometer
)

// The notification does the widget's job on the lock screen, so it says what
// the widget says. Its title is only redrawn on a fetch, so it counts whole
// minutes; the chronometer beside it has the seconds.
fun commuteNotice(v: WidgetView, lastOk: Long, now: Long, h24: Boolean): CommuteNotice {
    val to = v.countdownTo
    val lead = when {
        to == null -> v.message ?: ""
        to - now < 60 -> "Leave now"
        else -> "Leave in ${(to - now) / 60} min"
    }
    val asOf = if (lastOk > 0) "as of ${formatClock(lastOk, h24)}" else null
    // "as of" belongs to a departure; a message such as "Check settings" has none.
    val line = if (v.detail.isEmpty()) "" else listOfNotNull(v.detail, asOf).joinToString(" ")
    return CommuteNotice(
        title = listOf(lead, v.title).filter { it.isNotEmpty() }.joinToString(" · "),
        text = listOfNotNull(line.ifEmpty { null }, v.note).joinToString(" · "),
        countdownTo = to,
    )
}

// How long the screen-on loop waits before its next fetch (widget spec 9):
// RT_FAST_S, or until the backoff runs out; null to stop, because a 401 will
// not mend itself in 30 s and waits for the next window.
fun commuteDelay(w: WatchCache?, now: Long): Long? {
    if (w?.state == WatchState.CheckKey) return null
    return maxOf(RT_FAST_S.toLong(), (w?.backoffUntil ?: 0) - now)
}
```

- [ ] **Step 4: Run the tests**

Run: `./gradlew :core:test`
Expected: PASS, 124 tests.

- [ ] **Step 5: Commit**

```bash
git add android/core
git commit -m "Decide commute mode's place, words and pace in core

The open window's place, else the first; the notification's title and
text from the widget's own view; and the screen-on loop's wait, which
stops on a 401 rather than asking every 30 s with a refused key."
```

---

### Task 4: Fetch and draw the current place

**Files:**
- Modify: `$APP/Store.kt`, `$APP/Refresher.kt`, `$APP/DepartureWidget.kt`

**Interfaces:**
- Consumes: `currentPlace` (Task 3)
- Produces: `Store.quietUntil(): Long`, `Store.setQuietUntil(epoch: Long)`, `Store.place(now: Long): Place?` — used by Tasks 5 and 6

- [ ] **Step 1: The store**

Replace `$APP/Store.kt` with:

```kotlin
package io.github.msmgreen.nextdeparture

import android.content.Context
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.longPreferencesKey
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import io.github.msmgreen.nextdeparture.core.Cache
import io.github.msmgreen.nextdeparture.core.Config
import io.github.msmgreen.nextdeparture.core.ConfigCodec
import io.github.msmgreen.nextdeparture.core.ConfigResult
import io.github.msmgreen.nextdeparture.core.Place
import io.github.msmgreen.nextdeparture.core.currentPlace
import kotlinx.coroutines.flow.first

// Two JSON documents - the settings and the fetch cache - in one DataStore,
// and Not today's end. DataStore writes atomically, so a process killed
// mid-write keeps the old one.
private val Context.dataStore by preferencesDataStore(name = "state")

private val CONFIG = stringPreferencesKey("config")
private val CACHE = stringPreferencesKey("cache")
private val QUIET_UNTIL = longPreferencesKey("quiet_until")

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

    // Not today (widget spec 9): no window is open before this epoch.
    suspend fun quietUntil(): Long = data.data.first()[QUIET_UNTIL] ?: 0

    suspend fun setQuietUntil(epoch: Long) {
        data.edit { it[QUIET_UNTIL] = epoch }
    }

    // The place to show and fetch now (widget spec 7); null until set up.
    suspend fun place(now: Long): Place? = config()?.let { currentPlace(it, now, quietUntil()) }
}
```

- [ ] **Step 2: The refresher and the widget**

In `$APP/Refresher.kt`, the place is now the current one, so `now` comes
first:

```kotlin
            val store = Store(context)
            val now = System.currentTimeMillis() / 1000
            // Only the current place is fetched; another becomes current at a
            // window boundary, and is fetched then (widget spec 4).
            val place = store.place(now) ?: return@withLock
            val cache = store.cache()
            val last = cache.watch
```

replacing the five lines from `val store` to `val last`, whose
`val now = …` came after the place.

In `$APP/DepartureWidget.kt`, `renderAll` likewise:

```kotlin
            val store = Store(context)
            val now = System.currentTimeMillis() / 1000
            val place = store.place(now)
            if (place == null) {
                manager.updateAppWidget(ids, unconfigured(context))
                return
            }
```

removing the later `val now = …` line.

- [ ] **Step 3: Build**

Run: `./gradlew :core:test :app:assembleDebug`
Expected: `BUILD SUCCESSFUL`, 124 core tests. With no windows saved yet,
`currentPlace` is the first place, so the widget behaves exactly as in phase 1.

- [ ] **Step 4: Commit**

```bash
git add android/app
git commit -m "Fetch and draw the current place rather than the first"
```

---

### Task 5: Commute mode

**Files:**
- Create: `$APP/CommuteAlarm.kt`, `$APP/BoundaryReceiver.kt`, `$APP/CommuteService.kt`, `$RES/values/strings_commute.xml`, `$RES/drawable/ic_commute.xml`
- Modify: `$APP/BootReceiver.kt`, `$APP/RefreshWorker.kt`, `$APP/Refresher.kt`, `$APP/DepartureWidget.kt`, `android/app/src/main/AndroidManifest.xml`

**Interfaces:**
- Consumes: `activeWindow`, `nextBoundary`, `endOfLocalDay` (Task 2); `commuteNotice`, `commuteDelay`, `CommuteNotice` (Task 3); `Store.place`, `Store.quietUntil`, `Store.setQuietUntil` (Task 4); `Refresher`, `RefreshWorker`, `widgetView` (phase 1)
- Produces: `CommuteAlarm.arm(context)`, `CommuteService.notificationsOn(context): Boolean`, `DepartureWidget.refreshIntent(context): PendingIntent` — used by Task 6

- [ ] **Step 1: The boundary alarm**

`$APP/CommuteAlarm.kt`:

```kotlin
package io.github.msmgreen.nextdeparture

import android.app.AlarmManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import io.github.msmgreen.nextdeparture.core.activeWindow
import io.github.msmgreen.nextdeparture.core.nextBoundary

// The one commute boundary alarm (widget spec 9). Whatever may have changed the
// windows - boot, an update, a settings save, Not today, the periodic run -
// calls arm(); only the alarm starts the service.
object CommuteAlarm {
    // Inside an open window with no service running, the alarm is for now:
    // a foreground service may be started from an exact-alarm callback, where a
    // boot receiver or a worker may not start one.
    suspend fun arm(context: Context) {
        val store = Store(context)
        val now = System.currentTimeMillis() / 1000
        val windows = store.config()?.windows.orEmpty()
        val open = activeWindow(windows, now, store.quietUntil()) != null
        // A window removed, or Not today, while the service runs: stop it now.
        if (!open) CommuteService.stop(context)
        set(context, if (open && !CommuteService.running) now else nextBoundary(windows, now))
    }

    // After a boundary has fired: the next one, never now again.
    suspend fun armNext(context: Context) {
        val windows = Store(context).config()?.windows.orEmpty()
        set(context, nextBoundary(windows, System.currentTimeMillis() / 1000))
    }

    private fun set(context: Context, at: Long?) {
        val alarms = context.getSystemService(AlarmManager::class.java)
        val pi = PendingIntent.getBroadcast(
            context, 0, Intent(context, BoundaryReceiver::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        when {
            at == null -> alarms.cancel(pi)
            alarms.canScheduleExactAlarms() -> alarms.setExactAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, at * 1000, pi)
            // Without Alarms & reminders the boundary may land ten minutes or
            // more late; the setup check says so (widget spec 11).
            else -> alarms.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, at * 1000, pi)
        }
    }
}
```

`$APP/BoundaryReceiver.kt`:

```kotlin
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
```

- [ ] **Step 2: The service**

`$APP/CommuteService.kt`:

```kotlin
package io.github.msmgreen.nextdeparture

import android.app.ForegroundServiceStartNotAllowedException
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.ServiceInfo
import android.graphics.drawable.Icon
import android.os.IBinder
import android.os.PowerManager
import android.text.format.DateFormat
import io.github.msmgreen.nextdeparture.core.CommuteNotice
import io.github.msmgreen.nextdeparture.core.activeWindow
import io.github.msmgreen.nextdeparture.core.commuteDelay
import io.github.msmgreen.nextdeparture.core.commuteNotice
import io.github.msmgreen.nextdeparture.core.endOfLocalDay
import io.github.msmgreen.nextdeparture.core.widgetView
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

// Commute mode (widget spec 9). A foreground service whose job is to keep a
// process alive that hears the screen turn on and off - broadcasts a manifest
// receiver cannot get - and to fetch every 30 s while it is on. It holds no
// wake lock: with the screen off, nothing runs.
class CommuteService : Service() {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)
    private var loop: Job? = null

    private val screen = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                Intent.ACTION_SCREEN_ON -> startLoop()
                Intent.ACTION_SCREEN_OFF -> stopLoop()
            }
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        running = true
        val filter = IntentFilter().apply {
            addAction(Intent.ACTION_SCREEN_ON)
            addAction(Intent.ACTION_SCREEN_OFF)
        }
        // System broadcasts still reach a receiver that is not exported.
        registerReceiver(screen, filter, RECEIVER_NOT_EXPORTED)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_NOT_TODAY) {
            scope.launch { notToday() }
            return START_NOT_STICKY
        }
        // Required within seconds of startForegroundService, whatever follows.
        startForeground(NOTICE_ID, build(this, CommuteNotice(getString(R.string.commute_starting), "", null)), ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE)
        scope.launch {
            // A sticky restart may come after the window has closed.
            val store = Store(this@CommuteService)
            val now = System.currentTimeMillis() / 1000
            if (activeWindow(store.config()?.windows.orEmpty(), now, store.quietUntil()) == null) {
                stopSelf()
                return@launch
            }
            update(this@CommuteService)
            // The window has started: fetch once, screen on or not.
            Refresher.refresh(this@CommuteService, force = false)
        }
        if (getSystemService(PowerManager::class.java).isInteractive) startLoop()
        return START_STICKY
    }

    // Screen on: fetch now, then every 30 s, or slower while backing off. The
    // refresher's 15 s debounce absorbs a fetch that has only just happened.
    private fun startLoop() {
        if (loop?.isActive == true) return
        loop = scope.launch {
            while (true) {
                Refresher.refresh(this@CommuteService, force = false)
                val now = System.currentTimeMillis() / 1000
                val wait = commuteDelay(Store(this@CommuteService).cache().watch, now) ?: break
                delay(wait * 1000)
            }
        }
    }

    private fun stopLoop() {
        loop?.cancel()
        loop = null
    }

    // A day at home: no window opens again until local midnight.
    private suspend fun notToday() {
        Store(this).setQuietUntil(endOfLocalDay(System.currentTimeMillis() / 1000))
        stopLoop()
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
        CommuteAlarm.arm(this)
        RefreshWorker.now(this) // the window's place gives way to home
    }

    override fun onDestroy() {
        running = false
        unregisterReceiver(screen)
        scope.cancel()
        super.onDestroy()
    }

    companion object {
        private const val NOTICE_ID = 1
        private const val ALERT_ID = 2
        private const val CHANNEL = "commute"
        private const val ALERTS = "commute_alerts"
        private const val ACTION_NOT_TODAY = "io.github.msmgreen.nextdeparture.NOT_TODAY"

        // This process's service, so the notification is only redrawn while
        // commute mode is on.
        @Volatile var running = false
            private set

        fun start(context: Context) {
            val intent = Intent(context, CommuteService::class.java)
            try {
                context.startForegroundService(intent)
            } catch (e: ForegroundServiceStartNotAllowedException) {
                // Both of today's exemptions gone (widget spec 9): ask for a
                // tap, which is a user-initiated start.
                channels(context)
                val tap = PendingIntent.getForegroundService(context, 0, intent, PendingIntent.FLAG_IMMUTABLE)
                val alert = Notification.Builder(context, ALERTS)
                    .setSmallIcon(R.drawable.ic_commute)
                    .setContentTitle(context.getString(R.string.commute_not_started))
                    .setContentIntent(tap)
                    .setAutoCancel(true)
                    .build()
                context.getSystemService(NotificationManager::class.java).notify(ALERT_ID, alert)
            }
        }

        fun stop(context: Context) {
            context.stopService(Intent(context, CommuteService::class.java))
        }

        // Redraws the notification from the stored cache, as renderAll redraws
        // the widget; core's commuteNotice chooses every word.
        suspend fun update(context: Context) {
            if (!running) return
            val store = Store(context)
            val now = System.currentTimeMillis() / 1000
            val place = store.place(now) ?: return
            val watch = store.cache().watch?.takeIf { it.key == place.watch.key }
            val h24 = DateFormat.is24HourFormat(context)
            val notice = commuteNotice(widgetView(place, watch, now, h24), watch?.lastOk ?: 0, now, h24)
            context.getSystemService(NotificationManager::class.java).notify(NOTICE_ID, build(context, notice))
        }

        // Spec 11's setup check: notifications allowed, and neither channel off.
        fun notificationsOn(context: Context): Boolean {
            channels(context)
            val nm = context.getSystemService(NotificationManager::class.java)
            return nm.areNotificationsEnabled() &&
                listOf(CHANNEL, ALERTS).all { nm.getNotificationChannel(it)?.importance != NotificationManager.IMPORTANCE_NONE }
        }

        private fun channels(context: Context) {
            val nm = context.getSystemService(NotificationManager::class.java)
            val quiet = NotificationChannel(CHANNEL, context.getString(R.string.channel_commute), NotificationManager.IMPORTANCE_LOW)
            quiet.setSound(null, null)
            quiet.enableVibration(false)
            quiet.setShowBadge(false)
            nm.createNotificationChannel(quiet)
            nm.createNotificationChannel(
                NotificationChannel(ALERTS, context.getString(R.string.channel_commute_alerts), NotificationManager.IMPORTANCE_DEFAULT),
            )
        }

        private fun build(context: Context, n: CommuteNotice): Notification {
            channels(context)
            val settings = PendingIntent.getActivity(
                context, 0, Intent(context, SettingsActivity::class.java), PendingIntent.FLAG_IMMUTABLE,
            )
            val notToday = PendingIntent.getService(
                context, 1, Intent(context, CommuteService::class.java).setAction(ACTION_NOT_TODAY), PendingIntent.FLAG_IMMUTABLE,
            )
            val b = Notification.Builder(context, CHANNEL)
                .setSmallIcon(R.drawable.ic_commute)
                .setContentTitle(n.title)
                .setContentText(n.text)
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .setCategory(Notification.CATEGORY_STATUS)
                // It does the widget's job on the lock screen (widget spec 9).
                .setVisibility(Notification.VISIBILITY_PUBLIC)
                .setContentIntent(settings)
                .addAction(action(context, R.string.action_refresh, DepartureWidget.refreshIntent(context)))
                .addAction(action(context, R.string.action_not_today, notToday))
            val to = n.countdownTo
            if (to == null) {
                b.setShowWhen(false)
            } else {
                b.setWhen(to * 1000).setShowWhen(true).setUsesChronometer(true).setChronometerCountDown(true)
            }
            return b.build()
        }

        private fun action(context: Context, title: Int, pi: PendingIntent) =
            Notification.Action.Builder(Icon.createWithResource(context, R.drawable.ic_commute), context.getString(title), pi).build()
    }
}
```

`$RES/values/strings_commute.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<resources>
    <string name="commute_starting">Commute mode</string>
    <string name="commute_not_started">Commute mode didn\'t start — tap to start it</string>
    <string name="channel_commute">Commute</string>
    <string name="channel_commute_alerts">Commute alerts</string>
    <string name="action_refresh">Refresh</string>
    <string name="action_not_today">Not today</string>
</resources>
```

`$RES/drawable/ic_commute.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<!-- The commute notification's small icon: Material's directions_bus. -->
<vector xmlns:android="http://schemas.android.com/apk/res/android"
    android:width="24dp"
    android:height="24dp"
    android:viewportWidth="24"
    android:viewportHeight="24">
    <path
        android:fillColor="#FFFFFFFF"
        android:pathData="M4,16c0,0.88 0.39,1.67 1,2.22V20c0,0.55 0.45,1 1,1h1c0.55,0 1,-0.45 1,-1v-1h8v1c0,0.55 0.45,1 1,1h1c0.55,0 1,-0.45 1,-1v-1.78c0.61,-0.55 1,-1.34 1,-2.22V6c0,-3.5 -3.58,-4 -8,-4s-8,0.5 -8,4v10zM7.5,17c-0.83,0 -1.5,-0.67 -1.5,-1.5S6.67,14 7.5,14s1.5,0.67 1.5,1.5S8.33,17 7.5,17zM16.5,17c-0.83,0 -1.5,-0.67 -1.5,-1.5s0.67,-1.5 1.5,-1.5 1.5,0.67 1.5,1.5 -0.67,1.5 -1.5,1.5zM18,11H6V6h12v5z" />
</vector>
```

- [ ] **Step 3: Wire it in**

In `$APP/DepartureWidget.kt`'s companion, above `show`, so the notification's
**Refresh** is the widget's tap:

```kotlin
        // A tap's refresh, for the commute notification's Refresh action too.
        fun refreshIntent(context: Context): PendingIntent = broadcast(context, ACTION_REFRESH)
```

In `$APP/Refresher.kt`, after `DepartureWidget.renderAll(context)`:

```kotlin
        CommuteService.update(context)
```

In `$APP/RefreshWorker.kt`'s `doWork`, after the refresh:

```kotlin
        // Re-armed on every run, so a boundary alarm lost to a force-stop or a
        // cleared alarm heals within 30 minutes.
        CommuteAlarm.arm(applicationContext)
```

Replace `$APP/BootReceiver.kt` with:

```kotlin
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
```

Replace `android/app/src/main/AndroidManifest.xml` with:

```xml
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android">

    <uses-permission android:name="android.permission.INTERNET" />
    <uses-permission android:name="android.permission.RECEIVE_BOOT_COMPLETED" />
    <!-- Re-render the widget on time (widget spec 6). The user grants it, under
         Alarms & reminders; without it the render alarm is inexact. -->
    <uses-permission android:name="android.permission.SCHEDULE_EXACT_ALARM" />
    <!-- Commute mode (widget spec 9, 11). -->
    <uses-permission android:name="android.permission.FOREGROUND_SERVICE" />
    <uses-permission android:name="android.permission.FOREGROUND_SERVICE_SPECIAL_USE" />
    <uses-permission android:name="android.permission.POST_NOTIFICATIONS" />

    <application
        android:allowBackup="true"
        android:icon="@mipmap/ic_launcher"
        android:label="@string/app_name"
        android:roundIcon="@mipmap/ic_launcher_round"
        android:supportsRtl="true"
        android:theme="@style/Theme.Nextdeparture">
        <!-- The template's activity, renamed, gains the configure action. -->
        <activity
            android:name=".SettingsActivity"
            android:exported="true"
            android:windowSoftInputMode="adjustResize">
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
                <action android:name="android.intent.action.MY_PACKAGE_REPLACED" />
            </intent-filter>
        </receiver>

        <!-- A commute window's start or end, from the one boundary alarm. -->
        <receiver
            android:name=".BoundaryReceiver"
            android:exported="false" />

        <!-- specialUse: no other type fits a service that keeps a process alive
             to hear the screen turn on. Android 14+ requires a type. -->
        <service
            android:name=".CommuteService"
            android:exported="false"
            android:foregroundServiceType="specialUse">
            <property
                android:name="android.app.PROPERTY_SPECIAL_USE_FGS_SUBTYPE"
                android:value="Refreshes a transit departure while the screen is on during configured commute windows" />
        </service>
    </application>

</manifest>
```

- [ ] **Step 4: Build and lint**

Run: `./gradlew :core:test :app:assembleDebug :app:lintDebug`
Expected: `BUILD SUCCESSFUL`, 124 core tests. Lint reports no errors; its
warnings are phase 1's (`OldTargetApi`, `DataExtractionRules`,
`ObsoleteSdkInt`, `ContentDescription`).

- [ ] **Step 5: Commit**

```bash
git add android/app
git commit -m "Add commute mode: a boundary alarm, a service, a notification

One exact alarm at the next window start or end starts or stops a
specialUse foreground service. It hears the screen turn on and off and
fetches every 30 s while it is on, holding no wake lock, and its
notification does the widget's job on the lock screen."
```

---

### Task 6: Places and windows in settings

**Files:**
- Modify: `$APP/SettingsActivity.kt` (replace)

**Interfaces:**
- Consumes: `CommuteWindow`, `WEEKDAYS`, `MAX_PLACES`, `MAX_WINDOWS`, `placeAfterRemoval` (Task 1); `CommuteAlarm.arm`, `CommuteService.notificationsOn` (Task 5)
- Produces: the phone's places and windows; the end of phase 2's code

- [ ] **Step 1: Replace `SettingsActivity.kt`**

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
```

Each place keeps phase 1's fields and **Check** buttons. A window is seven day
chips, a from and a to time, and the place it shows, which a tap moves to the
next place. **Save** validates the whole config with `ConfigCodec.validate`,
then refreshes and re-arms the boundary alarm.

- [ ] **Step 2: Build**

Run: `./gradlew :core:test :app:assembleDebug`
Expected: `BUILD SUCCESSFUL`, 124 core tests.

- [ ] **Step 3: Commit**

```bash
git add android/app
git commit -m "Set places and commute windows on the settings screen

Removing a place keeps each window on its own place, or moves it to the
first. Saving re-arms the boundary alarm, and the setup check gains the
notifications commute mode needs."
```

---

### Task 7: On the phone — **PHONE**

Spec §13's commute checks, by hand. Windows are tested by setting one to start
two minutes ahead. Watch with `adb logcat -s NextDeparture`; every request logs
one `GET` line.

- [ ] **Step 1: Install and set up**

`./gradlew :app:installDebug`. In settings, keep the phase 1 place and add a
second (say `Work`, with its own stop and toward stop, both **Check**ed). Add a
window starting two minutes from now and ending five minutes later, showing
the second place. **Save**. Expected: `Saved`. In the setup check, every row is
✓; **Fix** the notifications row if it is not.

- [ ] **Step 2: Run the checks**

1. **The window starts on time** (spec check 4): the notification appears
   within a few seconds of the start, the widget switches to the second place,
   and the notification's countdown ticks.
2. **Screen on fetches** (check 5): turn the screen off and on. Expected: a
   `GET …tripupdates` within about 2 s. Leave the screen on: one every 30 s.
   Screen off for a minute: no requests at all.
3. **The window ends on time** (check 4): the notification goes, and the widget
   returns to the first place after one fetch.
4. **Reboot inside a window** (check 6): set a fresh window, restart the phone
   inside it. Expected: commute mode comes back.
5. **Not today** (check 7): inside a window, tap **Not today**. Expected: the
   notification goes, the widget shows the first place, and no window starts
   again today. Tomorrow's does.
6. **Removing a window while it runs** (Review Focus 1): inside a window, remove
   it and **Save**. Expected: the notification goes at once.
7. **Overnight** (check 10): set the real morning window and leave the phone
   overnight. Expected: the window starts on time.
8. **Without Alarms & reminders** (check 12): revoke it. Expected: the setup
   check shows ✗, and a window still starts, perhaps ten minutes late.

Note anything that differs, and whether `Commute mode didn't start` was ever
posted.

- [ ] **Step 3: Set the real windows**

Set each phone's real windows and places, and **Save**.

---

### Task 8: Write down how it landed

- [ ] **Step 1: Record it**

Add a `## How it landed` section to the end of this plan: what Task 7 found,
any change made to pass it, and the windows each phone ended up with in words
(not times or places).

- [ ] **Step 2: Commit**

```bash
git add docs/design/plans/2026-10-19-android-widget-phase-2.md
git commit -m "Record how the widget's commute mode landed"
```
