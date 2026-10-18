# Android widget: the next departure you can catch

Date: 2026-10-18
Status: designed, not built
Relates to: board spec §3a (direction), §5 (data flow), §6 (stop resolution),
§8 (degradation); config portal spec §2, §8 (secrets stay compiled in);
[`../../at-api-notes.md`](../../at-api-notes.md)

## 1. What this covers

A home-screen widget for two Android phones (a Galaxy S23 Ultra and an S24
Ultra) that shows **one departure: the next one you can still catch** from
wherever you are, using the same AT key as the board. It ships with **commute
mode**, which refreshes far more often during configured windows (say weekdays
07:30–08:30 and 16:00–18:00), and **places**, which decide whether "where you
are" is home or work.

| In | Out (deferred, see §15) |
|---|---|
| One-departure widget with a leave-by countdown | More than one departure on the widget |
| Places (home, work), each with one watch and a walk time | Nearest stop anywhere, by GPS |
| Commute windows with a foreground "commute mode" | Live Updates / Now Bar promotion |
| Geofences deciding the current place, with a manual flip | Leave-now alert |
| Settings screen per phone | Play Store distribution |
| A Kotlin port of `lib/core`'s data rules, tested on the board's fixtures | Wear OS, tablets, lock-screen widgets |

It is not a port of the board. Nothing below `lib/core/src/model.h` crosses
over: no lanes, sprites, themes or rendering.

## 2. Decisions, and why

**Everything runs on the phone.** No server, no proxy. Two phones do not need a
shared cache, and anything hosted is something to keep running for a commute.

**Not the board as a data source.** The board already has the data and a LAN
web server, but it is only reachable at home — the widget has to work at work,
which is half the point.

**Rewrite `lib/core`'s rules in Kotlin rather than call the C++ through the
NDK.** The part worth keeping is about 550 lines (`at_api.cpp`, `live.cpp`,
`freshness.cpp`). A JNI bridge, an NDK toolchain in Gradle and marshalling
fixed-size `char` arrays cost more than rewriting them, and the C++ test suites
then become the Kotlin port's conformance spec (§13) — a second, independent
implementation checked against the same captured responses.

**Classic `RemoteViews`, not Jetpack Glance.** The widget's one moving part is
a countdown that ticks with no app process running, and that is a
`Chronometer` in countdown mode (API 24+). Glance has no chronometer; embedding
`RemoteViews` inside Glance to get one leaves Glance doing nothing a 4×1 layout
needs.

**The key is compiled in.** `AT_API_KEY` comes from `android/local.properties`
(gitignored) into `BuildConfig`, mirroring `src/secrets.h`. It is not in the
settings screen, for the reason config portal spec §2 and §8 give for the
board: a screen that can hold the key can disclose it. It is extractable from
the APK, which goes to two phones (§14, risk 7).

**Home and work never reach the repository.** This repository is public.
Places and their coordinates live only in each phone's settings, never in
source, which is also why configuration is a settings screen rather than
compiled defaults like `src/watch_config.h`.

## 3. Configuration

Each phone holds its own configuration; the two phones will usually share a
home and not a workplace.

**Place** — up to four, matching `MAX_WATCHES`:

| Field | Meaning |
|---|---|
| `name` | Shown on the widget, e.g. `Home` (12 characters) |
| `lat`, `lon` | Set with "use my current location" while standing there, or typed |
| `radius_m` | Geofence radius, default 200, minimum 100 (§7) |
| `watch` | Exactly the board's `WatchConfig`: `label`, `stop_code`, `route_short_name` (`""` = any route), `toward_stop_code` |
| `walk_min` | Minutes from this place to the stop, 0–30 |

The watch keeps the board's encoding unchanged — the stop you travel *toward*,
never a direction (board spec §3a). A home watch points toward the work stop;
the work watch points toward the home stop. Direction is derived at every
schedule refresh, which is what survived the CRL direction flip.

**Commute window** — up to four:

| Field | Meaning |
|---|---|
| `days` | Day-of-week mask, default Monday–Friday |
| `start`, `end` | Local `Pacific/Auckland` wall-clock times, `start < end`. A window may not cross midnight |
| `default_place` | The place to show in this window when location gives no answer (§7) |

**Storage.** One JSON document in DataStore with a `"v": 1` field, as the board
stores its config in NVS (config portal spec §4). Validation reuses the
board's rules from `lib/core/src/config_schema.cpp`: alphanumeric stop codes,
the same length limits, an empty route accepted as "any route". A stop code
and a toward stop are each **checked against AT** before saving, showing the
stop's name, as the portal's Check button does.

## 4. The data path

The port, module by module:

| `lib/core/src/` | `android/core/` (Kotlin) | Notes |
|---|---|---|
| `at_api.{h,cpp}` | `AtApi.kt`, `Dto.kt` | URL builders and parsers. `stop_time_update` is one object, not an array; `header.timestamp` is a float |
| `live.{h,cpp}` | `Live.kt` | `tripServes`, `collectRouteDirs`, `selectServingRows`, `verdictForPairs`, `applyRealtime`, `carryRealtime`, `realtimeIds`, `scheduleWindows` — same rules, same names |
| `freshness.{h,cpp}` | `Freshness.kt` | The rule is re-derived for a widget (§10) |
| `nztime.{h,cpp}` | `java.time`, `ZoneId.of("Pacific/Auckland")` | Keep `gtfs_epoch`'s rule: service-day noon minus 12 h, plus the departure seconds. It is what makes DST days and `25:10:00` correct |
| `color.{h,cpp}` | `RouteColor.kt` | `route_color` when present, `#000000` treated as absent, fallback by `route_type` |
| `config_schema.{h,cpp}` | `Config.kt` | §3 |
| `dechunk`, `layout`, `model`, `theme`, `shapes`, `demo`, `rng`, `scenes_data`, `sprite_ref` | — | Not ported. OkHttp handles chunked bodies; there is no board to draw |
| `src/fetcher.cpp` | `Repository.kt` (app) | Orchestration, caching, backoff |

Cadences, as the board's (board spec §5), with the corrections in
`at-api-notes.md` taking precedence over the spec where they disagree:

- **Stop resolution:** once, then cached until `GET /stops/{id}` answers 404
  (board spec §6). A `stoptrips` 404 means an empty window and **never**
  re-resolves.
- **Schedule:** `stoptrips` over the next three hours via `scheduleWindows` —
  one request from 04:00, two in the small hours, and `start_hour` never 0. It
  is refreshed when it is older than 15 minutes at the moment a fetch runs, and
  on a service-date rollover. Every `(route_id, direction_id)` pair present is
  checked with `trips/{id}/stops` against the toward stop, per route, never one
  trip per direction.
- **Realtime:** one `/realtime/legacy/tripupdates?tripid=…` request per fetch,
  for the watch's next six rows from `realtimeIds`. Returned entities are
  re-filtered against the ids requested; a requested id with no entity has its
  realtime cleared, never kept as live (`apply_realtime`).

All of this state — resolved ids, the schedule rows, the latest realtime, the
time of the last good fetch — is **persisted**, because the process is killed
between almost every run. Any entry point (widget tap, worker, alarm, service,
geofence receiver) can re-render from disk without touching the network.

Only the **current place's** watch is fetched. The other places are fetched
when they become current.

## 5. Choosing the departure

From the current place's rows, in scheduled order:

```
predicted = sched + (has_rt ? delay : 0)
leave_by  = predicted - walk_min * 60
catchable = !cancelled && leave_by >= now
```

The widget shows the **first catchable row**. Negative delays are real (−427 s
was observed, board spec §5): a bus running early moves the leave-by time
earlier, and the widget must follow it rather than the timetable.

Cancellations are surfaced, never dropped (board spec §8): if the first row that
would otherwise have been catchable is cancelled, the widget shows the next
catchable row with a second line `7:36 cancelled`.

A row with no realtime is shown with its scheduled time and marked `sched`.
That is normal before a trip is under way — realtime only reports trips in
progress (`at-api-notes.md`).

If nothing in the three-hour window is catchable, the widget says
`No 20 toward Wynyard Quarter in the next 3 h` (the label and route come from
the watch). That is the normal empty state, not an error.

## 6. The widget

A 4×1 widget, resizable down to 2×1. At 4×1:

```
Home · 20 → Wynyard Quarter                as of 7:31
Leave in 04:12            7:42 · 2 min late · live
```

At 2×1 it keeps the route and the countdown only.

- **The countdown counts to `leave_by`, not to the departure.** It is a
  `Chronometer` in countdown mode whose base is `leave_by` in the
  `elapsedRealtime` timebase, so it ticks with no process running. Seconds are
  shown, and that precision is the chronometer's, not the data's.
- **The widget re-renders locally** (no network) at `leave_by`, so the next
  catchable departure takes over, and at the moment the data turns stale
  (§10), so "live" never outlasts its evidence. These are inexact alarms: they
  are deferred while the phone dozes and delivered when it wakes, which is the
  only time anyone looks. If one is late, the chronometer runs past zero and
  shows `−0:30`, which reads correctly as "you are 30 s late to leave".

Tap targets:

| Target | Action |
|---|---|
| The body | Refresh now (§8), serialized with every other fetch |
| The place name | Flip to the next place, as a manual override (§7) |
| Widget reconfigure (long-press), or the app icon | Open settings |

Several instances of the widget on one phone all show the same thing.

## 7. Where am I

The **current place** is decided in this order:

1. **Manual override**, set by tapping the place name. It lasts until the next
   geofence entry or the next commute window start, whichever comes first.
2. **The place most recently entered**, from geofences. Leaving a place changes
   nothing: after you leave home in the morning you are walking to the home
   stop, so the home departure is still the right one. Arriving at work flips
   it; leaving work keeps it.
3. **The current window's `default_place`**, when location is unavailable
   (permission not granted, location off, nothing entered yet).
4. **The first place**, otherwise.

This makes location an improvement, not a dependency: with no location
permission at all, windows alone switch home and work by time of day.

**Geofences.** Play services `GeofencingClient`, one geofence per place,
transition `ENTER` only, with `INITIAL_TRIGGER_ENTER` so that registering while
already inside counts. Notification responsiveness is two minutes — a commute
does not need faster, and the battery cost of faster is real. An entry stores
the place with a timestamp and enqueues a refresh.

Geofences are wiped on reboot, on app update and when location is toggled, and
Android does not reliably say when. So they are **re-registered on every
periodic run** (§8), on boot, on `MY_PACKAGE_REPLACED` and on every settings
save. Registration with the same request ids replaces rather than duplicates,
so this is idempotent.

Location never leaves the phone. Only stop codes and trip ids go to AT.

## 8. Refresh, outside commute mode

| Trigger | What runs |
|---|---|
| Periodic `WorkManager` job, every 30 minutes, network required | Fetch, re-register geofences |
| Widget tap | Expedited one-off fetch |
| Geofence entry | Expedited one-off fetch for the new place |
| `leave_by` and the stale threshold | Local re-render, no network |
| Boot, app update, settings save | Re-register geofences; schedule the next commute boundary (§9); one fetch |

Every fetch goes through one `Mutex` in `Repository`, so a tap during a
periodic run does not produce two requests. A fetch that is not a tap is
skipped if the last good one for the same place finished under 15 seconds ago.

Outside commute mode the widget is therefore usually stale, and says so (§10).
That is correct: nobody is about to leave.

## 9. Commute mode

Inside a commute window the phone refreshes **whenever the screen is on**, and
not at all while it is off.

**Boundaries.** One exact alarm is armed at a time, at the next window start or
end (`setExactAndAllowWhileIdle`, under `SCHEDULE_EXACT_ALARM`). When it fires
it starts or stops the service, then arms the next boundary. On boot, update
or settings save, the app never starts the service directly: it arms the alarm,
for now if already inside a window. Window arithmetic is a pure function in
`core`, in local time, tested across both DST changes (§13).

**The service.** A foreground service of type `specialUse` (Android 14+
requires a type; `specialUse` carries a
`PROPERTY_SPECIAL_USE_FGS_SUBTYPE` description and is only reviewed on the Play
Store). It holds no wake lock. Its job is to keep a process alive that has
registered, at runtime, for `ACTION_SCREEN_ON` and `ACTION_SCREEN_OFF` —
broadcasts a manifest receiver cannot get.

| Event | Commute mode does |
|---|---|
| Window starts | Start the service, post the notification, fetch once |
| Screen on | Fetch immediately, then every 30 s (`RT_FAST_S`) while the screen stays on |
| Screen off | Stop the loop. Nothing runs until the screen is next on |
| 401 | Stop the loop; show `Check API key` until the next window |
| 429 / 5xx | Back off exponentially to a 5-minute cap, keep last good data (board spec §8) |
| Window ends | Stop the service; the widget returns to §8 |

Screen-on is the trigger rather than unlock because the notification is already
readable on the lock screen. The screen turning on also brings the phone out of
Doze, so the network is available for that first fetch without any exemption.

**The notification** is required for a foreground service, so it does the
widget's job on the lock screen. Channel `Commute`, low importance, silent,
ongoing:

- Title: `Leave in 4 min · 20 → Wynyard Quarter`
- Text: `7:42 · 2 min late · live as of 7:31`
- A countdown to `leave_by`, via `setUsesChronometer` and
  `setChronometerCountDown`
- Actions: **Refresh**, **Switch place**, **Not today**

**Not today** stops the service and suppresses every window until local
midnight — a day working from home.

**If the service cannot start.** Starting a foreground service from the
background is allowed here twice over: from an exact-alarm callback, and for an
app exempt from battery optimization (§11). If a future Android revokes both
and `startForegroundService` throws, the app posts a plain notification,
`Commute mode didn't start — tap to start it`, and a tap is a user-initiated
start.

## 10. Freshness

The board's rule is `stale_s = now − last_ok − poll_interval_s`, stale when it
exceeds `STALE_AFTER_S` = 90 (`lib/core/src/model.h`, `freshness.h`). A widget
does not poll; its only honest interval is commute mode's 30 s. So:

**The widget is stale when `now − last_ok > 30 + 90`, i.e. after two minutes.**

`last_ok` is the end of the last fetch whose realtime request answered 200
**and** whose schedule is less than 15 minutes old — the widget is only as
fresh as its schedule, as `board_last_ok` holds for the board.

| Condition | Widget shows |
|---|---|
| Fresh, row has realtime | `live`, normal colours |
| Fresh, row has no realtime | `sched`, normal colours |
| Stale | Last good data kept, dimmed, `as of 7:31`. Never `live` |
| No network | As stale. Never blank |
| Cancelled | §5: promote the next catchable, say which one was cancelled |
| 401 | `Check API key` |
| 429 / 5xx | Backoff (§9), last good data stays up, ageing into stale |
| `stoptrips` 404 or empty | §5's empty state. Not an error, never a re-resolve |
| `/stops/{id}` 404 | Re-resolve from `stop_code` |
| `filter[stop_code]` 400s | `Stop lookup gone`: the undocumented filter was withdrawn (board spec §8) |
| No direction reaches the toward stop | `Check settings` (`verdict_for_pairs` → `CheckConfig`) |
| Location unavailable | The place name is followed by `(by time)` when chosen by §7 rule 3 |

## 11. Android and Samsung specifics

`minSdk` 34 (Android 14), `targetSdk` 36 (Android 16). Both phones are well
past the minimum.

Permissions:

| Permission | For |
|---|---|
| `INTERNET` | AT |
| `ACCESS_FINE_LOCATION`, `ACCESS_BACKGROUND_LOCATION` | Geofences require both; the second is "Allow all the time" |
| `POST_NOTIFICATIONS` | Commute notification |
| `SCHEDULE_EXACT_ALARM` | Window boundaries. Granted by the user under *Alarms & reminders*. Without it, boundaries fall back to `setWindow` with a 5-minute window and the setup check says so |
| `FOREGROUND_SERVICE`, `FOREGROUND_SERVICE_SPECIAL_USE` | Commute mode |
| `RECEIVE_BOOT_COMPLETED` | Re-arm after reboot |

**One UI will put this app to sleep.** A widget-only app that is never opened
is exactly what *Put unused apps to sleep* targets, and a sleeping app's alarms,
jobs and geofences stop firing — the widget would go quietly stale with no
error. The app's Battery setting must be **Unrestricted**, and it must not be
on the *Sleeping apps* or *Deep sleeping apps* lists.

So the settings screen has a **setup check** that reads each requirement back
from the system and links to the screen that fixes it:

- battery optimization exempt (`PowerManager.isIgnoringBatteryOptimizations`)
- `canScheduleExactAlarms()`
- notifications enabled for both channels
- background location granted, geofences registered
- the API key present in the build

## 12. Repository and build

```
android/
  settings.gradle.kts
  core/           pure Kotlin/JVM, no Android imports — mirrors lib/core
  app/            widget, service, workers, geofences, settings (Compose)
  local.properties   gitignored: sdk.dir, AT_API_KEY
```

Libraries: OkHttp, kotlinx.serialization, WorkManager, DataStore, Play services
location, Jetpack Compose for settings only.

`core` has no Android dependency, so its tests run on a plain JVM and read
`../test/fixtures/` directly — the same files the C++ suites use. CI gains a
job running `./gradlew :core:test` under the existing 15-minute cap; GitHub's
Ubuntu runners ship an Android SDK, which the Gradle configuration of `app`
needs even when only `core` is tested.

`.gitignore` gains `android/local.properties`, `android/.gradle/`,
`android/**/build/` and `*.jks`.

**Distribution.** Built in Android Studio and installed over USB (`adb`) on
both phones. One release keystore, kept outside the repository and backed up:
an update signed with a different key will not install over the old one, and
uninstalling loses the settings. Google's developer verification for sideloaded
apps is not enforced in New Zealand yet (the first regions are from
30 September 2026; global is planned for 2027), `adb` installs are unaffected,
and a free limited-distribution account covers up to 20 devices if it ever
matters.

## 13. Testing

### `core`, on the JVM, written first

The C++ suites in `test/test_at_api`, `test/test_live` and
`test/test_freshness` are ported case for case: the Kotlin rules must agree with
the C++ rules on every one. Then, on the fixtures:

- `stops_unknown.json` (200, `{"data":[]}`) resolves to nothing → `Check settings`
- `stoptrips_kingsland_after_midnight.json`: `24:04:00` on service date
  2026-09-19 is 00:04 on the 20th, NZST
- `stoptrips_start_hour_0.json`: the 400 body parses as an error, and
  `scheduleWindows` never asks for hour 0
- `realtime_tripupdates_only.json`: `stop_time_update` as a single object, the
  float timestamp, and an entity for a trip not requested is discarded
- `trip_dir1_stops.json`: a station target matches through `parent_station`

New rules, new tests:

- Catchable: `leave_by` exactly `now` is catchable, a second earlier is not; an
  early-running bus moves `leave_by` earlier; a cancelled row is skipped and
  reported; a row without realtime uses its schedule
- Windows: the day mask; `[start, end)` boundaries; the next boundary across a
  weekend; NZDT starting 27 September 2026 and ending 4 April 2027; `Not today`
  suppressing until local midnight and no longer
- Current place: every rule in §7 and the order between them, including the
  override expiring at the next entry and at the next window start
- Freshness: stale at 121 s and not at 120 s; a fresh realtime 200 on top of a
  16-minute-old schedule is still stale

### On the phones, by hand

Windows are tested by setting one to start two minutes ahead.

1. Adding the widget populates it; a tap refreshes it within about 2 s
2. The countdown reaches zero and the next departure takes over, offline
3. Airplane mode: the last data stays, dims after two minutes, shows `as of`
4. A window start posts the notification on time; its end removes it
5. In a window, screen on → a fetch within about 2 s; screen held on → every
   30 s; screen off → no requests at all (from the app's own fetch log)
6. Reboot inside a window: commute mode comes back
7. `Not today` stops it, and it does not return until tomorrow's window
8. Walking out of home and to the stop keeps `Home`; arriving at work flips to
   `Work` within a few minutes
9. A manual flip holds until the next arrival or window start
10. Left idle overnight, the next morning's window still starts on time — the
    test that One UI's sleeping-apps and Doze have not won
11. After a week, the app's battery use is negligible in *Battery* settings
12. Revoking *Alarms & reminders* falls back to inexact boundaries, and the
    setup check says so

## 14. Risks

| # | Risk | Mitigation |
|---|---|---|
| 1 | **One UI sleeps or kills the app**, and the widget goes stale silently | Unrestricted battery, enforced by the setup check (§11); on-device test 10 |
| 2 | **Exact-alarm permission** missing or revoked | Fall back to inexact boundaries; flagged in the setup check |
| 3 | **Background foreground-service starts** tightened by a future Android | Two independent exemptions today; the tap-to-start notification if both go (§9) |
| 4 | **Geofences miss a transition** or lag it | Manual flip; window default place; re-registration on every periodic run (§7) |
| 5 | **The chronometer** shows seconds and runs negative | Re-render at `leave_by`; negative reads correctly as "late to leave" (§6) |
| 6 | **`filter[stop_code]` withdrawn** by AT | Same as the board: say `Stop lookup gone` (board spec §8) |
| 7 | **The key is extractable from the APK.** It is the board's key, so rotating it means reflashing the board too | Two phones only; a second free key for the phones would decouple them if it ever leaks |
| 8 | **Developer verification** reaches New Zealand | `adb` installs are unaffected; a limited-distribution account if not (§12) |
| 9 | **A long screen-on session** in a window | At most 120 realtime requests an hour, while someone is looking at the phone (Appendix) |

## 15. Deferred

- **Live Updates.** Android 16's promoted ongoing notifications, which One UI 8
  shows in the Now Bar. The commute notification is already the right shape;
  promotion adds `POST_PROMOTED_NOTIFICATIONS` and `setRequestPromotedOngoing`.
- **Leave-now alert.** An exact alarm a minute before `leave_by`, a fetch, and a
  high-importance notification if the departure is still catchable. Opt-in per
  window, once per window.
- **Starting a window early** on leaving a place.
- **Several watches per place** — bus or train, whichever is catchable first.
- **Nearest stop anywhere**, by GPS against `/stops`.
- Public holidays; Wear OS; Play Store distribution.

## Phasing

1. **The widget.** `core` port and its tests, one place from settings, walk
   time, the catchable rule, periodic and tap refresh, freshness, the setup
   check, the CI job.
2. **Commute mode.** Windows, boundary alarms, the service, screen-on refresh,
   the notification, `Not today`. Windows' default places switch home and work
   by time of day from here on.
3. **Places by location.** Geofences, the most-recently-entered rule, the manual
   flip.
4. **Deferred items**, as wanted.

## Appendix: API budget

Per phone per day, against the board's own budget of about 130 requests an hour
(board spec §5), roughly 3,100 a day:

| Source | Requests/day |
|---|---|
| Periodic runs: 48 × (realtime 1 + schedule refresh ~3) | ~190 |
| Commute windows, 3 h: schedule refreshes, 12 × ~3 | ~36 |
| Commute windows: ~20 screen-ons plus ~15 min of screen time at 30 s | ~50 |
| Taps, geofence entries | ~10 |
| **Total** | **~290** |

Both phones together come to under a fifth of what the board alone uses. The
schedule refresh's "~3" is one or two `stoptrips` plus one `trips/{id}/stops`
per `(route, direction)` pair; a busy station with several lines costs more.
