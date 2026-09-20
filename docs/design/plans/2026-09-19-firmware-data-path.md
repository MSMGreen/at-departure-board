# Firmware Data Path Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The board shows real Auckland Transport departures for the two
configured watches — schedule merged with live delays, correct at 1am and
across the DST change, honest when it cannot know.

**Architecture:** All decision logic is pure and natively tested in
`lib/core/`: NZ time arithmetic, a de-chunker, the AT response parsers and URL
builders, and the merge that turns a snapshot plus "now" into the `Board` the
renderer already draws. The device side is thin: one HTTPS client and one
FreeRTOS fetch task pinned to core 0, publishing a mutex-guarded snapshot. The
Arduino loop keeps drawing at 15 fps and never blocks on the network: it
rebuilds the `Board` from the snapshot and the current time every frame, which
is what makes the countdown tick smoothly between polls.

**Tech Stack:** PlatformIO, `espressif32@7.1.3` (Arduino core 2.0.17),
TFT_eSPI 2.5.43, ArduinoJson 7.4.3 (native and device), Unity, pytest.

**Spec:** `docs/design/specs/2026-09-06-at-departure-board-design.md`
(§3a derived direction, §5 data flow and time handling, §6 stop resolution,
§8 degradation). `docs/at-api-notes.md` is the verified record of how the API
actually behaves and outranks the spec where they differ.

## Scope

Second of three firmware plans. The display plan
(`2026-09-19-firmware-display.md`) is merged.

**In scope:** WiFi, NTP, TLS with a pinned root CA, stop/route resolution,
schedule fetch, derived direction, realtime delays, the error and stale
surfaces, and a live frame loop.

**Not in scope, deliberately:**
- The captive portal and NVS config (plan 3). Until then WiFi credentials and
  the API key come from a git-ignored `src/secrets.h`, and the watch list from
  a committed `src/watch_config.h`. The spec's "no secrets in the repo, no
  recompile to change a stop" is only met when plan 3 lands.
- Brightness that follows use, and quiet hours (plan 3). `dimmed` stays false.
- Caching resolved stop ids in NVS (plan 3). They are resolved once per boot,
  four requests, and held in RAM.
- Spec §7's "next service's day and time in text" for the empty state. It needs
  a search beyond the fetch window; the board keeps showing `none tonight`.

## Global Constraints

- **The Python simulator is the source of truth for anything the screen
  shows.** A new display field is added to `tools/board/model.py` and
  `render.py` with goldens first, then regenerated into C. The firmware never
  grows a display concept the simulator does not have.
- **Never block the render loop on the network.** Fetching runs in a FreeRTOS
  task pinned to core 0; the Arduino loop only reads a published snapshot.
- **Never show a time the board cannot prove.** A watch whose direction cannot
  be derived shows `check config` and no times (spec §3a).
- **A `stoptrips` 404 is an empty window, not an error** (`docs/at-api-notes.md`).
  Only a 404 from `/stops/{id}` would invalidate a stop id, and this plan never
  calls that endpoint.
- **`filter[start_hour]` must be 1..23.** `start_hour=0` returns
  400 `Invalid Request` — verified live on 2026-09-19.
- **Branch on `http.getSize() < 0` at runtime** to decide whether to de-chunk.
  The GTFS API is chunked, the realtime API is not, and getting this wrong
  fails silently in both directions (`docs/hardware-notes.md`).
- **The `tripid` filter is inexact.** Re-filter returned trip ids against the
  requested set.
- **Delay comes from trip-level `trip_update.delay`**, preferring
  `stop_time_update.departure.delay` only when its `stop_id` is ours.
- **TLS verifies against the pinned DigiCert Global Root G2.**
  `setInsecure()` must not appear in `src/`.
- Pure code in `lib/core/` includes no Arduino or TFT headers, and builds under
  `native`. ArduinoJson is allowed there (it is portable and already used by
  both envs).
- Python `round()` is half-to-even (`nearbyint`), `int()` truncates (a C cast),
  PIL boxes are inclusive. Keep new ports consistent with these.
- Every `Board` the renderer sees is built by `build_board`, never assembled ad
  hoc in `src/`.
- Native tests need GCC on PATH. A fresh shell has it; an older one needs
  `export PATH="/c/Users/green/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin:$PATH"`.
- Uploading needs a human: hold BOOT, press and release EN, release BOOT, then
  `pio run -e <env> -t upload`. The controller does every flash, serial capture
  and on-glass check.
- `python -m pytest` and `pio test -e native` stay green after every task.
- Commits end with `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.

## File Structure

| File | Responsibility |
|---|---|
| `lib/core/src/nztime.{h,cpp}` | Civil-date arithmetic, NZ DST rules, GTFS time parsing |
| `lib/core/src/dechunk.{h,cpp}` | Pure HTTP chunked-transfer unwrapper, an ArduinoJson input |
| `lib/core/src/at_api.{h,cpp}` | AT URLs, ArduinoJson filters, response parsers into structs |
| `lib/core/src/live.{h,cpp}` | Watch config, snapshot types, direction derivation, realtime merge, `build_board` |
| `src/secrets.example.h` | Template for the git-ignored `src/secrets.h` |
| `src/watch_config.h` | The two watches and the location label (committed; no secrets) |
| `src/at_ca.h` | Pinned DigiCert Global Root G2 PEM |
| `src/at_client.{h,cpp}` | One HTTPS GET: CA, key header, chunked branch, filtered parse |
| `src/fetcher.{h,cpp}` | The core-0 task: resolve, schedule, direction, realtime, backoff, publish |
| `src/main.cpp` | Frame loop; demo or live board source |
| `test/test_nztime,test_dechunk,test_at_api,test_live/` | Native suites |

---

### Task 1: A location and a per-watch message, Python first

**Files:**
- Modify: `tools/board/model.py`, `tools/board/render.py`, `tools/board/scenes.py`
- Modify: `tests/test_model.py`, `tests/test_render.py`
- Create: `tests/golden/check_config.png` (generated)
- Modify: `tools/export_sprites.py`, `lib/core/src/demo.h`, `lib/core/src/demo.cpp`, `lib/core/src/model.h`, `lib/core/src/model.cpp`, `src/ui.cpp`, `test/test_model/test_main.cpp`, `test/test_demo/test_main.cpp`
- Regenerate: `lib/core/src/scenes_data.h`

**Interfaces:**
- Produces (Python): `Watch.message: Optional[str] = None`, `Board.location: str = "Kingsland"`
- Produces (C): `Board.location[24]`, `Watch.message[32]`,
  `void board_set_location(Board*, const char*)`, `void watch_set_message(Watch*, const char*)`
- Rule, both sides: a watch with a message shows no times and a parked vehicle.

Why: the data path needs to say "check config" and "check API key" on a lane
(spec §8), and the status bar's location stops being hardcoded. Per the global
constraint, the simulator gets it first.

- [ ] **Step 1: Write the failing Python tests.** Append to `tests/test_model.py`:

```python
def test_a_watch_has_no_message_by_default():
    assert Watch(badge="20", headsign="x", kind="bus").message is None


def test_a_board_defaults_to_the_kingsland_location():
    assert Board([Watch(badge="20", headsign="x", kind="bus")], "17:42").location == "Kingsland"
```

Append to `tests/test_render.py` (it already imports `render`, `scenes`; add
`from tools.board.model import Board, Departure, Watch` if not present):

```python
def test_a_watch_with_a_message_never_shows_a_time():
    # Spec 3a: a watch that cannot prove which way it points must not display
    # a time. Rendering must therefore ignore departures entirely.
    with_deps = Board([Watch(departures=[Departure(240, live=True)],
                             message="check config", **scenes.BUS20)], "17:42")
    without = Board([Watch(departures=[], message="check config", **scenes.BUS20)], "17:42")
    assert render.render(with_deps, t=0.0).tobytes() == render.render(without, t=0.0).tobytes()


def test_the_message_is_actually_drawn():
    plain = Board([Watch(departures=[], **scenes.BUS20)], "17:42")
    noisy = Board([Watch(departures=[], message="check config", **scenes.BUS20)], "17:42")
    assert render.render(plain, t=0.0).tobytes() != render.render(noisy, t=0.0).tobytes()


def test_the_location_is_drawn_in_the_status_bar():
    here = Board([Watch(departures=[], **scenes.BUS20)], "17:42")
    there = Board([Watch(departures=[], **scenes.BUS20)], "17:42", location="Ponsonby")
    assert render.render(here, t=0.0).tobytes() != render.render(there, t=0.0).tobytes()
```

- [ ] **Step 2: Run them to see them fail**

Run: `python -m pytest tests/test_model.py tests/test_render.py -q`
Expected: failures — `TypeError: __init__() got an unexpected keyword argument 'message'` / `'location'`.

- [ ] **Step 3: Add the fields in `tools/board/model.py`.** In `Watch`, after
`route_color`:

```python
    # Set when the board cannot honestly show times for this watch (spec 8).
    # A watch with a message shows no departures at all.
    message: Optional[str] = None
```

In `Board`, after `theme`:

```python
    location: str = "Kingsland"
```

- [ ] **Step 4: Draw them in `tools/board/render.py`.** In `_status_bar`,
replace the hardcoded label:

```python
    d.text((6, mid), board.location, font=reg(11), fill=th.colour("dim"), anchor="lm")
```

In `_lane`, replace `nxt = watch.next` with:

```python
    # A watch carrying a message must not show a time, however many
    # departures it happens to hold.
    nxt = None if watch.message else watch.next
```

and replace the `if nxt is None:` branch of the times block with:

```python
    if watch.message:
        d.text(ln.minutes_xy, "--", font=mono(20), fill=th.colour("dim"), anchor="ra")
        d.text(ln.following_xy, watch.message, font=reg(9),
               fill=th.colour("warn"), anchor="ra")
    elif nxt is None:
        d.text(ln.minutes_xy, "--", font=mono(20), fill=th.colour("dim"), anchor="ra")
        d.text(ln.following_xy, "none tonight", font=reg(9),
               fill=th.colour("dim"), anchor="ra")
```

- [ ] **Step 5: Add the scene.** At the end of `SCENES` in `tools/board/scenes.py`:

```python
    # Direction could not be derived (spec 3a). The departures are deliberately
    # present: a watch that cannot prove its direction must show none of them.
    "check_config": Board([
        Watch(departures=[Departure(240, live=True), Departure(1020)],
              message="check config", **BUS20),
        Watch(departures=[Departure(420, live=True)], **TRAIN),
    ], "17:42"),
```

- [ ] **Step 6: Run the Python tests, then freeze the new golden**

Run: `python -m pytest -q`
Expected: the five new tests pass; `test_matches_golden[check_config]` fails
with "no golden for 'check_config'".
Run: `python tools/regolden.py`
Run: `git status --short tests/golden`
Expected: exactly one new file, `tests/golden/check_config.png`. If any
existing golden changed, stop and report — the port must not alter existing
renders.
Run: `python -m pytest -q`
Expected: all pass.

- [ ] **Step 7: Mirror the fields in C.** In `lib/core/src/model.h`, add to
`Watch` after `route_color`:

```cpp
  // Set when the board cannot honestly show times for this watch (spec 8).
  // A watch with a message shows no departures at all.
  char message[32];
```

add to `Board` after `theme`:

```cpp
  char location[24];
```

and declare beneath `watch_init`:

```cpp
void watch_set_message(Watch* w, const char* message);  // null or "" clears it
void board_set_location(Board* b, const char* location);
```

In `lib/core/src/model.cpp` implement both with the existing `copy_text`
helper (`watch_set_message(w, nullptr)` must leave `w->message[0] == '\0'`).

- [ ] **Step 8: Carry them through the exporter.** In `tools/export_sprites.py`
`render_scenes_header`, emit `location` after the clock for a scene, and
`message` after `route_color` for a watch:

```python
        parts.append(f"    {{{_cstr(name)}, {_cstr(board.clock)}, {_cstr(board.location)}, "
                     f"{board.stale_s}, {_bool(board.dimmed)}, {len(board.watches)}, {{")
```

```python
            parts.append(f"        {{{_cstr(w.badge)}, {_cstr(w.headsign)}, "
                         f"{KIND_CONST[w.kind]}, {_cstr(w.route_color)}, {_cstr(w.message)}, "
                         f"{len(w.departures)}, {{{deps}}}}},")
```

In `lib/core/src/demo.h`, add `const char* location;` to `Scene` after `clock`,
and `const char* message;` to `SceneWatch` after `route_color`. In
`lib/core/src/demo.cpp` `board_from_scene`, after `watch_init(...)` add
`watch_set_message(&w, sw.message);`, and after the clock copy add
`board_set_location(&b, s.location);`.

- [ ] **Step 9: Honour the message on the panel.** In `src/ui.cpp`: delete the
`LOCATION` constant and use `b.location` in `status_bar`; in `times()` replace
the `nxt == nullptr` early branch with:

```cpp
  const Departure* nxt = w.message[0] != '\0' ? nullptr : w.next();
  if (nxt == nullptr) {
    p.text("--", ln.minutes_x, ln.minutes_y, TR_DATUM, FONT_MINUTES, dim);
    const bool has_message = w.message[0] != '\0';
    p.text(has_message ? w.message : "none tonight", ln.following_x, ln.following_y,
           TR_DATUM, FONT_SMALL, has_message ? th.colours[C_WARN] : dim);
    return;
  }
```

(delete the old `const Departure* nxt = w.next();` line that preceded it), and
in `vehicle()` change the first line to the same guard:

```cpp
  const Departure* nxt = w.message[0] != '\0' ? nullptr : w.next();
```

- [ ] **Step 10: Write the failing C tests.** Add to `test/test_model/test_main.cpp`
(and register with `RUN_TEST`):

```cpp
void test_message_and_location_round_trip_and_truncate() {
  Watch w;
  watch_init(&w, "20", "x", Kind::Bus, nullptr);
  TEST_ASSERT_EQUAL_STRING("", w.message);
  watch_set_message(&w, "check config");
  TEST_ASSERT_EQUAL_STRING("check config", w.message);
  watch_set_message(&w, nullptr);
  TEST_ASSERT_EQUAL_STRING("", w.message);
  watch_set_message(&w, "a message far longer than the thirty-two bytes we keep for it");
  TEST_ASSERT_EQUAL_size_t(sizeof w.message - 1, strlen(w.message));

  Board b;
  memset(&b, 0, sizeof b);
  board_set_location(&b, "Kingsland");
  TEST_ASSERT_EQUAL_STRING("Kingsland", b.location);
}
```

In `test/test_demo/test_main.cpp`: change the scene count assertion to 9, add
`TEST_ASSERT_EQUAL_STRING("check_config", demo_scene(8).name);`, replace the
hardcoded `8 * DEMO_SCENE_MS` in the theme-cycle test with
`demo_scene_count() * DEMO_SCENE_MS`, and add:

```cpp
void test_the_check_config_scene_carries_its_message_and_location() {
  const Board b = demo_board(8 * DEMO_SCENE_MS);
  TEST_ASSERT_EQUAL_STRING("check config", b.watches[0].message);
  TEST_ASSERT_EQUAL_STRING("", b.watches[1].message);
  TEST_ASSERT_EQUAL_STRING("Kingsland", b.location);
}
```

- [ ] **Step 11: Regenerate and run everything**

Run: `python tools/export_sprites.py`
Run: `git diff --ignore-cr-at-eol --stat -- src/sprites.h lib/core/src/theme_data.h`
Expected: empty. If so, `git checkout -- src/sprites.h lib/core/src/theme_data.h`.
Run: `python -m pytest -q` → all pass.
Run: `pio test -e native` → all suites pass.
Run: `pio run -e esp32` → [SUCCESS].

- [ ] **Step 12: Commit**

```bash
git add tools/board/model.py tools/board/render.py tools/board/scenes.py tests/ tools/export_sprites.py lib/core/src/ src/ui.cpp test/
git commit -m "Give a board a location and a watch a message, simulator first

The data path needs lanes that can say 'check config' and 'check API key'
without showing a time (spec 3a and 8), and a status bar whose location is
not hardcoded. Both start in the Python, with a golden, then regenerate.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 2: NZ time

**Files:**
- Create: `lib/core/src/nztime.h`, `lib/core/src/nztime.cpp`
- Test: `test/test_nztime/test_main.cpp`

**Interfaces:**
- Produces:
  - `struct CivilDate { int y, m, d; };`
  - `struct LocalTime { int y, m, d, hour, min, sec, offset_s; };`
  - `int64_t days_from_civil(int y, int m, int d);` / `CivilDate civil_from_days(int64_t z);`
  - `int nz_offset_s(int64_t epoch);` → 43200 or 46800
  - `LocalTime nz_local(int64_t epoch);`
  - `int64_t nz_epoch(int y, int m, int d, int32_t secs_of_day);`
  - `bool parse_gtfs_time(const char* s, int32_t* secs);` accepts hours 0..47
  - `bool parse_iso_date(const char* s, CivilDate* out);`
  - `int64_t gtfs_epoch(CivilDate service_date, int32_t secs);`
  - `void format_clock(int64_t epoch, char* out, size_t n);` → `"17:42"`
  - `void format_iso_date(CivilDate d, char* out, size_t n);` → `"2026-09-19"`

The host C runtime cannot be used for this: with `TZ=NZST-12NZDT,M9.5.0,M4.1.0/3`
set, Windows' UCRT placed 2026-09-28 12:00 NZDT at 11:00 UTC instead of the
correct 23:00 UTC on the 27th. So the rules are ours, in pure code, identical on
both sides. Expected values below come from Python's `zoneinfo`
(`Pacific/Auckland`, tzdata 2025.2).

NZ rules: NZDT (UTC+13) from 02:00 NZST on the **last Sunday in September** to
03:00 NZDT on the **first Sunday in April**; NZST (UTC+12) otherwise.

GTFS service-day rule: a time is elapsed seconds from **noon minus 12 hours**,
in absolute time, so noon always maps to noon even on a DST day.

- [ ] **Step 1: Write the failing test** at `test/test_nztime/test_main.cpp`

```cpp
#include <string.h>
#include <unity.h>

#include "nztime.h"

void setUp() {}
void tearDown() {}

void test_civil_days_round_trip() {
  TEST_ASSERT_EQUAL_INT64(0, days_from_civil(1970, 1, 1));
  TEST_ASSERT_EQUAL_INT64(20715, days_from_civil(2026, 9, 19));
  const CivilDate d = civil_from_days(20715);
  TEST_ASSERT_EQUAL_INT(2026, d.y);
  TEST_ASSERT_EQUAL_INT(9, d.m);
  TEST_ASSERT_EQUAL_INT(19, d.d);
  for (int64_t z = -1000; z < 30000; z += 97) {
    const CivilDate c = civil_from_days(z);
    TEST_ASSERT_EQUAL_INT64(z, days_from_civil(c.y, c.m, c.d));
  }
}

void test_dst_starts_on_the_last_sunday_in_september() {
  TEST_ASSERT_EQUAL_INT(43200, nz_offset_s(1790431199));  // 2026-09-27 01:59:59 NZST
  TEST_ASSERT_EQUAL_INT(46800, nz_offset_s(1790431200));  // 03:00:00 NZDT
}

void test_dst_ends_on_the_first_sunday_in_april() {
  TEST_ASSERT_EQUAL_INT(46800, nz_offset_s(1806760799));  // 2027-04-04 02:59:59 NZDT
  TEST_ASSERT_EQUAL_INT(43200, nz_offset_s(1806760800));  // 02:00:00 NZST
}

void test_offsets_in_the_middle_of_each_season() {
  TEST_ASSERT_EQUAL_INT(46800, nz_offset_s(1799953200));  // 2027-01-15 08:00 NZDT
  TEST_ASSERT_EQUAL_INT(43200, nz_offset_s(1782849600));  // 2026-07-01 08:00 NZST
}

void test_local_breakdown() {
  const LocalTime t = nz_local(1789796520);
  TEST_ASSERT_EQUAL_INT(2026, t.y);
  TEST_ASSERT_EQUAL_INT(9, t.m);
  TEST_ASSERT_EQUAL_INT(19, t.d);
  TEST_ASSERT_EQUAL_INT(17, t.hour);
  TEST_ASSERT_EQUAL_INT(42, t.min);
  TEST_ASSERT_EQUAL_INT(0, t.sec);
  TEST_ASSERT_EQUAL_INT(43200, t.offset_s);

  const LocalTime u = nz_local(1790550000);  // 2026-09-28 12:00 NZDT
  TEST_ASSERT_EQUAL_INT(28, u.d);
  TEST_ASSERT_EQUAL_INT(12, u.hour);
  TEST_ASSERT_EQUAL_INT(46800, u.offset_s);
}

void test_local_to_epoch_round_trips() {
  TEST_ASSERT_EQUAL_INT64(1789796520, nz_epoch(2026, 9, 19, 17 * 3600 + 42 * 60));
  TEST_ASSERT_EQUAL_INT64(1790550000, nz_epoch(2026, 9, 28, 12 * 3600));
}

void test_local_to_epoch_at_the_spring_gap_and_autumn_overlap() {
  // 02:30 on 2026-09-27 never happens; resolve it with the offset in force
  // before the jump, as Python's fold=0 does: the instant is 03:30 NZDT.
  TEST_ASSERT_EQUAL_INT64(1790433000, nz_epoch(2026, 9, 27, 2 * 3600 + 1800));
  // 02:30 on 2027-04-04 happens twice; take the first (still NZDT).
  TEST_ASSERT_EQUAL_INT64(1806759000, nz_epoch(2027, 4, 4, 2 * 3600 + 1800));
}

void test_parse_gtfs_time_including_after_midnight() {
  int32_t s = -1;
  TEST_ASSERT_TRUE(parse_gtfs_time("15:04:07", &s));
  TEST_ASSERT_EQUAL_INT32(15 * 3600 + 4 * 60 + 7, s);
  TEST_ASSERT_TRUE(parse_gtfs_time("25:10:00", &s));
  TEST_ASSERT_EQUAL_INT32(25 * 3600 + 600, s);
  TEST_ASSERT_TRUE(parse_gtfs_time("00:00:00", &s));
  TEST_ASSERT_EQUAL_INT32(0, s);
  TEST_ASSERT_FALSE(parse_gtfs_time("", &s));
  TEST_ASSERT_FALSE(parse_gtfs_time("15:04", &s));
  TEST_ASSERT_FALSE(parse_gtfs_time("aa:bb:cc", &s));
  TEST_ASSERT_FALSE(parse_gtfs_time("48:00:00", &s));
  TEST_ASSERT_FALSE(parse_gtfs_time("15:60:00", &s));
}

void test_parse_iso_date() {
  CivilDate d{};
  TEST_ASSERT_TRUE(parse_iso_date("2026-09-13", &d));
  TEST_ASSERT_EQUAL_INT(2026, d.y);
  TEST_ASSERT_EQUAL_INT(9, d.m);
  TEST_ASSERT_EQUAL_INT(13, d.d);
  TEST_ASSERT_FALSE(parse_iso_date("2026-13-01", &d));
  TEST_ASSERT_FALSE(parse_iso_date("nope", &d));
}

void test_gtfs_epoch_uses_noon_minus_twelve_hours() {
  TEST_ASSERT_EQUAL_INT64(1789268647, gtfs_epoch({2026, 9, 13}, 15 * 3600 + 4 * 60 + 7));
  // 25:10 on the 19th is 01:10 on the 20th.
  TEST_ASSERT_EQUAL_INT64(1789823400, gtfs_epoch({2026, 9, 19}, 25 * 3600 + 600));
  // On the day DST starts, noon is still noon...
  TEST_ASSERT_EQUAL_INT64(1790463600, gtfs_epoch({2026, 9, 27}, 12 * 3600));
  // ...but 01:00 is 00:00, because an hour of that day does not exist.
  TEST_ASSERT_EQUAL_INT64(1790424000, gtfs_epoch({2026, 9, 27}, 3600));
  // And on the day it ends, 01:00 is 02:00 for the same reason in reverse.
  TEST_ASSERT_EQUAL_INT64(1806757200, gtfs_epoch({2027, 4, 4}, 3600));
}

void test_formatting() {
  char buf[16];
  format_clock(1789796520, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("17:42", buf);
  format_iso_date({2026, 9, 5}, buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("2026-09-05", buf);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_civil_days_round_trip);
  RUN_TEST(test_dst_starts_on_the_last_sunday_in_september);
  RUN_TEST(test_dst_ends_on_the_first_sunday_in_april);
  RUN_TEST(test_offsets_in_the_middle_of_each_season);
  RUN_TEST(test_local_breakdown);
  RUN_TEST(test_local_to_epoch_round_trips);
  RUN_TEST(test_local_to_epoch_at_the_spring_gap_and_autumn_overlap);
  RUN_TEST(test_parse_gtfs_time_including_after_midnight);
  RUN_TEST(test_parse_iso_date);
  RUN_TEST(test_gtfs_epoch_uses_noon_minus_twelve_hours);
  RUN_TEST(test_formatting);
  return UNITY_END();
}
```

- [ ] **Step 2: Run it to see it fail**

Run: `pio test -e native -f test_nztime`
Expected: build error, `nztime.h: No such file or directory`.

- [ ] **Step 3: Create `lib/core/src/nztime.h`**

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>

// Pacific/Auckland without the C library. Windows' UCRT mis-handles the POSIX
// TZ string (it placed 2026-09-28 12:00 NZDT at 11:00 UTC), so the rules live
// here and behave identically on the device and in the native tests.
//
// NZDT (UTC+13) runs from 02:00 NZST on the last Sunday in September to
// 03:00 NZDT on the first Sunday in April. NZST (UTC+12) otherwise.

struct CivilDate {
  int y, m, d;
};

struct LocalTime {
  int y, m, d, hour, min, sec;
  int offset_s;  // 43200 (NZST) or 46800 (NZDT)
};

// Days since 1970-01-01 (Howard Hinnant's civil algorithms).
int64_t days_from_civil(int y, int m, int d);
CivilDate civil_from_days(int64_t z);

int nz_offset_s(int64_t epoch);
LocalTime nz_local(int64_t epoch);

// Wall-clock to epoch. A time in the spring gap resolves with the offset in
// force before the jump; an ambiguous autumn time takes the first occurrence.
int64_t nz_epoch(int y, int m, int d, int32_t secs_of_day);

// "HH:MM:SS", hours 0..47 - GTFS uses 25:10:00 for 01:10 the next day.
bool parse_gtfs_time(const char* s, int32_t* secs);
bool parse_iso_date(const char* s, CivilDate* out);

// GTFS measures a departure as elapsed seconds from noon minus 12 hours of the
// service day, in absolute time, so noon stays noon when the clocks change.
int64_t gtfs_epoch(CivilDate service_date, int32_t secs);

void format_clock(int64_t epoch, char* out, size_t n);      // "17:42"
void format_iso_date(CivilDate d, char* out, size_t n);     // "2026-09-19"
```

- [ ] **Step 4: Create `lib/core/src/nztime.cpp`**

```cpp
#include "nztime.h"

#include <stdio.h>
#include <string.h>

namespace {

constexpr int64_t DAY = 86400;
constexpr int NZST = 12 * 3600;
constexpr int NZDT = 13 * 3600;

// 0 = Sunday.
int weekday_from_days(int64_t z) { return static_cast<int>((z + 4) % 7 + 7) % 7; }

// Day-of-month of the last `weekday` in a month.
int last_weekday_of(int y, int m, int weekday) {
  static const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int last = mdays[m - 1];
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) last = 29;
  const int wd = weekday_from_days(days_from_civil(y, m, last));
  return last - ((wd - weekday + 7) % 7);
}

int first_weekday_of(int y, int m, int weekday) {
  const int wd = weekday_from_days(days_from_civil(y, m, 1));
  return 1 + ((weekday - wd + 7) % 7);
}

// DST starts 02:00 NZST on the last Sunday in September.
int64_t dst_start(int y) {
  return days_from_civil(y, 9, last_weekday_of(y, 9, 0)) * DAY + 2 * 3600 - NZST;
}

// DST ends 03:00 NZDT on the first Sunday in April.
int64_t dst_end(int y) {
  return days_from_civil(y, 4, first_weekday_of(y, 4, 0)) * DAY + 3 * 3600 - NZDT;
}

}  // namespace

int64_t days_from_civil(int y, int m, int d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

CivilDate civil_from_days(int64_t z) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t y = static_cast<int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp + (mp < 10 ? 3 : -9);
  return {static_cast<int>(y + (m <= 2)), static_cast<int>(m), static_cast<int>(d)};
}

int nz_offset_s(int64_t epoch) {
  const CivilDate utc = civil_from_days(epoch >= 0 ? epoch / DAY : (epoch - DAY + 1) / DAY);
  return (epoch >= dst_start(utc.y) || epoch < dst_end(utc.y)) ? NZDT : NZST;
}

LocalTime nz_local(int64_t epoch) {
  const int offset = nz_offset_s(epoch);
  const int64_t local = epoch + offset;
  int64_t days = local / DAY;
  int64_t rem = local % DAY;
  if (rem < 0) {
    rem += DAY;
    days -= 1;
  }
  const CivilDate c = civil_from_days(days);
  LocalTime t{};
  t.y = c.y;
  t.m = c.m;
  t.d = c.d;
  t.hour = static_cast<int>(rem / 3600);
  t.min = static_cast<int>((rem % 3600) / 60);
  t.sec = static_cast<int>(rem % 60);
  t.offset_s = offset;
  return t;
}

int64_t nz_epoch(int y, int m, int d, int32_t secs_of_day) {
  const int64_t wall = days_from_civil(y, m, d) * DAY + secs_of_day;
  const int64_t as_dst = wall - NZDT;
  if (nz_offset_s(as_dst) == NZDT) return as_dst;  // also picks the first of an
                                                   // ambiguous autumn pair
  const int64_t as_std = wall - NZST;
  if (nz_offset_s(as_std) == NZST) return as_std;
  return as_std;  // spring gap: the wall time never happens
}

bool parse_gtfs_time(const char* s, int32_t* secs) {
  if (s == nullptr || strlen(s) != 8 || s[2] != ':' || s[5] != ':') return false;
  int v[3] = {0, 0, 0};
  for (int part = 0; part < 3; part++) {
    const char* p = s + part * 3;
    if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return false;
    v[part] = (p[0] - '0') * 10 + (p[1] - '0');
  }
  if (v[0] > 47 || v[1] > 59 || v[2] > 59) return false;
  *secs = v[0] * 3600 + v[1] * 60 + v[2];
  return true;
}

bool parse_iso_date(const char* s, CivilDate* out) {
  if (s == nullptr || strlen(s) != 10 || s[4] != '-' || s[7] != '-') return false;
  int y = 0;
  for (int i = 0; i < 4; i++) {
    if (s[i] < '0' || s[i] > '9') return false;
    y = y * 10 + (s[i] - '0');
  }
  int part[2] = {0, 0};
  for (int k = 0; k < 2; k++) {
    const char* p = s + 5 + k * 3;
    if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return false;
    part[k] = (p[0] - '0') * 10 + (p[1] - '0');
  }
  if (part[0] < 1 || part[0] > 12 || part[1] < 1 || part[1] > 31) return false;
  *out = {y, part[0], part[1]};
  return true;
}

int64_t gtfs_epoch(CivilDate service_date, int32_t secs) {
  const int64_t noon = nz_epoch(service_date.y, service_date.m, service_date.d, 12 * 3600);
  return noon - 12 * 3600 + secs;
}

void format_clock(int64_t epoch, char* out, size_t n) {
  const LocalTime t = nz_local(epoch);
  snprintf(out, n, "%02d:%02d", t.hour, t.min);
}

void format_iso_date(CivilDate d, char* out, size_t n) {
  snprintf(out, n, "%04d-%02d-%02d", d.y, d.m, d.d);
}
```

- [ ] **Step 5: Run it to see it pass**

Run: `pio test -e native -f test_nztime`
Expected: `11 test cases: 11 succeeded`.

- [ ] **Step 6: Commit**

```bash
git add lib/core/src/nztime.h lib/core/src/nztime.cpp test/test_nztime/test_main.cpp
git commit -m "Implement Pacific/Auckland ourselves, because the host libc gets it wrong

Windows' UCRT placed 2026-09-28 12:00 NZDT at 11:00 UTC with the POSIX TZ
string the spike used. The rules are 40 lines; the values are pinned against
Python's zoneinfo, including both DST transitions and the GTFS noon-minus-12
service-day rule that keeps noon at noon when the clocks move.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 3: The de-chunker, as pure code

**Files:**
- Create: `lib/core/src/dechunk.h`, `lib/core/src/dechunk.cpp`
- Test: `test/test_dechunk/test_main.cpp`
- Modify: `platformio.ini` (ArduinoJson on both envs)

**Interfaces:**
- Produces: `class Dechunker { using ReadFn = int (*)(void*); Dechunker(ReadFn, void* ctx); int read(); size_t readBytes(char*, size_t); bool failed() const; };`
- `read()` returns the next body byte or -1; `ReadFn` returns -1 on end or timeout.
- The pair `read`/`readBytes` is exactly what ArduinoJson accepts as a custom
  input, so `deserializeJson(doc, dechunker)` works on both sides.

`spike/heap/src/stage3.cpp` proved this on hardware but as an Arduino `Stream`,
untestable natively. This is the same algorithm with the source behind a
callback. The bug it prevents is silent: fed the raw chunked body, ArduinoJson
reads the chunk-size line `2561` as a valid JSON number and reports success.

- [ ] **Step 1: Add ArduinoJson to both environments** in `platformio.ini`:
in `[env:esp32]` change the `lib_deps` line to

```ini
lib_deps =
  bodmer/TFT_eSPI@2.5.43
  bblanchon/ArduinoJson@7.4.3
```

and add to `[env:native]`

```ini
lib_deps = bblanchon/ArduinoJson@7.4.3
```

- [ ] **Step 2: Write the failing test** at `test/test_dechunk/test_main.cpp`

```cpp
#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

#include "dechunk.h"

void setUp() {}
void tearDown() {}

namespace {
struct Source {
  const char* p;
  size_t left;
};

int next_byte(void* ctx) {
  Source* s = static_cast<Source*>(ctx);
  if (s->left == 0) return -1;
  s->left--;
  return static_cast<unsigned char>(*s->p++);
}

std::string drain(const char* wire, size_t len, bool* failed = nullptr) {
  Source src{wire, len};
  Dechunker d(next_byte, &src);
  std::string out;
  for (int c = d.read(); c >= 0; c = d.read()) out.push_back(static_cast<char>(c));
  if (failed != nullptr) *failed = d.failed();
  return out;
}
}  // namespace

void test_unwraps_chunks() {
  const char wire[] = "4\r\nabcd\r\n3\r\nefg\r\n0\r\n\r\n";
  TEST_ASSERT_EQUAL_STRING("abcdefg", drain(wire, sizeof wire - 1).c_str());
}

void test_chunk_sizes_are_hexadecimal() {
  const char wire[] = "a\r\n0123456789\r\n0\r\n\r\n";
  TEST_ASSERT_EQUAL_STRING("0123456789", drain(wire, sizeof wire - 1).c_str());
}

void test_accepts_chunk_extensions() {
  const char wire[] = "4;name=value\r\nabcd\r\n0\r\n\r\n";
  TEST_ASSERT_EQUAL_STRING("abcd", drain(wire, sizeof wire - 1).c_str());
}

void test_a_clean_end_is_not_a_failure() {
  bool failed = true;
  const char wire[] = "1\r\nx\r\n0\r\n\r\n";
  drain(wire, sizeof wire - 1, &failed);
  TEST_ASSERT_FALSE(failed);
}

void test_a_truncated_body_is_a_failure() {
  bool failed = false;
  const char wire[] = "8\r\nabc";  // promised 8 bytes, source dies after 3
  TEST_ASSERT_EQUAL_STRING("abc", drain(wire, sizeof wire - 1, &failed).c_str());
  TEST_ASSERT_TRUE(failed);
}

void test_a_malformed_size_line_is_a_failure() {
  bool failed = false;
  const char wire[] = "zz\r\nabcd\r\n0\r\n\r\n";
  TEST_ASSERT_EQUAL_STRING("", drain(wire, sizeof wire - 1, &failed).c_str());
  TEST_ASSERT_TRUE(failed);
}

void test_read_bytes_fills_the_buffer() {
  const char wire[] = "4\r\nabcd\r\n3\r\nefg\r\n0\r\n\r\n";
  Source src{wire, sizeof wire - 1};
  Dechunker d(next_byte, &src);
  char buf[8] = {0};
  TEST_ASSERT_EQUAL_size_t(7, d.readBytes(buf, sizeof buf - 1));
  TEST_ASSERT_EQUAL_STRING("abcdefg", buf);
}

void test_arduinojson_parses_through_it() {
  // The whole point: this is what AT's GTFS API puts on the wire.
  const char wire[] = "12\r\n{\"data\":[{\"a\":1}\r\n2\r\n]}\r\n0\r\n\r\n";
  Source src{wire, sizeof wire - 1};
  Dechunker d(next_byte, &src);
  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, d));
  TEST_ASSERT_EQUAL_INT(1, doc["data"][0]["a"].as<int>());
}

void test_without_dechunking_the_same_bytes_parse_as_a_number() {
  // Documents the silent failure this class exists to prevent: the chunk
  // size line is itself valid JSON.
  const char wire[] = "12\r\n{\"data\":[{\"a\":1}\r\n2\r\n]}\r\n0\r\n\r\n";
  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, wire));
  TEST_ASSERT_TRUE(doc.is<int>());
  TEST_ASSERT_TRUE(doc["data"].isNull());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_unwraps_chunks);
  RUN_TEST(test_chunk_sizes_are_hexadecimal);
  RUN_TEST(test_accepts_chunk_extensions);
  RUN_TEST(test_a_clean_end_is_not_a_failure);
  RUN_TEST(test_a_truncated_body_is_a_failure);
  RUN_TEST(test_a_malformed_size_line_is_a_failure);
  RUN_TEST(test_read_bytes_fills_the_buffer);
  RUN_TEST(test_arduinojson_parses_through_it);
  RUN_TEST(test_without_dechunking_the_same_bytes_parse_as_a_number);
  return UNITY_END();
}
```

- [ ] **Step 3: Run it to see it fail**

Run: `pio test -e native -f test_dechunk`
Expected: build error, `dechunk.h: No such file or directory`.

- [ ] **Step 4: Create `lib/core/src/dechunk.h`**

```cpp
#pragma once
#include <stddef.h>
#include <stdint.h>

// Unwraps HTTP chunked transfer-encoding, one chunk header at a time, so
// memory stays flat however large the body is.
//
// AT's GTFS API is chunked. Handing the raw body to a JSON parser is a silent
// failure: it reads the chunk-size line ("2561\r\n") as a valid JSON number,
// stops, and reports success with an empty document - zero departures, no
// error, forever (docs/hardware-notes.md).
//
// read()/readBytes() are the pair ArduinoJson accepts as a custom input, so a
// Dechunker can be passed straight to deserializeJson.
class Dechunker {
 public:
  // Returns the next byte from the transport, or -1 on end-of-stream, error
  // or timeout.
  using ReadFn = int (*)(void* ctx);

  Dechunker(ReadFn fn, void* ctx) : fn_(fn), ctx_(ctx) {}

  int read();
  size_t readBytes(char* buffer, size_t length);

  // True when the framing ended badly: a truncated chunk or an unparsable
  // size line, as opposed to a clean terminating 0-chunk.
  bool failed() const { return failed_; }

 private:
  bool ensure();  // make left_ > 0, reading a chunk header if needed

  ReadFn fn_;
  void* ctx_;
  size_t left_ = 0;
  bool done_ = false;
  bool failed_ = false;
};
```

- [ ] **Step 5: Create `lib/core/src/dechunk.cpp`**

```cpp
#include "dechunk.h"

#include <stdlib.h>
#include <string.h>

int Dechunker::read() {
  if (!ensure()) return -1;
  const int c = fn_(ctx_);
  if (c < 0) {
    done_ = true;
    failed_ = true;  // the chunk promised more than the transport delivered
    return -1;
  }
  if (--left_ == 0) {
    fn_(ctx_);  // \r
    fn_(ctx_);  // \n
  }
  return c;
}

size_t Dechunker::readBytes(char* buffer, size_t length) {
  size_t n = 0;
  while (n < length) {
    const int c = read();
    if (c < 0) break;
    buffer[n++] = static_cast<char>(c);
  }
  return n;
}

bool Dechunker::ensure() {
  if (done_) return false;
  if (left_ > 0) return true;

  char line[24];
  size_t n = 0;
  for (;;) {
    const int c = fn_(ctx_);
    if (c < 0) {
      done_ = true;
      failed_ = true;
      return false;
    }
    if (c == '\n') break;
    if (c != '\r' && n < sizeof line - 1) line[n++] = static_cast<char>(c);
  }
  line[n] = '\0';
  if (n == 0) return ensure();  // stray blank line between chunks

  char* end = nullptr;
  const long size = strtol(line, &end, 16);  // chunk sizes are hex; ";ext" stops it
  if (end == line || size < 0) {
    done_ = true;
    failed_ = true;
    return false;
  }
  if (size == 0) {
    done_ = true;  // clean end
    return false;
  }
  left_ = static_cast<size_t>(size);
  return true;
}
```

- [ ] **Step 6: Run it to see it pass**

Run: `pio test -e native -f test_dechunk`
Expected: `9 test cases: 9 succeeded`.
Run: `pio run -e esp32`
Expected: [SUCCESS] (ArduinoJson now builds into the firmware too).

- [ ] **Step 7: Commit**

```bash
git add platformio.ini lib/core/src/dechunk.h lib/core/src/dechunk.cpp test/test_dechunk/test_main.cpp
git commit -m "Port the de-chunker to pure code, with the silent failure as a test

The spike proved the algorithm on hardware but as an Arduino Stream, which
cannot be tested natively. Behind a read callback it is testable, and one test
pins the bug it exists to prevent: the same bytes fed straight to ArduinoJson
parse as the number 4657 and report success.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 4: AT URLs, filters and parsers

**Files:**
- Create: `lib/core/src/at_api.h`, `lib/core/src/at_api.cpp`
- Test: `test/test_at_api/test_main.cpp`

**Interfaces:**
- Produces sizes: `TRIP_ID_LEN 48`, `STOP_ID_LEN 24`, `ROUTE_ID_LEN 16`, `SHORT_NAME_LEN 12`, `STOP_CODE_LEN 12`
- Produces structs: `StopInfo`, `RouteInfo`, `StopTripRow`, `TripStop`, `RtEntity` (fields below)
- Produces URL builders returning `false` when truncated:
  `url_stop_by_code`, `url_routes_by_short_name`, `url_rail_routes`, `url_stoptrips`, `url_trip_stops`, `url_realtime`
- Produces filter builders: `stop_filter`, `routes_filter`, `stoptrips_filter`, `trip_stops_filter`, `realtime_filter`
- Produces parsers: `parse_stop`, `parse_routes`, `parse_stoptrips`, `parse_trip_stops`, `parse_realtime`

Every parser runs against a fixture in `test/fixtures/`, parsed **through its
own filter**, so the filters are covered too: a filter that drops a needed
field fails the test rather than the board.

- [ ] **Step 1: Write the failing test** at `test/test_at_api/test_main.cpp`

```cpp
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <string>

#include "at_api.h"

void setUp() {}
void tearDown() {}

namespace {
// Native tests run with the project root as the working directory.
std::string fixture(const char* path) {
  FILE* f = fopen(path, "rb");
  if (f == nullptr) return "";
  std::string out;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
  fclose(f);
  return out;
}

void load(const char* path, JsonDocument& doc, const JsonDocument& filter) {
  const std::string text = fixture(path);
  TEST_ASSERT_TRUE_MESSAGE(!text.empty(), "fixture missing - run pio test from the project root");
  const DeserializationError err =
      deserializeJson(doc, text, DeserializationOption::Filter(filter));
  TEST_ASSERT_FALSE_MESSAGE(err, err.c_str());
}
}  // namespace

void test_urls() {
  char u[256];
  TEST_ASSERT_TRUE(url_stop_by_code(u, sizeof u, "8213"));
  TEST_ASSERT_EQUAL_STRING(
      "https://api.at.govt.nz/gtfs/v3/stops?filter%5Bstop_code%5D=8213", u);

  TEST_ASSERT_TRUE(url_routes_by_short_name(u, sizeof u, "20"));
  TEST_ASSERT_EQUAL_STRING(
      "https://api.at.govt.nz/gtfs/v3/routes?filter%5Broute_short_name%5D=20", u);

  TEST_ASSERT_TRUE(url_rail_routes(u, sizeof u));
  TEST_ASSERT_EQUAL_STRING(
      "https://api.at.govt.nz/gtfs/v3/routes?filter%5Broute_type%5D=2", u);

  TEST_ASSERT_TRUE(url_stoptrips(u, sizeof u, "122-34ecc043", {2026, 9, 19}, 17, 3));
  TEST_ASSERT_EQUAL_STRING(
      "https://api.at.govt.nz/gtfs/v3/stops/122-34ecc043/stoptrips"
      "?filter%5Bdate%5D=2026-09-19&filter%5Bstart_hour%5D=17&filter%5Bhour_range%5D=3", u);

  TEST_ASSERT_TRUE(url_trip_stops(u, sizeof u, "20-02005-54000-2-89a317b8"));
  TEST_ASSERT_EQUAL_STRING(
      "https://api.at.govt.nz/gtfs/v3/trips/20-02005-54000-2-89a317b8/stops", u);

  const char* ids[] = {"a-1", "b-2"};
  TEST_ASSERT_TRUE(url_realtime(u, sizeof u, ids, 2));
  TEST_ASSERT_EQUAL_STRING(
      "https://api.at.govt.nz/realtime/legacy/tripupdates?tripid=a-1,b-2", u);

  char small[32];
  TEST_ASSERT_FALSE(url_realtime(small, sizeof small, ids, 2));  // refuses to truncate
}

void test_start_hour_zero_is_rejected() {
  // The API answers 400 for start_hour=0 (verified live 2026-09-19), so the
  // builder refuses it rather than letting the board ask.
  char u[256];
  TEST_ASSERT_FALSE(url_stoptrips(u, sizeof u, "122-34ecc043", {2026, 9, 19}, 0, 3));
  TEST_ASSERT_FALSE(url_stoptrips(u, sizeof u, "122-34ecc043", {2026, 9, 19}, 24, 3));
}

void test_parse_stop() {
  JsonDocument filter, doc;
  stop_filter(filter);
  load("test/fixtures/post-crl/stops_8213.json", doc, filter);
  StopInfo s{};
  TEST_ASSERT_TRUE(parse_stop(doc, &s));
  TEST_ASSERT_EQUAL_STRING("8213-7e021a72", s.stop_id);
  TEST_ASSERT_EQUAL_INT(0, s.location_type);

  JsonDocument doc2;
  load("test/fixtures/post-crl/stops_122.json", doc2, filter);
  TEST_ASSERT_TRUE(parse_stop(doc2, &s));
  TEST_ASSERT_EQUAL_STRING("122-34ecc043", s.stop_id);
  TEST_ASSERT_EQUAL_INT(1, s.location_type);  // a station
}

void test_an_unknown_stop_code_returns_an_empty_list_not_an_error() {
  JsonDocument filter, doc;
  stop_filter(filter);
  load("test/fixtures/post-crl/stops_unknown.json", doc, filter);
  StopInfo s{};
  TEST_ASSERT_FALSE(parse_stop(doc, &s));
}

void test_parse_routes() {
  JsonDocument filter, doc;
  routes_filter(filter);
  load("test/fixtures/routes_20.json", doc, filter);
  RouteInfo r[8];
  TEST_ASSERT_EQUAL_INT(1, parse_routes(doc, r, 8));
  TEST_ASSERT_EQUAL_STRING("20-202", r[0].route_id);
  TEST_ASSERT_EQUAL_STRING("20", r[0].short_name);
  TEST_ASSERT_EQUAL_INT(3, r[0].route_type);
  TEST_ASSERT_FALSE(r[0].has_color);  // AT omits route_color for buses

  JsonDocument doc2;
  load("test/fixtures/post-crl/routes_rail.json", doc2, filter);
  TEST_ASSERT_EQUAL_INT(8, parse_routes(doc2, r, 8));
  bool saw_ew = false, saw_huia = false;
  for (int i = 0; i < 8; i++) {
    if (strcmp(r[i].route_id, "E-W-201") == 0) {
      saw_ew = true;
      TEST_ASSERT_EQUAL_INT(2, r[i].route_type);
      TEST_ASSERT_TRUE(r[i].has_color);
      TEST_ASSERT_EQUAL_UINT8(0x97, r[i].color.r);
      TEST_ASSERT_EQUAL_UINT8(0xC9, r[i].color.g);
      TEST_ASSERT_EQUAL_UINT8(0x3D, r[i].color.b);
    }
    if (strcmp(r[i].route_id, "HUIA-404") == 0) {
      saw_huia = true;
      TEST_ASSERT_FALSE(r[i].has_color);  // #000000 is unusable on our ground
    }
  }
  TEST_ASSERT_TRUE(saw_ew);
  TEST_ASSERT_TRUE(saw_huia);
}

void test_parse_stoptrips() {
  JsonDocument filter, doc;
  stoptrips_filter(filter);
  load("test/fixtures/post-crl/stoptrips_kingsland.json", doc, filter);
  StopTripRow rows[48];
  const int n = parse_stoptrips(doc, rows, 48);
  TEST_ASSERT_EQUAL_INT(23, n);

  int dir0 = 0, dir1 = 0;
  for (int i = 0; i < n; i++) dir0 += rows[i].direction_id == 0, dir1 += rows[i].direction_id == 1;
  TEST_ASSERT_EQUAL_INT(12, dir0);
  TEST_ASSERT_EQUAL_INT(11, dir1);

  // First row, checked field by field: a station's rows carry the platform id.
  TEST_ASSERT_EQUAL_STRING("258-880001-51480-2-W118540-4f47826a", rows[0].trip_id);
  TEST_ASSERT_EQUAL_STRING("E-W-201", rows[0].route_id);
  TEST_ASSERT_EQUAL_STRING("9305-ef07ca76", rows[0].stop_id);
  TEST_ASSERT_EQUAL_INT(0, rows[0].direction_id);
  TEST_ASSERT_EQUAL_INT32(15 * 3600 + 11 * 60, rows[0].departure_s);
  TEST_ASSERT_EQUAL_INT(2026, rows[0].service_date.y);
  TEST_ASSERT_EQUAL_INT(9, rows[0].service_date.m);
  TEST_ASSERT_EQUAL_INT(13, rows[0].service_date.d);
}

void test_parse_stoptrips_respects_the_cap() {
  JsonDocument filter, doc;
  stoptrips_filter(filter);
  load("test/fixtures/post-crl/stoptrips_8213.json", doc, filter);
  StopTripRow rows[4];
  TEST_ASSERT_EQUAL_INT(4, parse_stoptrips(doc, rows, 4));
}

void test_parse_trip_stops_keeps_sequence_order() {
  JsonDocument filter, doc;
  trip_stops_filter(filter);
  load("test/fixtures/post-crl/trip_dir1_stops.json", doc, filter);
  TripStop stops[40];
  const int n = parse_trip_stops(doc, stops, 40);
  TEST_ASSERT_EQUAL_INT(27, n);
  TEST_ASSERT_EQUAL_STRING("9328", stops[0].stop_code);       // Swanson, first
  TEST_ASSERT_EQUAL_STRING("9304-dcb2ed75", stops[12].stop_id);  // Kingsland platform
  TEST_ASSERT_EQUAL_STRING("9001", stops[16].stop_code);      // Waitemata platform 1
  TEST_ASSERT_EQUAL_STRING("133-08da14b5", stops[16].parent_station);
}

void test_parse_realtime() {
  JsonDocument filter, doc;
  realtime_filter(filter);
  load("test/fixtures/realtime_tripupdates_only.json", doc, filter);
  RtEntity ents[16];
  const int n = parse_realtime(doc, ents, 16);
  TEST_ASSERT_EQUAL_INT(6, n);

  TEST_ASSERT_EQUAL_STRING("20-02005-61200-2-89a317b8", ents[0].trip_id);
  TEST_ASSERT_TRUE(ents[0].has_delay);
  TEST_ASSERT_EQUAL_INT32(-427, ents[0].delay);  // seven minutes early, observed
  TEST_ASSERT_FALSE(ents[0].cancelled);
  TEST_ASSERT_TRUE(ents[0].has_stu_departure);
  TEST_ASSERT_EQUAL_STRING("1060-00b64ee7", ents[0].stu_stop_id);
  TEST_ASSERT_EQUAL_INT32(-427, ents[0].stu_departure_delay);

  // This one's stop_time_update IS our Kingsland platform, and disagrees with
  // the trip-level delay: +66 against -28.
  TEST_ASSERT_EQUAL_STRING("247-810023-61200-2-9731921-77638b11", ents[4].trip_id);
  TEST_ASSERT_EQUAL_INT32(-28, ents[4].delay);
  TEST_ASSERT_EQUAL_STRING("9304-dcb2ed75", ents[4].stu_stop_id);
  TEST_ASSERT_EQUAL_INT32(66, ents[4].stu_departure_delay);

  // And this one has a stop_time_update with no departure at all.
  TEST_ASSERT_EQUAL_STRING("247-810007-60000-2-9729960-592012e4", ents[3].trip_id);
  TEST_ASSERT_FALSE(ents[3].has_stu_departure);
  TEST_ASSERT_EQUAL_INT32(-25, ents[3].delay);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_urls);
  RUN_TEST(test_start_hour_zero_is_rejected);
  RUN_TEST(test_parse_stop);
  RUN_TEST(test_an_unknown_stop_code_returns_an_empty_list_not_an_error);
  RUN_TEST(test_parse_routes);
  RUN_TEST(test_parse_stoptrips);
  RUN_TEST(test_parse_stoptrips_respects_the_cap);
  RUN_TEST(test_parse_trip_stops_keeps_sequence_order);
  RUN_TEST(test_parse_realtime);
  return UNITY_END();
}
```

- [ ] **Step 2: Run it to see it fail**

Run: `pio test -e native -f test_at_api`
Expected: build error, `at_api.h: No such file or directory`.

- [ ] **Step 3: Create `lib/core/src/at_api.h`**

```cpp
#pragma once
#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#include "color.h"
#include "nztime.h"

// URLs, response filters and parsers for the two AT APIs. Pure: no networking
// here, so every shape below is covered by a fixture in test/fixtures/.
// docs/at-api-notes.md records why each of these looks the way it does.

constexpr int TRIP_ID_LEN = 48;
constexpr int STOP_ID_LEN = 24;
constexpr int ROUTE_ID_LEN = 16;
constexpr int SHORT_NAME_LEN = 12;
constexpr int STOP_CODE_LEN = 12;

struct StopInfo {
  char stop_id[STOP_ID_LEN];
  int location_type;  // 0 stop, 1 station
};

struct RouteInfo {
  char route_id[ROUTE_ID_LEN];
  char short_name[SHORT_NAME_LEN];
  int route_type;  // 2 rail, 3 bus
  bool has_color;
  Rgb color;
};

struct StopTripRow {
  char trip_id[TRIP_ID_LEN];
  char route_id[ROUTE_ID_LEN];
  char stop_id[STOP_ID_LEN];  // the platform for a station's rows
  int8_t direction_id;
  CivilDate service_date;
  int32_t departure_s;  // seconds from the service day's noon minus 12h
};

struct TripStop {
  char stop_id[STOP_ID_LEN];
  char stop_code[STOP_CODE_LEN];
  char parent_station[STOP_ID_LEN];  // "" for stops with no parent
};

struct RtEntity {
  char trip_id[TRIP_ID_LEN];
  bool has_delay;
  int32_t delay;  // trip level, signed; negative is early
  bool cancelled;
  bool has_stu_departure;
  char stu_stop_id[STOP_ID_LEN];
  int32_t stu_departure_delay;
};

// All builders return false rather than emit a truncated URL.
bool url_stop_by_code(char* out, size_t n, const char* stop_code);
bool url_routes_by_short_name(char* out, size_t n, const char* short_name);
bool url_rail_routes(char* out, size_t n);
// start_hour must be 1..23: the API answers 400 for 0 (verified 2026-09-19).
bool url_stoptrips(char* out, size_t n, const char* stop_id, CivilDate date,
                   int start_hour, int hour_range);
bool url_trip_stops(char* out, size_t n, const char* trip_id);
bool url_realtime(char* out, size_t n, const char* const trip_ids[], int count);

void stop_filter(JsonDocument& f);
void routes_filter(JsonDocument& f);
void stoptrips_filter(JsonDocument& f);
void trip_stops_filter(JsonDocument& f);
void realtime_filter(JsonDocument& f);

bool parse_stop(const JsonDocument& doc, StopInfo* out);  // false when data is empty
int parse_routes(const JsonDocument& doc, RouteInfo out[], int cap);
int parse_stoptrips(const JsonDocument& doc, StopTripRow out[], int cap);
int parse_trip_stops(const JsonDocument& doc, TripStop out[], int cap);
int parse_realtime(const JsonDocument& doc, RtEntity out[], int cap);
```

- [ ] **Step 4: Create `lib/core/src/at_api.cpp`**

```cpp
#include "at_api.h"

#include <stdio.h>
#include <string.h>

namespace {

constexpr const char* GTFS = "https://api.at.govt.nz/gtfs/v3";
constexpr const char* REALTIME = "https://api.at.govt.nz/realtime/legacy";

bool put(char* out, size_t n, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

bool put(char* out, size_t n, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  const int written = vsnprintf(out, n, fmt, args);
  va_end(args);
  return written > 0 && static_cast<size_t>(written) < n;
}

void copy(char* dst, size_t size, const char* src) {
  if (src == nullptr) src = "";
  strncpy(dst, src, size - 1);
  dst[size - 1] = '\0';
}

}  // namespace

bool url_stop_by_code(char* out, size_t n, const char* stop_code) {
  return put(out, n, "%s/stops?filter%%5Bstop_code%%5D=%s", GTFS, stop_code);
}

bool url_routes_by_short_name(char* out, size_t n, const char* short_name) {
  return put(out, n, "%s/routes?filter%%5Broute_short_name%%5D=%s", GTFS, short_name);
}

bool url_rail_routes(char* out, size_t n) {
  return put(out, n, "%s/routes?filter%%5Broute_type%%5D=2", GTFS);
}

bool url_stoptrips(char* out, size_t n, const char* stop_id, CivilDate date,
                   int start_hour, int hour_range) {
  if (start_hour < 1 || start_hour > 23 || hour_range < 1) return false;
  char day[16];
  format_iso_date(date, day, sizeof day);
  return put(out, n,
             "%s/stops/%s/stoptrips?filter%%5Bdate%%5D=%s"
             "&filter%%5Bstart_hour%%5D=%d&filter%%5Bhour_range%%5D=%d",
             GTFS, stop_id, day, start_hour, hour_range);
}

bool url_trip_stops(char* out, size_t n, const char* trip_id) {
  return put(out, n, "%s/trips/%s/stops", GTFS, trip_id);
}

bool url_realtime(char* out, size_t n, const char* const trip_ids[], int count) {
  if (count <= 0) return false;
  int written = snprintf(out, n, "%s/tripupdates?tripid=", REALTIME);
  if (written <= 0 || static_cast<size_t>(written) >= n) return false;
  size_t used = static_cast<size_t>(written);
  for (int i = 0; i < count; i++) {
    const int more = snprintf(out + used, n - used, "%s%s", i == 0 ? "" : ",", trip_ids[i]);
    if (more <= 0 || used + static_cast<size_t>(more) >= n) return false;
    used += static_cast<size_t>(more);
  }
  return true;
}

void stop_filter(JsonDocument& f) {
  JsonObject a = f["data"][0]["attributes"].to<JsonObject>();
  a["stop_id"] = true;
  a["location_type"] = true;
}

void routes_filter(JsonDocument& f) {
  JsonObject a = f["data"][0]["attributes"].to<JsonObject>();
  a["route_id"] = true;
  a["route_short_name"] = true;
  a["route_type"] = true;
  a["route_color"] = true;
}

void stoptrips_filter(JsonDocument& f) {
  JsonObject a = f["data"][0]["attributes"].to<JsonObject>();
  a["trip_id"] = true;
  a["route_id"] = true;
  a["stop_id"] = true;
  a["direction_id"] = true;
  a["departure_time"] = true;
  a["service_date"] = true;
}

void trip_stops_filter(JsonDocument& f) {
  JsonObject a = f["data"][0]["attributes"].to<JsonObject>();
  a["stop_id"] = true;
  a["stop_code"] = true;
  a["parent_station"] = true;
}

void realtime_filter(JsonDocument& f) {
  JsonObject e = f["response"]["entity"][0].to<JsonObject>();
  JsonObject tu = e["trip_update"].to<JsonObject>();
  tu["trip"]["trip_id"] = true;
  tu["trip"]["schedule_relationship"] = true;
  tu["delay"] = true;
  // Kept whole: the API sends a single object where the docs promise an array,
  // so a shaped filter would drop one of the two forms.
  tu["stop_time_update"] = true;
}

bool parse_stop(const JsonDocument& doc, StopInfo* out) {
  JsonArrayConst rows = doc["data"].as<JsonArrayConst>();
  if (rows.isNull() || rows.size() == 0) return false;
  JsonObjectConst a = rows[0]["attributes"];
  copy(out->stop_id, sizeof out->stop_id, a["stop_id"] | "");
  out->location_type = a["location_type"] | 0;
  return out->stop_id[0] != '\0';
}

int parse_routes(const JsonDocument& doc, RouteInfo out[], int cap) {
  int n = 0;
  for (JsonObjectConst row : doc["data"].as<JsonArrayConst>()) {
    if (n >= cap) break;
    JsonObjectConst a = row["attributes"];
    RouteInfo& r = out[n];
    copy(r.route_id, sizeof r.route_id, a["route_id"] | "");
    copy(r.short_name, sizeof r.short_name, a["route_short_name"] | "");
    r.route_type = a["route_type"] | 0;
    r.has_color = parse_hex(a["route_color"] | static_cast<const char*>(nullptr), &r.color);
    if (r.route_id[0] != '\0') n++;
  }
  return n;
}

int parse_stoptrips(const JsonDocument& doc, StopTripRow out[], int cap) {
  int n = 0;
  for (JsonObjectConst row : doc["data"].as<JsonArrayConst>()) {
    if (n >= cap) break;
    JsonObjectConst a = row["attributes"];
    StopTripRow& r = out[n];
    copy(r.trip_id, sizeof r.trip_id, a["trip_id"] | "");
    copy(r.route_id, sizeof r.route_id, a["route_id"] | "");
    copy(r.stop_id, sizeof r.stop_id, a["stop_id"] | "");
    r.direction_id = static_cast<int8_t>(a["direction_id"] | -1);
    if (!parse_gtfs_time(a["departure_time"] | "", &r.departure_s)) continue;
    if (!parse_iso_date(a["service_date"] | "", &r.service_date)) continue;
    if (r.trip_id[0] != '\0') n++;
  }
  return n;
}

int parse_trip_stops(const JsonDocument& doc, TripStop out[], int cap) {
  int n = 0;
  // Order is positional - these rows carry no stop_sequence, so never sort them.
  for (JsonObjectConst row : doc["data"].as<JsonArrayConst>()) {
    if (n >= cap) break;
    JsonObjectConst a = row["attributes"];
    TripStop& s = out[n];
    copy(s.stop_id, sizeof s.stop_id, a["stop_id"] | "");
    copy(s.stop_code, sizeof s.stop_code, a["stop_code"] | "");
    copy(s.parent_station, sizeof s.parent_station, a["parent_station"] | "");
    if (s.stop_id[0] != '\0') n++;
  }
  return n;
}

int parse_realtime(const JsonDocument& doc, RtEntity out[], int cap) {
  int n = 0;
  for (JsonObjectConst ent : doc["response"]["entity"].as<JsonArrayConst>()) {
    if (n >= cap) break;
    JsonObjectConst tu = ent["trip_update"];
    if (tu.isNull()) continue;
    RtEntity& e = out[n];
    memset(&e, 0, sizeof e);
    copy(e.trip_id, sizeof e.trip_id, tu["trip"]["trip_id"] | "");
    e.has_delay = !tu["delay"].isNull();
    e.delay = tu["delay"] | 0;
    e.cancelled = (tu["trip"]["schedule_relationship"] | 0) == 3;

    // The docs promise an array; the API sends one object. Accept both, taking
    // the first element of an array.
    JsonVariantConst stu = tu["stop_time_update"];
    if (stu.is<JsonArrayConst>()) stu = stu[0];
    if (!stu.isNull()) {
      copy(e.stu_stop_id, sizeof e.stu_stop_id, stu["stop_id"] | "");
      JsonVariantConst dep = stu["departure"];
      if (!dep.isNull() && !dep["delay"].isNull()) {
        e.has_stu_departure = true;
        e.stu_departure_delay = dep["delay"] | 0;
      }
    }
    if (e.trip_id[0] != '\0') n++;
  }
  return n;
}
```

Add `#include <stdarg.h>` at the top of the file if the compiler asks for it.

- [ ] **Step 5: Run it to see it pass**

Run: `pio test -e native -f test_at_api`
Expected: `9 test cases: 9 succeeded`.

- [ ] **Step 6: Commit**

```bash
git add lib/core/src/at_api.h lib/core/src/at_api.cpp test/test_at_api/test_main.cpp
git commit -m "Parse the AT APIs against the captured fixtures

Every parser is exercised through its own ArduinoJson filter, so a filter that
drops a field the board needs fails a test instead of the board. The awkward
shapes from docs/at-api-notes.md are pinned: a stop_time_update that is an
object rather than an array, a bus route with no colour, HUIA's #000000, and
start_hour=0 which the API rejects outright.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 5: The live merge — direction, selection, board

**Files:**
- Create: `lib/core/src/live.h`, `lib/core/src/live.cpp`
- Test: `test/test_live/test_main.cpp`

**Interfaces:**
- Produces: `struct WatchConfig { const char* label; const char* stop_code; const char* route_short_name; const char* toward_stop_code; };`
- Produces: `enum class WatchState : uint8_t { Starting, Ok, CheckConfig, CheckKey, StopLookupGone };`
- Produces: `struct LiveRow`, `struct LiveWatch`, `struct Snapshot` (below)
- Produces: `const char* state_message(WatchState)`
- Produces: `bool trip_serves(const TripStop[], int, const char* our_stop_id, const char* target_code, const char* target_stop_id)`
- Produces: `int choose_direction(const bool present[2], const bool serves[2])` → 0, 1, `DIR_NONE`, `DIR_UNPROVABLE`
- Produces: `int select_rows(const StopTripRow[], int, int direction, const char* const route_ids[], int n_routes, int64_t now, LiveRow out[], int cap)`
- Produces: `void apply_realtime(LiveWatch&, const RtEntity[], int)`
- Produces: `int realtime_ids(const Snapshot&, int64_t now, const char* out[], int cap, int per_watch)`
- Produces: `struct FetchWindow { CivilDate date; int start_hour; int hour_range; };`
  and `int schedule_windows(const LocalTime& now, FetchWindow out[2]);`
- Produces: `Board build_board(const Snapshot&, const WatchConfig[], const char* location, int64_t now, uint8_t theme)`

- [ ] **Step 1: Write the failing test** at `test/test_live/test_main.cpp`

```cpp
#include <string.h>
#include <unity.h>

#include "live.h"

void setUp() {}
void tearDown() {}

namespace {
// Kingsland, 2026-09-13, from the post-CRL fixtures.
constexpr int64_t T_15_00 = 1789268400;  // 2026-09-13 15:00:00 NZST

TripStop stop(const char* id, const char* code, const char* parent) {
  TripStop s{};
  strncpy(s.stop_id, id, sizeof s.stop_id - 1);
  strncpy(s.stop_code, code, sizeof s.stop_code - 1);
  strncpy(s.parent_station, parent, sizeof s.parent_station - 1);
  return s;
}

StopTripRow row(const char* trip, const char* route, int dir, int32_t secs) {
  StopTripRow r{};
  strncpy(r.trip_id, trip, sizeof r.trip_id - 1);
  strncpy(r.route_id, route, sizeof r.route_id - 1);
  strncpy(r.stop_id, "9305-ef07ca76", sizeof r.stop_id - 1);
  r.direction_id = static_cast<int8_t>(dir);
  r.service_date = {2026, 9, 13};
  r.departure_s = secs;
  return r;
}
}  // namespace

void test_a_trip_serves_a_target_that_comes_after_us() {
  // The real dir-1 trip: Kingsland platform 9304 at index 12, Waitemata
  // platform 9001 (parent 133) at 16.
  const TripStop stops[] = {
      stop("9328-f6f84eac", "9328", "127-2affe079"),
      stop("9304-dcb2ed75", "9304", "122-34ecc043"),
      stop("9001-11111111", "9001", "133-08da14b5"),
  };
  TEST_ASSERT_TRUE(trip_serves(stops, 3, "9304-dcb2ed75", "133", "133-08da14b5"));
}

void test_a_trip_going_the_other_way_does_not_serve_it() {
  // The real dir-0 trip passes Waitemata BEFORE Kingsland, so it is no use.
  const TripStop stops[] = {
      stop("9004-22222222", "9004", "133-08da14b5"),
      stop("9305-ef07ca76", "9305", "122-34ecc043"),
      stop("9328-f6f84eac", "9328", "127-2affe079"),
  };
  TEST_ASSERT_FALSE(trip_serves(stops, 3, "9305-ef07ca76", "133", "133-08da14b5"));
}

void test_a_bus_target_matches_on_stop_code_alone() {
  const TripStop stops[] = {
      stop("8213-7e021a72", "8213", ""),
      stop("1060-00b64ee7", "1060", ""),
  };
  TEST_ASSERT_TRUE(trip_serves(stops, 2, "8213-7e021a72", "1060", "1060-00b64ee7"));
}

void test_a_trip_that_never_calls_at_our_stop_serves_nothing() {
  const TripStop stops[] = {stop("1111-aaaa", "1111", ""), stop("1060-00b64ee7", "1060", "")};
  TEST_ASSERT_FALSE(trip_serves(stops, 2, "8213-7e021a72", "1060", "1060-00b64ee7"));
}

void test_choose_direction() {
  const bool both[2] = {true, true};
  const bool one_serves[2] = {false, true};
  TEST_ASSERT_EQUAL_INT(1, choose_direction(both, one_serves));

  const bool zero_serves[2] = {true, false};
  TEST_ASSERT_EQUAL_INT(0, choose_direction(both, zero_serves));

  // Both ways run and neither reaches the target: the config is wrong.
  const bool neither[2] = {false, false};
  TEST_ASSERT_EQUAL_INT(DIR_UNPROVABLE, choose_direction(both, neither));

  // Only one direction is running and it does not go our way: nothing is due,
  // which is an empty board, not a misconfiguration.
  const bool only_zero[2] = {true, false};
  TEST_ASSERT_EQUAL_INT(DIR_NONE, choose_direction(only_zero, neither));
}

void test_select_rows_filters_by_direction_and_route() {
  const StopTripRow rows[] = {
      row("a", "E-W-201", 0, 15 * 3600 + 11 * 60),
      row("b", "E-W-201", 1, 15 * 3600 + 13 * 60),
      row("c", "O-W-201", 1, 15 * 3600 + 20 * 60),
      row("d", "E-W-201", 1, 15 * 3600 + 30 * 60),
  };
  LiveRow out[8];
  TEST_ASSERT_EQUAL_INT(3, select_rows(rows, 4, 1, nullptr, 0, T_15_00, out, 8));
  TEST_ASSERT_EQUAL_STRING("b", out[0].trip_id);

  const char* only_ew[] = {"E-W-201"};
  TEST_ASSERT_EQUAL_INT(2, select_rows(rows, 4, 1, only_ew, 1, T_15_00, out, 8));
  TEST_ASSERT_EQUAL_STRING("b", out[0].trip_id);
  TEST_ASSERT_EQUAL_STRING("d", out[1].trip_id);
  TEST_ASSERT_EQUAL_INT64(T_15_00 + 13 * 60, out[0].sched_epoch);
}

void test_select_rows_keeps_recent_departures_because_delays_can_be_large() {
  // Scheduled 10 minutes ago, but a bus can run 7 minutes late; dropping it
  // here would lose a service that is still to come.
  const StopTripRow rows[] = {row("a", "E-W-201", 1, 14 * 3600 + 50 * 60),
                              row("b", "E-W-201", 1, 13 * 3600)};
  LiveRow out[4];
  TEST_ASSERT_EQUAL_INT(1, select_rows(rows, 2, 1, nullptr, 0, T_15_00, out, 4));
  TEST_ASSERT_EQUAL_STRING("a", out[0].trip_id);
}

void test_apply_realtime_prefers_our_own_stop_time_update() {
  LiveWatch w{};
  w.n_rows = 2;
  strncpy(w.rows[0].trip_id, "t1", sizeof w.rows[0].trip_id - 1);
  strncpy(w.rows[0].stop_id, "9304-dcb2ed75", sizeof w.rows[0].stop_id - 1);
  strncpy(w.rows[1].trip_id, "t2", sizeof w.rows[1].trip_id - 1);
  strncpy(w.rows[1].stop_id, "9304-dcb2ed75", sizeof w.rows[1].stop_id - 1);

  RtEntity ents[3]{};
  strncpy(ents[0].trip_id, "t1", sizeof ents[0].trip_id - 1);
  ents[0].has_delay = true;
  ents[0].delay = -28;
  ents[0].has_stu_departure = true;
  strncpy(ents[0].stu_stop_id, "9304-dcb2ed75", sizeof ents[0].stu_stop_id - 1);
  ents[0].stu_departure_delay = 66;

  strncpy(ents[1].trip_id, "t2", sizeof ents[1].trip_id - 1);
  ents[1].has_delay = true;
  ents[1].delay = -427;
  ents[1].has_stu_departure = true;
  strncpy(ents[1].stu_stop_id, "1060-00b64ee7", sizeof ents[1].stu_stop_id - 1);
  ents[1].stu_departure_delay = 999;

  // A trip we never asked about: the tripid filter is inexact.
  strncpy(ents[2].trip_id, "stranger", sizeof ents[2].trip_id - 1);
  ents[2].has_delay = true;
  ents[2].delay = 12345;

  apply_realtime(w, ents, 3);
  TEST_ASSERT_TRUE(w.rows[0].has_rt);
  TEST_ASSERT_EQUAL_INT32(66, w.rows[0].delay);     // our stop wins
  TEST_ASSERT_EQUAL_INT32(-427, w.rows[1].delay);   // someone else's is ignored
}

void test_apply_realtime_marks_cancellations() {
  LiveWatch w{};
  w.n_rows = 1;
  strncpy(w.rows[0].trip_id, "t1", sizeof w.rows[0].trip_id - 1);
  RtEntity e{};
  strncpy(e.trip_id, "t1", sizeof e.trip_id - 1);
  e.cancelled = true;
  apply_realtime(w, &e, 1);
  TEST_ASSERT_TRUE(w.rows[0].cancelled);
}

void test_build_board_turns_a_snapshot_into_what_the_screen_draws() {
  const WatchConfig cfg[] = {
      {"to Wynyard Quarter", "8213", "20", "1060"},
      {"to Waitemata", "122", "", "133"},
  };
  Snapshot s{};
  s.n_watches = 2;
  s.last_ok = T_15_00 - 10;
  s.poll_interval_s = 30;

  s.watches[0].state = WatchState::Ok;
  s.watches[0].kind = Kind::Bus;
  strncpy(s.watches[0].badge, "20", sizeof s.watches[0].badge - 1);
  s.watches[0].n_rows = 2;
  s.watches[0].rows[0].sched_epoch = T_15_00 + 240;
  s.watches[0].rows[0].has_rt = true;
  s.watches[0].rows[0].delay = 60;  // a minute late: eta 300
  s.watches[0].rows[1].sched_epoch = T_15_00 + 1020;

  s.watches[1].state = WatchState::CheckConfig;
  s.watches[1].kind = Kind::Train;
  strncpy(s.watches[1].badge, "E-W", sizeof s.watches[1].badge - 1);
  s.watches[1].n_rows = 1;
  s.watches[1].rows[0].sched_epoch = T_15_00 + 420;

  const Board b = build_board(s, cfg, "Kingsland", T_15_00, 0);
  TEST_ASSERT_EQUAL_UINT8(2, b.n_watches);
  TEST_ASSERT_EQUAL_STRING("Kingsland", b.location);
  TEST_ASSERT_EQUAL_STRING("15:00", b.clock);

  TEST_ASSERT_EQUAL_STRING("20", b.watches[0].badge);
  TEST_ASSERT_EQUAL_STRING("to Wynyard Quarter", b.watches[0].headsign);
  TEST_ASSERT_EQUAL_STRING("", b.watches[0].message);
  TEST_ASSERT_EQUAL_UINT8(2, b.watches[0].n_deps);
  TEST_ASSERT_EQUAL_INT32(300, b.watches[0].next()->eta_s);
  TEST_ASSERT_TRUE(b.watches[0].next()->live);
  TEST_ASSERT_EQUAL_INT32(1020, b.watches[0].following()->eta_s);
  TEST_ASSERT_FALSE(b.watches[0].following()->live);

  // A watch that cannot prove its direction shows a message and no times.
  TEST_ASSERT_EQUAL_STRING("check config", b.watches[1].message);
  TEST_ASSERT_EQUAL_UINT8(0, b.watches[1].n_deps);
}

void test_build_board_drops_departures_that_have_left() {
  const WatchConfig cfg[] = {{"to Wynyard Quarter", "8213", "20", "1060"}};
  Snapshot s{};
  s.n_watches = 1;
  s.last_ok = T_15_00;
  s.poll_interval_s = 30;
  s.watches[0].state = WatchState::Ok;
  s.watches[0].n_rows = 2;
  s.watches[0].rows[0].sched_epoch = T_15_00 - 120;  // gone
  s.watches[0].rows[1].sched_epoch = T_15_00 + 300;

  const Board b = build_board(s, cfg, "Kingsland", T_15_00, 0);
  TEST_ASSERT_EQUAL_UINT8(1, b.watches[0].n_deps);
  TEST_ASSERT_EQUAL_INT32(300, b.watches[0].next()->eta_s);
}

void test_build_board_orders_by_the_time_the_service_will_actually_leave() {
  const WatchConfig cfg[] = {{"to Wynyard Quarter", "8213", "20", "1060"}};
  Snapshot s{};
  s.n_watches = 1;
  s.last_ok = T_15_00;
  s.poll_interval_s = 30;
  s.watches[0].state = WatchState::Ok;
  s.watches[0].n_rows = 2;
  s.watches[0].rows[0].sched_epoch = T_15_00 + 120;
  s.watches[0].rows[0].has_rt = true;
  s.watches[0].rows[0].delay = 600;  // ten minutes late, so it is now second
  s.watches[0].rows[1].sched_epoch = T_15_00 + 300;

  const Board b = build_board(s, cfg, "Kingsland", T_15_00, 0);
  TEST_ASSERT_EQUAL_INT32(300, b.watches[0].next()->eta_s);
  TEST_ASSERT_EQUAL_INT32(720, b.watches[0].following()->eta_s);
}

void test_staleness_is_measured_against_the_polling_interval() {
  const WatchConfig cfg[] = {{"to Wynyard Quarter", "8213", "20", "1060"}};
  Snapshot s{};
  s.n_watches = 1;
  s.watches[0].state = WatchState::Ok;
  s.poll_interval_s = 30;

  s.last_ok = T_15_00 - 30;  // a poll due now is not late
  TEST_ASSERT_EQUAL_INT32(0, build_board(s, cfg, "K", T_15_00, 0).stale_s);

  s.last_ok = T_15_00 - 270;  // four minutes overdue
  const Board b = build_board(s, cfg, "K", T_15_00, 0);
  TEST_ASSERT_EQUAL_INT32(240, b.stale_s);
  TEST_ASSERT_TRUE(b.is_stale());

  s.last_ok = 0;  // nothing has ever arrived: Starting says so instead
  TEST_ASSERT_EQUAL_INT32(0, build_board(s, cfg, "K", T_15_00, 0).stale_s);
}

void test_realtime_ids_asks_only_about_services_still_to_come() {
  Snapshot s{};
  s.n_watches = 1;
  s.watches[0].state = WatchState::Ok;
  s.watches[0].n_rows = 3;
  strncpy(s.watches[0].rows[0].trip_id, "gone", sizeof s.watches[0].rows[0].trip_id - 1);
  s.watches[0].rows[0].sched_epoch = T_15_00 - 3600;
  strncpy(s.watches[0].rows[1].trip_id, "soon", sizeof s.watches[0].rows[1].trip_id - 1);
  s.watches[0].rows[1].sched_epoch = T_15_00 + 300;
  strncpy(s.watches[0].rows[2].trip_id, "later", sizeof s.watches[0].rows[2].trip_id - 1);
  s.watches[0].rows[2].sched_epoch = T_15_00 + 900;

  const char* ids[8];
  TEST_ASSERT_EQUAL_INT(2, realtime_ids(s, T_15_00, ids, 8, 6));
  TEST_ASSERT_EQUAL_STRING("soon", ids[0]);
  TEST_ASSERT_EQUAL_STRING("later", ids[1]);

  TEST_ASSERT_EQUAL_INT(1, realtime_ids(s, T_15_00, ids, 8, 1));  // per-watch cap
}

void test_schedule_windows_cover_three_hours_from_now() {
  FetchWindow w[2];
  LocalTime t{};
  t.y = 2026;
  t.m = 9;
  t.d = 19;
  t.hour = 17;
  TEST_ASSERT_EQUAL_INT(1, schedule_windows(t, w));
  TEST_ASSERT_EQUAL_INT(2026, w[0].date.y);
  TEST_ASSERT_EQUAL_INT(19, w[0].date.d);
  TEST_ASSERT_EQUAL_INT(17, w[0].start_hour);
  TEST_ASSERT_EQUAL_INT(3, w[0].hour_range);
}

void test_schedule_windows_split_at_midnight_because_the_api_clamps() {
  FetchWindow w[2];
  LocalTime t{};
  t.y = 2026;
  t.m = 9;
  t.d = 19;
  t.hour = 23;
  TEST_ASSERT_EQUAL_INT(2, schedule_windows(t, w));
  TEST_ASSERT_EQUAL_INT(19, w[0].date.d);
  TEST_ASSERT_EQUAL_INT(23, w[0].start_hour);
  TEST_ASSERT_EQUAL_INT(1, w[0].hour_range);
  TEST_ASSERT_EQUAL_INT(20, w[1].date.d);  // next service date
  TEST_ASSERT_EQUAL_INT(1, w[1].start_hour);  // never 0: the API rejects it
  TEST_ASSERT_EQUAL_INT(2, w[1].hour_range);
}

void test_schedule_windows_never_ask_for_hour_zero() {
  FetchWindow w[2];
  LocalTime t{};
  t.y = 2026;
  t.m = 9;
  t.d = 20;
  t.hour = 0;
  TEST_ASSERT_EQUAL_INT(1, schedule_windows(t, w));
  TEST_ASSERT_EQUAL_INT(20, w[0].date.d);
  TEST_ASSERT_EQUAL_INT(1, w[0].start_hour);
  TEST_ASSERT_EQUAL_INT(3, w[0].hour_range);
}

void test_schedule_windows_roll_over_a_month_end() {
  FetchWindow w[2];
  LocalTime t{};
  t.y = 2026;
  t.m = 9;
  t.d = 30;
  t.hour = 23;
  TEST_ASSERT_EQUAL_INT(2, schedule_windows(t, w));
  TEST_ASSERT_EQUAL_INT(10, w[1].date.m);
  TEST_ASSERT_EQUAL_INT(1, w[1].date.d);
}

void test_state_messages() {
  TEST_ASSERT_EQUAL_STRING("", state_message(WatchState::Ok));
  TEST_ASSERT_EQUAL_STRING("check config", state_message(WatchState::CheckConfig));
  TEST_ASSERT_EQUAL_STRING("check API key", state_message(WatchState::CheckKey));
  TEST_ASSERT_EQUAL_STRING("starting", state_message(WatchState::Starting));
  TEST_ASSERT_EQUAL_STRING("stop lookup gone", state_message(WatchState::StopLookupGone));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_trip_serves_a_target_that_comes_after_us);
  RUN_TEST(test_a_trip_going_the_other_way_does_not_serve_it);
  RUN_TEST(test_a_bus_target_matches_on_stop_code_alone);
  RUN_TEST(test_a_trip_that_never_calls_at_our_stop_serves_nothing);
  RUN_TEST(test_choose_direction);
  RUN_TEST(test_select_rows_filters_by_direction_and_route);
  RUN_TEST(test_select_rows_keeps_recent_departures_because_delays_can_be_large);
  RUN_TEST(test_apply_realtime_prefers_our_own_stop_time_update);
  RUN_TEST(test_apply_realtime_marks_cancellations);
  RUN_TEST(test_build_board_turns_a_snapshot_into_what_the_screen_draws);
  RUN_TEST(test_build_board_drops_departures_that_have_left);
  RUN_TEST(test_build_board_orders_by_the_time_the_service_will_actually_leave);
  RUN_TEST(test_staleness_is_measured_against_the_polling_interval);
  RUN_TEST(test_realtime_ids_asks_only_about_services_still_to_come);
  RUN_TEST(test_schedule_windows_cover_three_hours_from_now);
  RUN_TEST(test_schedule_windows_split_at_midnight_because_the_api_clamps);
  RUN_TEST(test_schedule_windows_never_ask_for_hour_zero);
  RUN_TEST(test_schedule_windows_roll_over_a_month_end);
  RUN_TEST(test_state_messages);
  return UNITY_END();
}
```

- [ ] **Step 2: Run it to see it fail**

Run: `pio test -e native -f test_live`
Expected: build error, `live.h: No such file or directory`.

- [ ] **Step 3: Create `lib/core/src/live.h`**

```cpp
#pragma once
#include <stdint.h>

#include "at_api.h"
#include "model.h"

// Everything between "the API answered" and "the screen draws". Pure, so the
// rules that decide what the board claims are tested on a laptop.

struct WatchConfig {
  const char* label;             // headsign line, e.g. "to Wynyard Quarter"
  const char* stop_code;         // the number on the pole, e.g. "8213"
  const char* route_short_name;  // "" means any route (spec 3)
  const char* toward_stop_code;  // the stop being travelled toward (spec 3a)
};

enum class WatchState : uint8_t {
  Starting,        // nothing fetched yet
  Ok,
  CheckConfig,     // direction cannot be derived, or the stop code is unknown
  CheckKey,        // 401
  StopLookupGone,  // filter[stop_code] started 400ing (spec 8)
};

const char* state_message(WatchState s);  // "" for Ok

constexpr int MAX_ROWS = 16;      // scheduled departures kept per watch
constexpr int DIR_NONE = -1;      // nothing runs our way in this window
constexpr int DIR_UNPROVABLE = -2;

struct LiveRow {
  char trip_id[TRIP_ID_LEN];
  char stop_id[STOP_ID_LEN];
  int64_t sched_epoch;
  bool has_rt;
  int32_t delay;
  bool cancelled;
};

struct LiveWatch {
  WatchState state;
  Kind kind;
  char badge[8];
  bool has_route_color;
  Rgb route_color;
  LiveRow rows[MAX_ROWS];
  uint8_t n_rows;
};

struct Snapshot {
  LiveWatch watches[MAX_WATCHES];
  uint8_t n_watches;
  int64_t last_ok;         // epoch of the last successful fetch, 0 if never
  int32_t poll_interval_s; // what the fetcher is currently aiming for
};

// Does this trip call at our stop and then reach the target? Platforms carry a
// parent_station, so a station target matches either the code or the parent.
bool trip_serves(const TripStop stops[], int n, const char* our_stop_id,
                 const char* target_code, const char* target_stop_id);

// Which direction_id to believe, given which directions have trips in the
// window and which of them reach the target.
int choose_direction(const bool present[2], const bool serves[2]);

int select_rows(const StopTripRow rows[], int n, int direction,
                const char* const route_ids[], int n_routes, int64_t now,
                LiveRow out[], int cap);

// Applies delays and cancellations to rows whose trip_id we asked for; entities
// for any other trip are discarded (the tripid filter is inexact).
void apply_realtime(LiveWatch& w, const RtEntity ents[], int n);

// Trip ids worth asking realtime about: those still to come, at most
// per_watch from each watch.
int realtime_ids(const Snapshot& s, int64_t now, const char* out[], int cap, int per_watch);

struct FetchWindow {
  CivilDate date;
  int start_hour;  // always 1..23
  int hour_range;
};

// The stoptrips request(s) covering the next three hours. hour_range is clamped
// to the service day by the API, so a window crossing midnight needs a second
// request against the next date - and start_hour is never 0, which the API
// rejects with 400. Returns 1 or 2.
int schedule_windows(const LocalTime& now, FetchWindow out[2]);

Board build_board(const Snapshot& s, const WatchConfig cfg[], const char* location,
                  int64_t now, uint8_t theme);
```

- [ ] **Step 4: Create `lib/core/src/live.cpp`**

```cpp
#include "live.h"

#include <string.h>

namespace {

// A service scheduled this long ago may still be ahead of us: delays of -427 s
// are real, and so are late ones.
constexpr int64_t KEEP_PAST_S = 1800;

struct Candidate {
  int32_t eta_s;
  bool live;
  bool cancelled;
};

void sort_candidates(Candidate c[], int n) {
  for (int i = 1; i < n; i++) {  // insertion sort: stable, n is tiny
    const Candidate v = c[i];
    int j = i - 1;
    while (j >= 0 && c[j].eta_s > v.eta_s) {
      c[j + 1] = c[j];
      j--;
    }
    c[j + 1] = v;
  }
}

}  // namespace

const char* state_message(WatchState s) {
  switch (s) {
    case WatchState::Ok: return "";
    case WatchState::Starting: return "starting";
    case WatchState::CheckConfig: return "check config";
    case WatchState::CheckKey: return "check API key";
    case WatchState::StopLookupGone: return "stop lookup gone";
  }
  return "";
}

bool trip_serves(const TripStop stops[], int n, const char* our_stop_id,
                 const char* target_code, const char* target_stop_id) {
  int ours = -1;
  for (int i = 0; i < n; i++) {
    if (strcmp(stops[i].stop_id, our_stop_id) == 0) {
      ours = i;
      break;
    }
  }
  if (ours < 0) return false;
  for (int i = ours + 1; i < n; i++) {
    if (strcmp(stops[i].stop_code, target_code) == 0) return true;
    if (target_stop_id != nullptr && target_stop_id[0] != '\0' &&
        strcmp(stops[i].parent_station, target_stop_id) == 0) {
      return true;
    }
  }
  return false;
}

int choose_direction(const bool present[2], const bool serves[2]) {
  for (int d = 0; d < 2; d++) {
    if (present[d] && serves[d]) return d;
  }
  // Both directions are running and neither reaches the target: the watch is
  // pointed at a stop this service does not serve.
  if (present[0] && present[1]) return DIR_UNPROVABLE;
  return DIR_NONE;
}

int select_rows(const StopTripRow rows[], int n, int direction,
                const char* const route_ids[], int n_routes, int64_t now,
                LiveRow out[], int cap) {
  int count = 0;
  for (int i = 0; i < n && count < cap; i++) {
    const StopTripRow& r = rows[i];
    if (r.direction_id != direction) continue;
    if (n_routes > 0) {
      bool wanted = false;
      for (int k = 0; k < n_routes; k++) {
        if (strcmp(r.route_id, route_ids[k]) == 0) {
          wanted = true;
          break;
        }
      }
      if (!wanted) continue;
    }
    const int64_t sched = gtfs_epoch(r.service_date, r.departure_s);
    if (sched < now - KEEP_PAST_S) continue;
    LiveRow& o = out[count];
    memset(&o, 0, sizeof o);
    strncpy(o.trip_id, r.trip_id, sizeof o.trip_id - 1);
    strncpy(o.stop_id, r.stop_id, sizeof o.stop_id - 1);
    o.sched_epoch = sched;
    count++;
  }
  for (int i = 1; i < count; i++) {  // by scheduled time, stable
    const LiveRow v = out[i];
    int j = i - 1;
    while (j >= 0 && out[j].sched_epoch > v.sched_epoch) {
      out[j + 1] = out[j];
      j--;
    }
    out[j + 1] = v;
  }
  return count;
}

void apply_realtime(LiveWatch& w, const RtEntity ents[], int n) {
  for (int i = 0; i < w.n_rows; i++) {
    LiveRow& row = w.rows[i];
    for (int e = 0; e < n; e++) {
      const RtEntity& ent = ents[e];
      if (strcmp(ent.trip_id, row.trip_id) != 0) continue;  // re-filter: the
                                                            // tripid query is inexact
      row.cancelled = ent.cancelled;
      // The per-stop update usually describes the vehicle's next stop, not
      // ours; prefer it only when it really is ours.
      if (ent.has_stu_departure && strcmp(ent.stu_stop_id, row.stop_id) == 0) {
        row.delay = ent.stu_departure_delay;
        row.has_rt = true;
      } else if (ent.has_delay) {
        row.delay = ent.delay;
        row.has_rt = true;
      }
      break;
    }
  }
}

int realtime_ids(const Snapshot& s, int64_t now, const char* out[], int cap, int per_watch) {
  int count = 0;
  for (int i = 0; i < s.n_watches && count < cap; i++) {
    const LiveWatch& w = s.watches[i];
    if (w.state != WatchState::Ok) continue;
    int taken = 0;
    for (int r = 0; r < w.n_rows && count < cap && taken < per_watch; r++) {
      const LiveRow& row = w.rows[r];
      if (row.trip_id[0] == '\0') continue;
      if (row.sched_epoch + row.delay < now - 60) continue;
      out[count++] = row.trip_id;
      taken++;
    }
  }
  return count;
}

int schedule_windows(const LocalTime& now, FetchWindow out[2]) {
  constexpr int LOOKAHEAD_H = 3;
  // Hour 0 is rejected by the API, so the first hour we can ask for is 1; a
  // departure between 00:00 and 00:59 is simply not fetchable.
  const int start = now.hour < 1 ? 1 : now.hour;
  const CivilDate today{now.y, now.m, now.d};
  if (start + LOOKAHEAD_H <= 24) {
    out[0] = {today, start, LOOKAHEAD_H};
    return 1;
  }
  out[0] = {today, start, 24 - start};
  out[1] = {civil_from_days(days_from_civil(today.y, today.m, today.d) + 1), 1,
            start + LOOKAHEAD_H - 24};
  return 2;
}

Board build_board(const Snapshot& s, const WatchConfig cfg[], const char* location,
                  int64_t now, uint8_t theme) {
  Board b;
  memset(&b, 0, sizeof b);
  b.n_watches = s.n_watches;
  b.theme = theme;
  board_set_location(&b, location);
  format_clock(now, b.clock, sizeof b.clock);

  // Overdue, not elapsed: at the slow cadence a two-minute gap is normal.
  if (s.last_ok > 0) {
    const int64_t overdue = now - s.last_ok - s.poll_interval_s;
    b.stale_s = overdue > 0 ? static_cast<int32_t>(overdue) : 0;
  }

  for (int i = 0; i < s.n_watches; i++) {
    const LiveWatch& lw = s.watches[i];
    Watch& w = b.watches[i];
    watch_init(&w, lw.badge, cfg[i].label, lw.kind, nullptr);
    w.has_route_color = lw.has_route_color;
    w.route_color = lw.route_color;
    watch_set_message(&w, state_message(lw.state));
    if (lw.state != WatchState::Ok) continue;

    Candidate cand[MAX_ROWS];
    int n = 0;
    for (int r = 0; r < lw.n_rows; r++) {
      const LiveRow& row = lw.rows[r];
      const int64_t eta = row.sched_epoch + (row.has_rt ? row.delay : 0) - now;
      if (eta < 0) continue;  // it has left; the next one is promoted
      cand[n].eta_s = static_cast<int32_t>(eta);
      cand[n].live = row.has_rt;
      cand[n].cancelled = row.cancelled;
      n++;
    }
    sort_candidates(cand, n);
    for (int k = 0; k < n; k++) {
      if (!w.add({cand[k].eta_s, cand[k].live, cand[k].cancelled})) break;
    }
  }
  return b;
}
```

- [ ] **Step 5: Run it to see it pass**

Run: `pio test -e native -f test_live`
Expected: `19 test cases: 19 succeeded`.

- [ ] **Step 6: Commit**

```bash
git add lib/core/src/live.h lib/core/src/live.cpp test/test_live/test_main.cpp
git commit -m "Merge schedule, direction and delays into a Board, on a laptop

The rules that decide what the board claims are the ones worth testing off the
device: which direction actually reaches your destination, which delay to
believe when the per-stop update is about someone else's stop, what to do with
a trip the realtime feed threw in unasked, and when 'stale' is honest rather
than merely overdue by a polling interval.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 6: HTTPS to AT, with a pinned root

**Files:**
- Create: `src/secrets.example.h`, `src/watch_config.h`, `src/at_ca.h`, `src/at_client.h`, `src/at_client.cpp`
- Modify: `platformio.ini`, `src/main.cpp`

**Interfaces:**
- Produces: `constexpr int AT_TRANSPORT_ERROR = -1; constexpr int AT_PARSE_ERROR = -2;`
- Produces: `int at_get(const char* url, JsonDocument& doc, const JsonDocument& filter);`
  → HTTP status (200/401/404/429/5xx) on a reply, or one of the negatives.
- Produces: env `esp32` (live) and `esp32_demo`; `WATCHES`, `N_WATCHES`, `LOCATION` in `src/watch_config.h`

- [ ] **Step 1: Split the environments** in `platformio.ini`. Remove
`-DDEMO_MODE=1` from `[env:esp32]`'s build flags and append:

```ini
[env:esp32_demo]
extends = env:esp32
build_flags = ${env:esp32.build_flags} -DDEMO_MODE=1
```

Add `-Wall -Wextra` to `[env:native]`'s `build_flags`. Update the header
comment to list all three commands.

- [ ] **Step 2: Create `src/secrets.example.h`**

```cpp
// Copy to src/secrets.h and fill in. secrets.h is gitignored - never commit it.
#pragma once

#define WIFI_SSID     "your-network"
#define WIFI_PASSWORD "your-password"

// Free from https://dev-portal.at.govt.nz/ - subscribe to both the GTFS and
// the Realtime products with the same key.
#define AT_API_KEY    "your-key"
```

- [ ] **Step 3: Create `src/watch_config.h`**

```cpp
#pragma once
#include "live.h"

// The watches, until the setup portal lands (spec 3). Stop codes are the
// numbers on the pole; nothing here is secret.
//
// route_short_name "" means any route, which is what carries a rail watch
// through a line rename - see docs/at-api-notes.md on the CRL changeover.
// toward_stop_code is the stop you are travelling toward, never a direction:
// direction is derived at every refresh (spec 3a).
static const char* const LOCATION = "Kingsland";

static const WatchConfig WATCHES[] = {
    {"to Wynyard Quarter", "8213", "20", "1060"},
    {"to Waitemata", "122", "", "133"},  // no macron: the panel font is ASCII
};
static constexpr int N_WATCHES = sizeof WATCHES / sizeof WATCHES[0];
```

- [ ] **Step 4: Create `src/at_ca.h`.** Generate it rather than typing it:

```bash
echo | openssl s_client -connect api.at.govt.nz:443 -servername api.at.govt.nz -showcerts 2>/dev/null \
  | awk '/BEGIN CERT/{n++} n==3' | sed -n '/BEGIN/,/END/p' > /tmp/g2.pem
openssl x509 -in /tmp/g2.pem -noout -subject -fingerprint -sha256
```

The subject must be `CN=DigiCert Global Root G2` and the SHA256 fingerprint
exactly
`CB:3C:CB:B7:60:31:E5:E0:13:8F:8D:D3:9A:23:F9:DE:47:FF:C3:5E:43:C1:14:4C:EA:27:D4:6A:5A:B1:CB:5F`.
If either differs, stop and report — do not pin an unverified certificate.
Then write the file with the PEM inlined:

```cpp
#pragma once

// DigiCert Global Root G2, the root of api.at.govt.nz's chain. Pinned rather
// than trusting a bundle, and the root rather than the leaf: the leaf expires
// in March 2027 and is reissued, this root runs to 2038.
//
// Verify with:
//   openssl s_client -connect api.at.govt.nz:443 -servername api.at.govt.nz -showcerts
// SHA256 CB:3C:CB:B7:60:31:E5:E0:13:8F:8D:D3:9A:23:F9:DE:47:FF:C3:5E:43:C1:14:4C:EA:27:D4:6A:5A:B1:CB:5F
static const char AT_ROOT_CA[] PROGMEM = R"CERT(
-----BEGIN CERTIFICATE-----
...the PEM body from the command above...
-----END CERTIFICATE-----
)CERT";
```

- [ ] **Step 5: Create `src/at_client.h`**

```cpp
#pragma once
#include <ArduinoJson.h>

// One GET against the AT APIs: pinned TLS, the subscription-key header, the
// chunked/content-length branch, and a filtered streaming parse.

constexpr int AT_TRANSPORT_ERROR = -1;  // DNS, TLS, socket, timeout
constexpr int AT_PARSE_ERROR = -2;      // reply arrived but would not parse

// Returns the HTTP status when the server replied, otherwise one of the
// negatives above. `doc` is only populated on 200.
int at_get(const char* url, JsonDocument& doc, const JsonDocument& filter);
```

- [ ] **Step 6: Create `src/at_client.cpp`**

```cpp
#include "at_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "at_ca.h"
#include "dechunk.h"
#include "secrets.h"

namespace {

constexpr uint32_t CONNECT_TIMEOUT_MS = 10000;
constexpr uint32_t READ_TIMEOUT_MS = 10000;

// Dechunker's source: block until the socket has a byte, or give up.
int read_stream(void* ctx) {
  Stream* s = static_cast<Stream*>(ctx);
  const uint32_t t0 = millis();
  while (s->available() == 0) {
    if (millis() - t0 > READ_TIMEOUT_MS) return -1;
    delay(1);
  }
  return s->read();
}

}  // namespace

int at_get(const char* url, JsonDocument& doc, const JsonDocument& filter) {
  WiFiClientSecure client;
  client.setCACert(AT_ROOT_CA);  // never setInsecure(): spec 8
  client.setTimeout(READ_TIMEOUT_MS / 1000);

  HTTPClient http;
  http.setReuse(false);
  http.setConnectTimeout(CONNECT_TIMEOUT_MS);
  http.setTimeout(READ_TIMEOUT_MS);
  if (!http.begin(client, url)) return AT_TRANSPORT_ERROR;
  http.addHeader("Ocp-Apim-Subscription-Key", AT_API_KEY);

  const int status = http.GET();
  if (status <= 0) {
    http.end();
    return AT_TRANSPORT_ERROR;
  }
  if (status != HTTP_CODE_OK) {
    http.end();
    return status;
  }

  // Branch on what the server actually sent, never on which endpoint we think
  // we called: the GTFS API is chunked, the realtime API is not, and getting
  // this wrong fails silently in both directions.
  Stream& raw = http.getStream();
  DeserializationError err;
  if (http.getSize() < 0) {
    Dechunker dechunked(read_stream, &raw);
    err = deserializeJson(doc, dechunked, DeserializationOption::Filter(filter));
    if (!err && dechunked.failed()) err = DeserializationError::IncompleteInput;
  } else {
    err = deserializeJson(doc, raw, DeserializationOption::Filter(filter));
  }
  http.end();
  return err ? AT_PARSE_ERROR : status;
}
```

- [ ] **Step 7: Make the live build exist.** In `src/main.cpp`, guard the
existing demo path and add a live one that, for now, proves the client end to
end. Replace the `#ifndef DEMO_MODE #error ...` block with nothing, wrap the
existing `demo_board(...)` call so that the frame loop asks a helper for its
board, and add:

```cpp
#ifdef DEMO_MODE
Board board_now(uint32_t ms, int64_t) { return demo_board(ms); }
#else
Board board_now(uint32_t, int64_t now) {
  static Snapshot snap;  // static: a Snapshot is far too big for a task stack
  return build_board(snap, WATCHES, LOCATION, now, 0);
}
#endif
```

with `snap.n_watches = N_WATCHES;` and every watch left `Starting` in `setup()`,
so the live build draws both lanes saying `starting` while the rest of the
plan fills the snapshot in. In the live build, `setup()` also: connects WiFi
(`WiFi.mode(WIFI_STA)`, `WiFi.begin(WIFI_SSID, WIFI_PASSWORD)`), calls
`configTime(0, 0, "pool.ntp.org")` (UTC — `nztime` does the local conversion),
and then runs one self-check, printing to serial:

```cpp
  JsonDocument filter, doc;
  stop_filter(filter);
  char url[256];
  for (int i = 0; i < N_WATCHES; i++) {
    url_stop_by_code(url, sizeof url, WATCHES[i].stop_code);
    const int status = at_get(url, doc, filter);
    StopInfo info{};
    const bool ok = status == 200 && parse_stop(doc, &info);
    Serial.printf("stop %s -> HTTP %d %s (location_type %d)\n", WATCHES[i].stop_code,
                  status, ok ? info.stop_id : "unresolved", info.location_type);
  }
```

Use `time(nullptr)` for `now` in the live frame loop, and keep the demo build
byte-for-byte as it behaves today.

- [ ] **Step 8: Build both**

Run: `cp src/secrets.example.h src/secrets.h` only if `src/secrets.h` does not
exist; the controller supplies the real one.
Run: `pio run -e esp32_demo` → [SUCCESS].
Run: `pio run -e esp32` → [SUCCESS].
Run: `pio test -e native` → all suites pass.

- [ ] **Step 9: Commit**

```bash
git add platformio.ini src/secrets.example.h src/watch_config.h src/at_ca.h src/at_client.h src/at_client.cpp src/main.cpp
git commit -m "Talk to AT over TLS pinned to DigiCert Global Root G2

Pins the root rather than the leaf, which is reissued every six months, and
never setInsecure(). Splits the build into a live esp32 env and an esp32_demo
env so the demo stays flashable. The live build resolves both stop codes at
boot and says so on serial.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

**Controller, after the commit:** copy `spike/heap/src/secrets.h` to
`src/secrets.h`, flash `esp32`, and confirm on serial:
`stop 8213 -> HTTP 200 8213-7e021a72 (location_type 0)` and
`stop 122 -> HTTP 200 122-34ecc043 (location_type 1)`. TLS failure shows as
HTTP -1.

---

### Task 7: The fetcher

**Files:**
- Create: `src/fetcher.h`, `src/fetcher.cpp`
- Modify: `src/main.cpp`

**Interfaces:**
- Produces: `void fetcher_begin();` (starts the task on core 0)
- Produces: `void fetcher_snapshot(Snapshot* out);` (copies under the mutex)

Cadences, from spec §5: schedule every 15 minutes and on date rollover;
realtime every 30 s while anything is within 30 minutes, otherwise every
2 minutes; exponential backoff to a 5-minute cap on 429/5xx and transport
errors.

- [ ] **Step 1: Create `src/fetcher.h`**

```cpp
#pragma once
#include "live.h"

// Owns every network call. Runs as a FreeRTOS task on core 0 so that nothing
// it does can stall the 15 fps render loop on core 1 (a TLS handshake alone
// takes longer than a frame). Publishes a snapshot the loop copies.
void fetcher_begin();
void fetcher_snapshot(Snapshot* out);
```

- [ ] **Step 2: Create `src/fetcher.cpp`** implementing, in one task function:

```cpp
#include "fetcher.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <time.h>

#include "at_api.h"
#include "at_client.h"
#include "secrets.h"
#include "watch_config.h"
```

with this structure (write it out; the plan names every rule it must obey):

- File-scope state, all static, never on a stack:
  `Snapshot g_published;` a `SemaphoreHandle_t g_lock;` a working `Snapshot g_work;`
  and per watch a `struct Resolved { char stop_id[STOP_ID_LEN]; char target_stop_id[STOP_ID_LEN]; char route_ids[2][ROUTE_ID_LEN]; int n_routes; int direction; bool resolved; } g_res[MAX_WATCHES];`
  plus `RouteInfo g_rail[12]; int g_n_rail;`.
- `publish()`: take the mutex, copy `g_work` into `g_published`, give it back.
  `fetcher_snapshot` does the reverse. Never hold the mutex across a network call.
- `ensure_wifi()`: `WiFi.status() == WL_CONNECTED` or `WiFi.begin(...)` and
  return false; the task retries in 5 s.
- `ensure_time()`: `configTime(0, 0, "pool.ntp.org")` once; time is ready when
  `time(nullptr) > 1700000000`.
- `resolve(i)`: `url_stop_by_code` for the watch's stop and its
  `toward_stop_code`; `url_routes_by_short_name` when a route is pinned (store
  every returned `route_id`). Status mapping, which is the whole of spec §8 for
  this task: 200 with data → resolved; 200 with empty data → `CheckConfig`;
  400 → `StopLookupGone`; 401 → `CheckKey` on every watch; anything else →
  leave unresolved and retry next pass.
- `load_rail()` once: `url_rail_routes` → `g_rail`. A row's route is rail if
  its `route_id` is in this list, which is what decides `Kind` and supplies the
  badge and colour for an unpinned rail watch. A route in neither list is a bus
  badged with its `route_id` up to the last '-'.
- `refresh_schedule(i)`:
  - Windows: `schedule_windows(nz_local(now), w)` — it returns 1 or 2 requests
    and already handles the midnight split and the rejected hour 0. Issue
    `url_stoptrips` for each and parse both into the same row buffer.
  - Parse into a file-scope `StopTripRow g_rows[48]`.
  - Direction: take the first row of each `direction_id` that survives the route
    filter, `url_trip_stops` each, `parse_trip_stops`, `trip_serves(...)`, then
    `choose_direction`. `DIR_UNPROVABLE` → `CheckConfig` and no rows.
    `DIR_NONE` → keep the cached direction if there is one, else no rows and
    state `Ok` (an empty board is the honest answer).
  - `select_rows(...)` into the watch, set badge/colour/kind from the first
    row's route, state `Ok`.
  - A 404 is an empty window: zero rows, state `Ok`, cache untouched.
- `refresh_realtime()`: `realtime_ids(g_work, now, ids, 24, 6)`; if any,
  `url_realtime` then `parse_realtime` into `RtEntity g_ents[32]`, then
  `apply_realtime` per watch. On 200, `g_work.last_ok = now`.
- The loop: every pass, decide the interval — 30 s if any row is within
  30 minutes, else 120 s; on 429, 5xx or a transport error, double the interval
  to a 300 s cap and keep the last good data. Set `g_work.poll_interval_s` to
  the interval in force, publish, then `vTaskDelay`.
- One serial line per pass, terse:
  `fetch: wifi 1 time 1 sched 892s rt 30s rows 4/3 last_ok 12s ago heap 214000`.
- `fetcher_begin()`: create the mutex, then
  `xTaskCreatePinnedToCore(task, "fetch", 16384, nullptr, 1, nullptr, 0);`
  (16 KB: a TLS handshake needs far more stack than the default).

- [ ] **Step 3: Use it in `src/main.cpp`.** In the live build, call
`fetcher_begin()` at the end of `setup()` (after WiFi and the self-check), and
have `board_now` fill its static snapshot with `fetcher_snapshot(&snap)` before
calling `build_board`.

- [ ] **Step 4: Build**

Run: `pio run -e esp32` → [SUCCESS], and report flash/RAM.
Run: `pio test -e native` → all suites pass (nothing pure changed).

- [ ] **Step 5: Commit**

```bash
git add src/fetcher.h src/fetcher.cpp src/main.cpp
git commit -m "Fetch on core 0 and publish a snapshot the renderer can read

Every network call lives in one task pinned to the other core, because a TLS
handshake takes longer than a frame and the board must keep animating (spec 8).
The task owns the cadences, the backoff and the error surfaces; the render loop
only ever copies a snapshot.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

**Controller, after the commit:** flash `esp32` and capture 3 minutes of serial.
Expected: both stops resolve, a schedule pass with non-zero rows for each watch,
a realtime pass, `last_ok` advancing, heap steady. The screen will still show
whatever `board_now` builds — Task 8 is what makes it right.

---

### Task 8: Live on the glass

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Rebuild the board every frame** in the live path:

```cpp
Board board_now(uint32_t, int64_t now) {
  static Snapshot snap;  // static: ~6 KB is too much for the loop task's stack
  fetcher_snapshot(&snap);
  return build_board(snap, WATCHES, LOCATION, now, 0);
}
```

The countdown ticks every second and the vehicle creeps between polls because
`now` moves, not because data arrived — which is exactly what spec §5 asks for.

- [ ] **Step 2: Pace the loop against an absolute deadline**

```cpp
  static uint32_t next_frame = 0;
  const uint32_t start = millis();
  if (next_frame == 0) next_frame = start;
  ...
  next_frame += FRAME_MS;
  const uint32_t now_ms = millis();
  if (static_cast<int32_t>(next_frame - now_ms) > 0) {
    delay(next_frame - now_ms);
  } else {
    next_frame = now_ms;  // we are behind; do not spiral
  }
```

- [ ] **Step 3: Use real time.** `int64_t now = time(nullptr);` each frame. Until
NTP has landed (`now < 1700000000`), draw the board anyway: every watch is
`Starting`, so every lane says `starting` and no time is shown.

- [ ] **Step 4: Report once every 5 s**, extending the existing line with the
fetcher's view: fps, draw ms, heap, largest block, and `rows` per watch.

- [ ] **Step 5: Build and run the native suites**

Run: `pio run -e esp32` → [SUCCESS].
Run: `pio test -e native` → all pass.

- [ ] **Step 6: Commit**

```bash
git add src/main.cpp
git commit -m "Draw the live board: real departures, counting down between polls

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

**Controller, after the commit:** flash `esp32` and check with the human, against
AT's own app or journeyplanner.at.govt.nz for stop 8213 and Kingsland station:

1. Both lanes show a route badge, the configured headsign, and a countdown.
2. The minutes agree with AT's app within a minute or so.
3. The countdown ticks down every minute and the vehicle creeps right, smoothly,
   between polls rather than jumping only when data arrives.
4. The status bar shows the correct local time and `live` with a green dot.
5. Pull the WiFi (or the router) — within a few minutes the dot turns orange,
   `stale Nm` appears, the last data stays on screen, and the vehicles keep
   moving. Restore it and the board recovers without a reboot.
6. The train lane keeps working although no route is pinned.

---

### Task 9: Measure, and write it down

**Files:**
- Modify: `docs/hardware-notes.md`, `docs/at-api-notes.md`, `README.md`, `docs/design/specs/2026-09-06-at-departure-board-design.md`
- Add: `test/fixtures/post-crl/stops_{8213,122,133,1060}.json`, `test/fixtures/post-crl/stops_unknown.json`

- [ ] **Step 1: Measure on the board** over at least 10 minutes of live running:
worst draw ms and fps with WiFi and TLS active, lowest heap and largest block
across a fetch, and flash/RAM use. If the worst fps is below 15, raise
`-DSPI_FREQUENCY` to 40000000, reflash, re-measure, and keep it only if the
image stays clean.

- [ ] **Step 2: Update `docs/at-api-notes.md`** with what this plan verified on
2026-09-19: `filter[start_hour]=0` is rejected with 400 `Invalid Request`
(so 1..23, and a 00:00–00:59 departure cannot be fetched); an unknown stop code
returns 200 with `{"data":[]}`, not 404; a bad key returns 401 with a
`statusCode`/`message` body; `stops?filter[stop_code]` still resolves
8213/122/133/1060 to the same ids as on 13 September. Note the four new
fixtures and that they were captured on 2026-09-19.

- [ ] **Step 3: Update `docs/hardware-notes.md`** with a `## Live data on the
board` section: the measured fps/heap/flash with the network running, the TLS
root that is pinned and why the root rather than the leaf, and the stack size
the fetch task needs. Replace the "Still to verify on hardware" TLS bullet,
which this plan closes, and leave the PWM one.

- [ ] **Step 4: Update `README.md`:** the Status line becomes live data; add a
short "Point it at your own stops" section covering `src/secrets.h` (copy
`secrets.example.h`, never commit it), `src/watch_config.h`, and that the
portal is still to come; note `pio run -e esp32_demo -t upload` for the
no-network demo.

- [ ] **Step 5: Update the spec's status line** to record that §5, §6 and §3a
are implemented, and that §3 (portal, NVS) and §8's dimming and quiet hours are
not.

- [ ] **Step 6: Final verification**

Run: `python -m pytest -q` → all pass.
Run: `pio test -e native` → all pass.
Run: `pio run -e esp32` and `pio run -e esp32_demo` → [SUCCESS].

- [ ] **Step 7: Commit**

```bash
git add docs/ README.md test/fixtures/
git commit -m "Record what the live board measures and what the API really does

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```
