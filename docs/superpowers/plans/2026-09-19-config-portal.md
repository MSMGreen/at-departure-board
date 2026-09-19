# Config Portal Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the board a LAN web page that changes its location, watches and theme at runtime, so neither a stop change nor a theme change needs a recompile and a reflash.

**Architecture:** Three new units. `lib/core/src/config_schema.{h,cpp}` is pure data logic — parse, validate, compact, serialize — with no Arduino headers, so every rule is proven under `pio test -e native` on the PC. `src/config.{h,cpp}` is a thin NVS wrapper owning the single live instance and the accessors that replace today's compile-time `WATCHES` / `LOCATION`. `src/portal.{h,cpp}` runs an Arduino `WebServer` on its own core-0 FreeRTOS task, away from the 15 fps draw loop on core 1.

**Tech Stack:** C++17 (`-std=gnu++17`), PlatformIO, Arduino-ESP32 (`espressif32@7.1.3`), ArduinoJson 7.4.3 (already linked), Arduino `WebServer` + `Preferences` + `ESPmDNS` (bundled with the framework, no new `lib_deps`), Unity for native tests.

**Spec:** `docs/superpowers/specs/2026-09-19-config-portal-design.md` (committed as `c962ba3`)

## Global Constraints

- `MAX_WATCHES = 4` (`lib/core/src/model.h:11`). The spec caps watches at four; at five the lane height stops being readable across a room.
- **`lib/core/src/` must never include an Arduino header.** It compiles under `platform = native`. `config_schema` lives there precisely so it can be tested on the PC.
- **No heap in the data model.** Fixed-size arrays and char buffers only — the board runs for months. No `String` in `config_schema` or `config`.
- **Only `theme` is written to live config at runtime** (spec §6). Everything else persists to NVS and takes effect via reboot. The fetcher task reads `config_watches()` on core 0 with no lock.
- **15 fps must hold.** Ghibli frames already cost 58–59 ms of the 66 ms budget. `handleClient()` never runs on the draw loop.
- Flash ceiling is 1,310,720 B (default partition table, unchanged). Baseline before this work: 992,053 B.
- The panel font is ASCII only — no macrons in any string that can reach the display.
- **Commit messages follow this repo's existing style:** sentence-case imperative subject, no `feat:`/`fix:` prefixes (see `git log`). Every commit ends with `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.
- **Flashing is a human step.** `upload_port = COM8`, and auto-reset does not work on this board. Before *every* `-t upload`, a human must put it in download mode: hold BOOT, tap EN, release BOOT. Tasks that flash are marked **FLASH**; the person driving this plan performs them and reads the panel, never a subagent.

---

## File Structure

| File | Responsibility | New? |
|---|---|---|
| `lib/core/src/at_api.h/.cpp` | Add `stop_name` to `StopInfo` and its filter | Modify |
| `lib/core/src/config_schema.h/.cpp` | `Config` type, parse, validate, compact, serialize. Pure. | Create |
| `src/config.h/.cpp` | NVS load/store, the live instance, accessors, and the relocated `N_WATCHES` compile-time guard | Create |
| `src/portal.h/.cpp` | `WebServer`, its task, the endpoints | Create |
| `src/portal_page.h` | The config page as one `PROGMEM` string | Create |
| `src/watch_config.h` | Demoted to compiled defaults; content unchanged | Keep |
| `src/fetcher.cpp` | Read config accessors instead of `WATCHES` / `N_WATCHES` | Modify |
| `src/main.cpp` | `config_begin()`, `portal_begin()`, boot screen, theme from config | Modify |
| `test/test_at_api/test_main.cpp` | Cover `stop_name` | Modify |
| `test/test_config_schema/test_main.cpp` | Cover every schema rule | Create |

---

### Task 1: Stop name in the pure AT layer

`POST /api/stop` has to show a human "Kingsland Train Station" back, but `StopInfo` only carries `stop_id`. This is the smallest possible change to the pure layer and there is already a fixture with the field in it.

**Files:**
- Modify: `lib/core/src/at_api.h:19-22` (the `StopInfo` struct), `lib/core/src/at_api.cpp:70-74` (`stop_filter`), and `parse_stop` in the same file
- Test: `test/test_at_api/test_main.cpp`

**Interfaces:**
- Consumes: nothing from earlier tasks
- Produces: `StopInfo::stop_name` (a `char[40]`), populated by `parse_stop`, used by Task 8

- [ ] **Step 1: Write the failing test**

Add to `test/test_at_api/test_main.cpp`, and add `RUN_TEST(test_stop_name);` to `main()`:

```cpp
void test_stop_name() {
  JsonDocument f;
  stop_filter(f);
  JsonDocument doc;
  load("test/fixtures/post-crl/stops_1060.json", doc, f);
  StopInfo info{};
  TEST_ASSERT_TRUE(parse_stop(doc, &info));
  TEST_ASSERT_EQUAL_STRING("1060-00b64ee7", info.stop_id);
  TEST_ASSERT_EQUAL_STRING("Wynyard Quarter", info.stop_name);
  TEST_ASSERT_EQUAL_INT(0, info.location_type);
}
```

- [ ] **Step 2: Run it and watch it fail**

Run: `pio test -e native -f test_at_api`
Expected: FAIL to compile — `StopInfo` has no member `stop_name`.

- [ ] **Step 3: Add the field and the filter entry**

In `lib/core/src/at_api.h`, extend the struct:

```c
constexpr int STOP_NAME_LEN = 40;

struct StopInfo {
  char stop_id[STOP_ID_LEN];
  char stop_name[STOP_NAME_LEN];
  int location_type;  // 0 stop, 1 station
};
```

In `lib/core/src/at_api.cpp`, add one line to `stop_filter`:

```c
void stop_filter(JsonDocument& f) {
  JsonObject a = f["data"][0]["attributes"].to<JsonObject>();
  a["stop_id"] = true;
  a["stop_name"] = true;
  a["location_type"] = true;
}
```

- [ ] **Step 4: Populate it in `parse_stop`**

`lib/core/src/at_api.cpp:112` already has a file-local `copy()` helper; use it rather than introducing a second truncation idiom. Add one line:

```c
bool parse_stop(const JsonDocument& doc, StopInfo* out) {
  JsonArrayConst rows = doc["data"].as<JsonArrayConst>();
  if (rows.isNull() || rows.size() == 0) return false;
  JsonObjectConst a = rows[0]["attributes"];
  copy(out->stop_id, sizeof out->stop_id, a["stop_id"] | "");
  copy(out->stop_name, sizeof out->stop_name, a["stop_name"] | "");
  out->location_type = a["location_type"] | 0;
  return out->stop_id[0] != '\0';
}
```

The return condition is unchanged: a stop is still identified by its id, and a nameless stop is still valid.

- [ ] **Step 5: Run the whole native suite**

Run: `pio test -e native`
Expected: PASS, including every pre-existing test. `stop_filter` is shared, so a regression here would show up in the stoptrips and post-CRL tests.

- [ ] **Step 6: Commit**

```bash
git add lib/core/src/at_api.h lib/core/src/at_api.cpp test/test_at_api/test_main.cpp
git commit -m "Parse stop_name so the portal can confirm a stop code

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 2: `config_schema` — the type and the parser

**Files:**
- Create: `lib/core/src/config_schema.h`, `lib/core/src/config_schema.cpp`
- Test: `test/test_config_schema/test_main.cpp`

**Interfaces:**
- Consumes: `WatchConfig` (`lib/core/src/live.h:10`), `MAX_WATCHES` (`lib/core/src/model.h:11`)
- Produces: `Config`, `CfgWatch`, `CfgError`, `cfg_error_text()`, `cfg_parse()`, and the caps `CFG_LOCATION_CAP` / `CFG_FIELD_CAP` / `CFG_JSON_CAP` / `CFG_SCHEMA_VERSION`. Tasks 3, 4, 8 and 9 all rely on these exact names.

**Note on the shape.** `Config` stores *all* watches with an `enabled` flag, not just the enabled ones. Compaction happens on the way out, in Task 3's `cfg_publish()`. Storing only the enabled ones would make "disable" indistinguishable from "delete", and the spec's watch object carries `enabled` as a real field. `Config` is therefore plain data with no internal pointers, so it is safe to copy.

- [ ] **Step 1: Write the header**

Create `lib/core/src/config_schema.h`:

```c
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "live.h"   // WatchConfig
#include "model.h"  // MAX_WATCHES

// Pure config logic: no Arduino headers, no NVS, no heap. Everything here is
// exercised by test/test_config_schema under `pio test -e native`.

constexpr int CFG_LOCATION_CAP = 24;  // matches Board::location
constexpr int CFG_FIELD_CAP = 32;
constexpr int CFG_JSON_CAP = 1024;  // four watches serialise to ~620 bytes
constexpr uint8_t CFG_SCHEMA_VERSION = 1;

struct CfgWatch {
  char label[CFG_FIELD_CAP];
  char stop_code[CFG_FIELD_CAP];
  char route_short_name[CFG_FIELD_CAP];  // "" means any route (spec 3)
  char toward_stop_code[CFG_FIELD_CAP];  // "" means no direction filter
  bool enabled;
};

// Plain data - no pointers into itself, so this is safe to copy.
struct Config {
  char location[CFG_LOCATION_CAP];
  uint8_t theme;
  uint8_t n_watches;  // every watch, enabled or not
  CfgWatch watches[MAX_WATCHES];
};

enum class CfgError : uint8_t {
  Ok,
  BadJson,          // would not deserialise
  BadVersion,       // "v" missing or not CFG_SCHEMA_VERSION
  TooManyWatches,   // more than MAX_WATCHES
  NoWatches,        // zero watches, or zero enabled ones
  MissingStopCode,  // a watch with an empty stop_code
  FieldTooLong,     // a string that will not fit its buffer
  LocationTooLong,
};

const char* cfg_error_text(CfgError e);

// Fills *out only on CfgError::Ok; *out is untouched otherwise, so a caller
// can keep its previous config on a failed save.
// theme is clamped to [0, theme_max), never rejected - a theme can disappear
// when the generated table changes and that must not brick the config.
CfgError cfg_parse(const char* json, Config* out, uint8_t theme_max);
```

- [ ] **Step 2: Write the failing tests**

Create `test/test_config_schema/test_main.cpp`:

```cpp
#include <string.h>
#include <unity.h>

#include "config_schema.h"

void setUp() {}
void tearDown() {}

namespace {
const char* GOOD =
    "{\"v\":1,\"location\":\"Kingsland\",\"theme\":1,\"watches\":["
    "{\"label\":\"to Wynyard Quarter\",\"stop_code\":\"8213\","
    "\"route_short_name\":\"20\",\"toward_stop_code\":\"1060\",\"enabled\":true},"
    "{\"label\":\"to Waitemata\",\"stop_code\":\"122\","
    "\"route_short_name\":\"\",\"toward_stop_code\":\"133\",\"enabled\":false}]}";
}  // namespace

void test_parses_a_good_document() {
  Config c{};
  TEST_ASSERT_TRUE(cfg_parse(GOOD, &c, 2) == CfgError::Ok);
  TEST_ASSERT_EQUAL_STRING("Kingsland", c.location);
  TEST_ASSERT_EQUAL_UINT8(1, c.theme);
  TEST_ASSERT_EQUAL_UINT8(2, c.n_watches);
  TEST_ASSERT_EQUAL_STRING("8213", c.watches[0].stop_code);
  TEST_ASSERT_EQUAL_STRING("20", c.watches[0].route_short_name);
  TEST_ASSERT_TRUE(c.watches[0].enabled);
  TEST_ASSERT_FALSE(c.watches[1].enabled);
}

void test_empty_route_short_name_is_allowed() {
  // "" means any route, which is what carries a rail watch through the CRL
  // rename. Rejecting it would break the shipped default config.
  Config c{};
  TEST_ASSERT_TRUE(cfg_parse(GOOD, &c, 2) == CfgError::Ok);
  TEST_ASSERT_EQUAL_STRING("", c.watches[1].route_short_name);
}

void test_theme_is_clamped_not_rejected() {
  Config c{};
  const char* j =
      "{\"v\":1,\"location\":\"X\",\"theme\":99,\"watches\":["
      "{\"label\":\"a\",\"stop_code\":\"1\",\"route_short_name\":\"\","
      "\"toward_stop_code\":\"\",\"enabled\":true}]}";
  TEST_ASSERT_TRUE(cfg_parse(j, &c, 2) == CfgError::Ok);
  TEST_ASSERT_EQUAL_UINT8(1, c.theme);  // clamped to theme_max - 1
}

void test_rejects_garbage_and_wrong_version() {
  Config c{};
  TEST_ASSERT_TRUE(cfg_parse("not json at all", &c, 2) == CfgError::BadJson);
  TEST_ASSERT_TRUE(cfg_parse("", &c, 2) == CfgError::BadJson);
  TEST_ASSERT_TRUE(cfg_parse("{\"v\":1,\"location\":\"X\"", &c, 2) == CfgError::BadJson);
  TEST_ASSERT_TRUE(
      cfg_parse("{\"v\":2,\"location\":\"X\",\"watches\":[]}", &c, 2) == CfgError::BadVersion);
  TEST_ASSERT_TRUE(
      cfg_parse("{\"location\":\"X\",\"watches\":[]}", &c, 2) == CfgError::BadVersion);
}

void test_rejects_too_many_watches() {
  Config c{};
  char j[CFG_JSON_CAP];
  int p = snprintf(j, sizeof j, "{\"v\":1,\"location\":\"X\",\"theme\":0,\"watches\":[");
  for (int i = 0; i < MAX_WATCHES + 1; i++) {
    p += snprintf(j + p, sizeof(j) - p,
                  "%s{\"label\":\"a\",\"stop_code\":\"1\",\"route_short_name\":\"\","
                  "\"toward_stop_code\":\"\",\"enabled\":true}",
                  i ? "," : "");
  }
  snprintf(j + p, sizeof(j) - p, "]}");
  TEST_ASSERT_TRUE(cfg_parse(j, &c, 2) == CfgError::TooManyWatches);
}

void test_rejects_no_watches_and_none_enabled() {
  Config c{};
  TEST_ASSERT_TRUE(
      cfg_parse("{\"v\":1,\"location\":\"X\",\"theme\":0,\"watches\":[]}", &c, 2) ==
      CfgError::NoWatches);
  const char* all_off =
      "{\"v\":1,\"location\":\"X\",\"theme\":0,\"watches\":["
      "{\"label\":\"a\",\"stop_code\":\"1\",\"route_short_name\":\"\","
      "\"toward_stop_code\":\"\",\"enabled\":false}]}";
  TEST_ASSERT_TRUE(cfg_parse(all_off, &c, 2) == CfgError::NoWatches);
}

void test_rejects_missing_stop_code() {
  Config c{};
  const char* j =
      "{\"v\":1,\"location\":\"X\",\"theme\":0,\"watches\":["
      "{\"label\":\"a\",\"stop_code\":\"\",\"route_short_name\":\"\","
      "\"toward_stop_code\":\"\",\"enabled\":true}]}";
  TEST_ASSERT_TRUE(cfg_parse(j, &c, 2) == CfgError::MissingStopCode);
}

void test_rejects_overlong_fields() {
  Config c{};
  char big[CFG_FIELD_CAP + 8];
  memset(big, 'x', sizeof big);
  big[sizeof(big) - 1] = '\0';
  char j[CFG_JSON_CAP];
  snprintf(j, sizeof j,
           "{\"v\":1,\"location\":\"X\",\"theme\":0,\"watches\":["
           "{\"label\":\"%s\",\"stop_code\":\"1\",\"route_short_name\":\"\","
           "\"toward_stop_code\":\"\",\"enabled\":true}]}",
           big);
  TEST_ASSERT_TRUE(cfg_parse(j, &c, 2) == CfgError::FieldTooLong);

  char loc[CFG_LOCATION_CAP + 8];
  memset(loc, 'y', sizeof loc);
  loc[sizeof(loc) - 1] = '\0';
  snprintf(j, sizeof j,
           "{\"v\":1,\"location\":\"%s\",\"theme\":0,\"watches\":["
           "{\"label\":\"a\",\"stop_code\":\"1\",\"route_short_name\":\"\","
           "\"toward_stop_code\":\"\",\"enabled\":true}]}",
           loc);
  TEST_ASSERT_TRUE(cfg_parse(j, &c, 2) == CfgError::LocationTooLong);
}

void test_failed_parse_leaves_the_target_alone() {
  // config_save_json must be able to reject without destroying live config.
  Config c{};
  TEST_ASSERT_TRUE(cfg_parse(GOOD, &c, 2) == CfgError::Ok);
  TEST_ASSERT_TRUE(cfg_parse("rubbish", &c, 2) == CfgError::BadJson);
  TEST_ASSERT_EQUAL_STRING("Kingsland", c.location);
  TEST_ASSERT_EQUAL_UINT8(2, c.n_watches);
}

void test_every_error_has_text() {
  const CfgError all[] = {CfgError::Ok,           CfgError::BadJson,
                          CfgError::BadVersion,   CfgError::TooManyWatches,
                          CfgError::NoWatches,    CfgError::MissingStopCode,
                          CfgError::FieldTooLong, CfgError::LocationTooLong};
  for (CfgError e : all) {
    const char* t = cfg_error_text(e);
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_TRUE(strlen(t) > 0);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parses_a_good_document);
  RUN_TEST(test_empty_route_short_name_is_allowed);
  RUN_TEST(test_theme_is_clamped_not_rejected);
  RUN_TEST(test_rejects_garbage_and_wrong_version);
  RUN_TEST(test_rejects_too_many_watches);
  RUN_TEST(test_rejects_no_watches_and_none_enabled);
  RUN_TEST(test_rejects_missing_stop_code);
  RUN_TEST(test_rejects_overlong_fields);
  RUN_TEST(test_failed_parse_leaves_the_target_alone);
  RUN_TEST(test_every_error_has_text);
  return UNITY_END();
}
```

- [ ] **Step 3: Run them and watch them fail**

Run: `pio test -e native -f test_config_schema`
Expected: FAIL to link — `cfg_parse` and `cfg_error_text` are undefined.

- [ ] **Step 4: Implement the parser**

Create `lib/core/src/config_schema.cpp`. Notes that matter for getting this right:

- Parse into a *local* `Config` and only copy to `*out` once every check has passed. That is what `test_failed_parse_leaves_the_target_alone` pins.
- A field is "too long" when `strlen(src) >= cap`, so there is always room for the NUL. Check before copying, do not truncate silently — a truncated stop code would produce a silently empty lane.
- `enabled` defaults to `true` when the key is absent, so a hand-written config without the field behaves sensibly.
- Count enabled watches while validating; `NoWatches` covers both an empty array and an array with nothing enabled.

```c
#include "config_schema.h"

#include <ArduinoJson.h>
#include <string.h>

namespace {

// false when src will not fit dst (cap includes the NUL).
bool copy_field(char* dst, size_t cap, const char* src) {
  if (src == nullptr) src = "";
  if (strlen(src) >= cap) return false;
  strcpy(dst, src);
  return true;
}

}  // namespace

const char* cfg_error_text(CfgError e) {
  switch (e) {
    case CfgError::Ok: return "ok";
    case CfgError::BadJson: return "could not parse the config as JSON";
    case CfgError::BadVersion: return "config version is not supported";
    case CfgError::TooManyWatches: return "too many watches (maximum four)";
    case CfgError::NoWatches: return "at least one enabled watch is required";
    case CfgError::MissingStopCode: return "every watch needs a stop code";
    case CfgError::FieldTooLong: return "a watch field is too long";
    case CfgError::LocationTooLong: return "the location name is too long";
  }
  return "unknown error";
}

CfgError cfg_parse(const char* json, Config* out, uint8_t theme_max) {
  if (json == nullptr || json[0] == '\0') return CfgError::BadJson;

  JsonDocument doc;
  if (deserializeJson(doc, json)) return CfgError::BadJson;
  if (doc["v"].isNull() || doc["v"].as<uint8_t>() != CFG_SCHEMA_VERSION) {
    return CfgError::BadVersion;
  }

  Config c{};
  if (!copy_field(c.location, sizeof c.location, doc["location"] | "")) {
    return CfgError::LocationTooLong;
  }

  const uint8_t t = doc["theme"] | 0;
  c.theme = (theme_max == 0 || t < theme_max) ? t : static_cast<uint8_t>(theme_max - 1);

  JsonArrayConst ws = doc["watches"].as<JsonArrayConst>();
  if (ws.size() > MAX_WATCHES) return CfgError::TooManyWatches;

  uint8_t n_enabled = 0;
  for (JsonObjectConst w : ws) {
    CfgWatch& d = c.watches[c.n_watches];
    if (!copy_field(d.label, sizeof d.label, w["label"] | "") ||
        !copy_field(d.stop_code, sizeof d.stop_code, w["stop_code"] | "") ||
        !copy_field(d.route_short_name, sizeof d.route_short_name,
                    w["route_short_name"] | "") ||
        !copy_field(d.toward_stop_code, sizeof d.toward_stop_code,
                    w["toward_stop_code"] | "")) {
      return CfgError::FieldTooLong;
    }
    if (d.stop_code[0] == '\0') return CfgError::MissingStopCode;
    d.enabled = w["enabled"] | true;
    if (d.enabled) n_enabled++;
    c.n_watches++;
  }

  if (n_enabled == 0) return CfgError::NoWatches;

  *out = c;
  return CfgError::Ok;
}
```

- [ ] **Step 5: Run the tests**

Run: `pio test -e native -f test_config_schema`
Expected: PASS, all ten.

- [ ] **Step 6: Run the whole suite**

Run: `pio test -e native`
Expected: PASS. `config_schema.cpp` joins `lib/core`, so this confirms it has not broken any existing target's build.

- [ ] **Step 7: Commit**

```bash
git add lib/core/src/config_schema.h lib/core/src/config_schema.cpp test/test_config_schema/
git commit -m "Add a pure config schema with a validating parser

Config holds every watch with its enabled flag rather than only the
enabled ones, so disabling a watch in the UI stays distinct from
deleting it. Validation rejects rather than truncates: a truncated stop
code would show as a silently empty lane.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 3: `config_schema` — serialize and compact

**Files:**
- Modify: `lib/core/src/config_schema.h`, `lib/core/src/config_schema.cpp`
- Test: `test/test_config_schema/test_main.cpp`

**Interfaces:**
- Consumes: `Config`, `CfgWatch`, `cfg_parse` from Task 2
- Produces: `cfg_serialize(const Config&, char* out, size_t cap) -> size_t` and `cfg_publish(const Config&, WatchConfig out[MAX_WATCHES]) -> uint8_t`. Task 4 uses both.

**The pointer hazard.** `cfg_publish` fills a `WatchConfig[]` whose `const char*` fields point *into* the `Config` it was given. The `Config` must outlive the published array, and if the `Config` is later overwritten the array must be republished. In `src/config.cpp` (Task 4) both are file-scope statics and republishing happens only in `config_begin()`, so this holds — but the header must say so, because a caller who copies a `Config` onto the stack and publishes from it gets dangling pointers.

- [ ] **Step 1: Add the declarations**

Append to `lib/core/src/config_schema.h`:

```c
// Serialises cfg as the document cfg_parse accepts. Returns the length
// written, or 0 if cap was too small (out is then left empty).
size_t cfg_serialize(const Config& cfg, char* out, size_t cap);

// Fills out[] with the enabled watches, compacted and in order, and returns
// how many. The WatchConfig pointers point INTO cfg, so cfg must outlive out[]
// and out[] must be refilled whenever cfg changes.
uint8_t cfg_publish(const Config& cfg, WatchConfig out[MAX_WATCHES]);
```

- [ ] **Step 2: Write the failing tests**

Add to `test/test_config_schema/test_main.cpp`, and add the four `RUN_TEST` lines to `main()`:

```cpp
void test_round_trips_through_serialise() {
  Config a{};
  TEST_ASSERT_TRUE(cfg_parse(GOOD, &a, 2) == CfgError::Ok);
  char buf[CFG_JSON_CAP];
  const size_t n = cfg_serialize(a, buf, sizeof buf);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_size_t(n, strlen(buf));

  Config b{};
  TEST_ASSERT_TRUE(cfg_parse(buf, &b, 2) == CfgError::Ok);
  TEST_ASSERT_EQUAL_STRING(a.location, b.location);
  TEST_ASSERT_EQUAL_UINT8(a.theme, b.theme);
  TEST_ASSERT_EQUAL_UINT8(a.n_watches, b.n_watches);
  for (int i = 0; i < a.n_watches; i++) {
    TEST_ASSERT_EQUAL_STRING(a.watches[i].label, b.watches[i].label);
    TEST_ASSERT_EQUAL_STRING(a.watches[i].stop_code, b.watches[i].stop_code);
    TEST_ASSERT_EQUAL_STRING(a.watches[i].route_short_name, b.watches[i].route_short_name);
    TEST_ASSERT_EQUAL_STRING(a.watches[i].toward_stop_code, b.watches[i].toward_stop_code);
    // The disabled watch must survive the round trip as disabled.
    TEST_ASSERT_EQUAL_INT(a.watches[i].enabled, b.watches[i].enabled);
  }
}

void test_serialise_refuses_a_small_buffer() {
  Config a{};
  TEST_ASSERT_TRUE(cfg_parse(GOOD, &a, 2) == CfgError::Ok);
  char small[16];
  TEST_ASSERT_EQUAL_size_t(0, cfg_serialize(a, small, sizeof small));
  TEST_ASSERT_EQUAL_STRING("", small);
}

void test_publish_compacts_around_a_disabled_watch() {
  const char* j =
      "{\"v\":1,\"location\":\"X\",\"theme\":0,\"watches\":["
      "{\"label\":\"first\",\"stop_code\":\"1\",\"route_short_name\":\"\","
      "\"toward_stop_code\":\"\",\"enabled\":true},"
      "{\"label\":\"middle\",\"stop_code\":\"2\",\"route_short_name\":\"\","
      "\"toward_stop_code\":\"\",\"enabled\":false},"
      "{\"label\":\"last\",\"stop_code\":\"3\",\"route_short_name\":\"\","
      "\"toward_stop_code\":\"\",\"enabled\":true}]}";
  Config c{};
  TEST_ASSERT_TRUE(cfg_parse(j, &c, 2) == CfgError::Ok);
  TEST_ASSERT_EQUAL_UINT8(3, c.n_watches);

  WatchConfig pub[MAX_WATCHES];
  TEST_ASSERT_EQUAL_UINT8(2, cfg_publish(c, pub));
  TEST_ASSERT_EQUAL_STRING("first", pub[0].label);
  TEST_ASSERT_EQUAL_STRING("1", pub[0].stop_code);
  TEST_ASSERT_EQUAL_STRING("last", pub[1].label);
  TEST_ASSERT_EQUAL_STRING("3", pub[1].stop_code);
}

void test_published_pointers_reach_into_the_config() {
  Config c{};
  TEST_ASSERT_TRUE(cfg_parse(GOOD, &c, 2) == CfgError::Ok);
  WatchConfig pub[MAX_WATCHES];
  TEST_ASSERT_EQUAL_UINT8(1, cfg_publish(c, pub));  // second watch is disabled
  TEST_ASSERT_EQUAL_PTR(c.watches[0].stop_code, pub[0].stop_code);
}
```

- [ ] **Step 3: Run them and watch them fail**

Run: `pio test -e native -f test_config_schema`
Expected: FAIL to link — `cfg_serialize` and `cfg_publish` are undefined.

- [ ] **Step 4: Implement both**

Append to `lib/core/src/config_schema.cpp`:

```c
size_t cfg_serialize(const Config& cfg, char* out, size_t cap) {
  if (out == nullptr || cap == 0) return 0;
  out[0] = '\0';

  JsonDocument doc;
  doc["v"] = CFG_SCHEMA_VERSION;
  doc["location"] = cfg.location;
  doc["theme"] = cfg.theme;
  JsonArray ws = doc["watches"].to<JsonArray>();
  for (uint8_t i = 0; i < cfg.n_watches; i++) {
    JsonObject w = ws.add<JsonObject>();
    w["label"] = cfg.watches[i].label;
    w["stop_code"] = cfg.watches[i].stop_code;
    w["route_short_name"] = cfg.watches[i].route_short_name;
    w["toward_stop_code"] = cfg.watches[i].toward_stop_code;
    w["enabled"] = cfg.watches[i].enabled;
  }

  // measureJson excludes the NUL, serializeJson needs room for it.
  if (measureJson(doc) + 1 > cap) return 0;
  return serializeJson(doc, out, cap);
}

uint8_t cfg_publish(const Config& cfg, WatchConfig out[MAX_WATCHES]) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < cfg.n_watches && n < MAX_WATCHES; i++) {
    if (!cfg.watches[i].enabled) continue;
    out[n].label = cfg.watches[i].label;
    out[n].stop_code = cfg.watches[i].stop_code;
    out[n].route_short_name = cfg.watches[i].route_short_name;
    out[n].toward_stop_code = cfg.watches[i].toward_stop_code;
    n++;
  }
  return n;
}
```

- [ ] **Step 5: Run the suite**

Run: `pio test -e native`
Expected: PASS, fourteen tests in `test_config_schema` plus everything pre-existing.

- [ ] **Step 6: Commit**

```bash
git add lib/core/src/config_schema.h lib/core/src/config_schema.cpp test/test_config_schema/
git commit -m "Serialise config and publish enabled watches compacted

cfg_publish hands out WatchConfig pointers into the Config it was given,
so the header states the lifetime rule the callers have to keep.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 4: The NVS wrapper

**Files:**
- Create: `src/config.h`, `src/config.cpp`
- Test: none native (NVS is device-only). Verified on device in Task 5.

**Interfaces:**
- Consumes: everything from Tasks 2 and 3; `WATCHES` / `N_WATCHES` / `LOCATION` (`src/watch_config.h`); `theme_count()` (`lib/core/src/theme.h`)
- Produces: `config_begin()`, `config_watches()`, `config_n_watches()`, `config_location()`, `config_theme()`, `config_set_theme()`, `config_save_json()`, `config_to_json()`. Tasks 5–11 use these.

- [ ] **Step 1: Write the header**

Create `src/config.h`:

```c
#pragma once
#include "config_schema.h"

// The live configuration: NVS-backed, seeded from src/watch_config.h when NVS
// holds nothing valid. Spec section 6 - nothing but the theme is written at
// runtime, because the fetch task reads the watches on core 0 with no lock.

void config_begin();  // call once in setup(), before fetcher_begin()

const WatchConfig* config_watches();
uint8_t config_n_watches();
const char* config_location();

uint8_t config_theme();
void config_set_theme(uint8_t t);  // applies live AND persists

// Validates, then persists to NVS ONLY. The in-RAM config is deliberately not
// updated: the caller reboots so config_begin() picks the new values up at the
// one moment nothing else is reading them.
CfgError config_save_json(const char* json);

size_t config_to_json(char* out, size_t cap);
```

- [ ] **Step 2: Write the implementation**

Create `src/config.cpp`:

```c
#include "config.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "theme.h"
#include "watch_config.h"

namespace {

constexpr char NVS_NS[] = "board";
constexpr char NVS_KEY[] = "cfg";

Config g_cfg;
WatchConfig g_pub[MAX_WATCHES];
uint8_t g_n_pub = 0;

// The one value written at runtime. A uint8_t store is atomic on this target,
// which is the whole reason the theme may change without a reboot.
volatile uint8_t g_theme = 0;

Preferences g_prefs;

void seed_from_compiled_defaults() {
  Config c{};
  strncpy(c.location, LOCATION, sizeof c.location - 1);
  c.theme = 0;
  c.n_watches = 0;
  for (int i = 0; i < N_WATCHES && i < MAX_WATCHES; i++) {
    CfgWatch& d = c.watches[c.n_watches];
    strncpy(d.label, WATCHES[i].label, sizeof d.label - 1);
    strncpy(d.stop_code, WATCHES[i].stop_code, sizeof d.stop_code - 1);
    strncpy(d.route_short_name, WATCHES[i].route_short_name, sizeof d.route_short_name - 1);
    strncpy(d.toward_stop_code, WATCHES[i].toward_stop_code, sizeof d.toward_stop_code - 1);
    d.enabled = true;
    c.n_watches++;
  }
  g_cfg = c;
}

}  // namespace

void config_begin() {
  char json[CFG_JSON_CAP];
  json[0] = '\0';

  bool loaded = false;
  if (g_prefs.begin(NVS_NS, true)) {  // read-only
    g_prefs.getString(NVS_KEY, json, sizeof json);
    g_prefs.end();
    const CfgError e = cfg_parse(json, &g_cfg, theme_count());
    if (e == CfgError::Ok) {
      loaded = true;
    } else if (json[0] != '\0') {
      Serial.printf("config: stored config rejected (%s), using defaults\n",
                    cfg_error_text(e));
    }
  }
  if (!loaded) seed_from_compiled_defaults();

  g_theme = g_cfg.theme;
  g_n_pub = cfg_publish(g_cfg, g_pub);
  Serial.printf("config: %s, %u watches, theme %u (%s)\n", g_cfg.location,
                static_cast<unsigned>(g_n_pub), static_cast<unsigned>(g_theme),
                loaded ? "nvs" : "compiled defaults");
}

const WatchConfig* config_watches() { return g_pub; }
uint8_t config_n_watches() { return g_n_pub; }
const char* config_location() { return g_cfg.location; }
uint8_t config_theme() { return g_theme; }

void config_set_theme(uint8_t t) {
  if (t >= theme_count()) return;
  g_theme = t;   // live, atomic
  g_cfg.theme = t;
  char json[CFG_JSON_CAP];
  if (cfg_serialize(g_cfg, json, sizeof json) == 0) return;
  if (g_prefs.begin(NVS_NS, false)) {
    g_prefs.putString(NVS_KEY, json);
    g_prefs.end();
  }
}

CfgError config_save_json(const char* json) {
  Config scratch{};
  const CfgError e = cfg_parse(json, &scratch, theme_count());
  if (e != CfgError::Ok) return e;

  // Re-serialise rather than storing the caller's bytes: this normalises the
  // document and guarantees what lands in NVS is something cfg_parse accepts.
  char clean[CFG_JSON_CAP];
  if (cfg_serialize(scratch, clean, sizeof clean) == 0) return CfgError::FieldTooLong;

  if (!g_prefs.begin(NVS_NS, false)) return CfgError::BadJson;
  g_prefs.putString(NVS_KEY, clean);
  g_prefs.end();
  return CfgError::Ok;  // caller reboots; g_cfg deliberately untouched
}

size_t config_to_json(char* out, size_t cap) {
  Config snapshot = g_cfg;  // plain data, safe to copy
  snapshot.theme = g_theme;  // the live value, which may be ahead of g_cfg
  return cfg_serialize(snapshot, out, cap);
}
```

- [ ] **Step 3: Confirm it builds**

Run: `pio run -e esp32`
Expected: SUCCESS. Nothing calls `config_begin()` yet, so flash should be roughly unchanged — the linker drops most of it. Note the number.

- [ ] **Step 4: Confirm the demo build and native suite still build**

Run: `pio run -e esp32_demo` then `pio test -e native`
Expected: both SUCCESS. `src/config.cpp` is not compiled into `native` (it lives in `src/`, and `test_build_src = no`), which is exactly why the logic worth testing lives in `lib/core`.

- [ ] **Step 5: Commit**

```bash
git add src/config.h src/config.cpp
git commit -m "Back the configuration with NVS, seeded from watch_config.h

config_save_json writes NVS and deliberately leaves the in-RAM config
alone: the fetch task reads the watches on core 0 without a lock, so the
new values are picked up by config_begin() after a reboot instead.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 5: Convert the call sites — **FLASH**

The behaviour-preserving step. Nothing should look different on the panel; that is the entire test.

**Files:**
- Modify: `src/fetcher.cpp:30`, `:290`, `:355`, `:575`, `:705`, `:707`
- Modify: `src/main.cpp:60`, and `setup()` around `:80`

**Interfaces:**
- Consumes: `config_begin()`, `config_watches()`, `config_n_watches()`, `config_location()`, `config_theme()` from Task 4
- Produces: a firmware whose watch list comes from NVS. No new symbols.

- [ ] **Step 1: Remove the static_assert**

In `src/fetcher.cpp`, delete line 30 entirely:

```c
static_assert(N_WATCHES <= MAX_WATCHES, "watch_config declares more watches than a Snapshot holds");
```

It has to go from *here*: Step 2 removes `#include "watch_config.h"` from this file, so `N_WATCHES` is no longer even in scope.

**It does not go away, though.** An earlier draft of this step claimed "the count is no longer a compile-time constant" and left it at that. That is wrong, and acting on it would have silently retired a real invariant: `N_WATCHES` is still `constexpr` (`src/watch_config.h:17`), and `seed_from_compiled_defaults()` in `src/config.cpp` still loops over it. Task 4 therefore **relocated** the guard rather than deleting it — `src/config.cpp` now carries:

```c
static_assert(N_WATCHES <= MAX_WATCHES,
              "watch_config.h declares more watches than a Config can hold");
```

Verified to fire: adding a fifth watch to `watch_config.h` fails the build with that message. Do not add it back to `fetcher.cpp`.

For the *runtime* path — config arriving from NVS rather than the header — the invariant is enforced at the boundary instead: `cfg_parse` rejects more than `MAX_WATCHES` (Task 2's `test_rejects_too_many_watches`) and `cfg_publish` clamps.

- [ ] **Step 2: Swap the header and the reads in `fetcher.cpp`**

Replace `#include "watch_config.h"` with `#include "config.h"`, then:

- line ~290 in `resolve()`: `const WatchConfig& cfg = config_watches()[i];`
- line ~355 in `refresh_schedule()`: `const WatchConfig& cfg = config_watches()[i];`
- line ~575 in the state log: `config_watches()[i].stop_code` in place of `WATCHES[i].stop_code`
- line ~705 in `fetcher_begin()`: `g_work.n_watches = config_n_watches();`
- line ~707: `for (int i = 0; i < config_n_watches(); i++) {`

- [ ] **Step 3: Wire `main.cpp`**

Replace `#include "watch_config.h"` with `#include "config.h"`. In `board_now()` at line 60:

```c
return build_board(snap, config_watches(), config_location(), now, config_theme());
```

In `setup()`, call `config_begin()` before `fetcher_begin()` — it must run first, because `fetcher_begin()` reads `config_n_watches()`. Put it immediately after `WiFi.mode(WIFI_STA);` so the log line lands early:

```c
  config_begin();
```

- [ ] **Step 4: Build both environments**

Run: `pio run -e esp32` and `pio run -e esp32_demo`
Expected: both SUCCESS. `esp32_demo` does not compile the live path, so if it fails you have put a `config.h` include behind the wrong `#ifdef`.

- [ ] **Step 5: FLASH and verify nothing changed**

**Ask the human to put the board in download mode first: hold BOOT, tap EN, release BOOT.** Then:

Run: `pio run -e esp32 -t upload` followed by `pio device monitor`

Check, on the panel and in the log:
1. `config: Kingsland, 2 watches, theme 0 (compiled defaults)` on the first boot — NVS is empty, so the defaults are used
2. Both lanes appear with the same labels as before this change
3. Departures arrive and count down
4. The `fps` line still reports ~15

- [ ] **Step 6: Commit**

```bash
git add src/fetcher.cpp src/main.cpp
git commit -m "Read the watch list from config instead of the header

watch_config.h is now the compiled default rather than the live
configuration. The static_assert on N_WATCHES goes with it: the count is
no longer known at compile time, and cfg_parse rejects more than
MAX_WATCHES at the boundary instead.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 6: The portal task and the read-only endpoints — **FLASH**

**Files:**
- Create: `src/portal.h`, `src/portal.cpp`
- Modify: `src/main.cpp` (call `portal_begin()`)

**Interfaces:**
- Consumes: `config_to_json()`, `config_theme()` from Task 4; `theme_count()` and `theme(i).name` from `lib/core/src/theme.h`
- Produces: `portal_begin()`. Tasks 7–11 add handlers inside `src/portal.cpp`.

During this task and the next three you reach the board by the IP address `main.cpp` already logs (`wifi: connected, ip ...`). The mDNS name arrives in Task 11.

**`src/portal.cpp`'s body must be wrapped in `#ifndef DEMO_MODE` / `#endif`.** PlatformIO compiles every `.cpp` under `src/` into *every* environment, regardless of which branch calls it. `fetcher.cpp` survives unguarded because its globals are plain data that `--gc-sections` drops; `WebServer g_server(80);` does not, because its constructor runs at boot whether or not `portal_begin()` is ever called, dragging lwIP and WiFi into the demo image. Measured: unguarded, the demo build grows from 352,777 to 465,693 bytes. Guarded, byte-identical.

**Tasks 7-11 append handlers to this file, and every one of them goes *inside* that guard.** A handler added after the `#endif` re-breaks the demo build, and the symptom is a size change, not a compile error.

- [ ] **Step 1: Write the header**

Create `src/portal.h`:

```c
#pragma once

// The LAN config UI. Runs on its own core-0 task: handleClient() must never
// run on the draw loop, which has about 8 ms of slack in a 66 ms frame.
void portal_begin();
```

- [ ] **Step 2: Write the server and its task**

Create `src/portal.cpp`:

```c
#include "portal.h"

#include <Arduino.h>
#include <WebServer.h>

#include "config.h"
#include "theme.h"

namespace {

WebServer g_server(80);

void handle_config() {
  char json[CFG_JSON_CAP];
  if (config_to_json(json, sizeof json) == 0) {
    g_server.send(500, "application/json", "{\"error\":\"could not serialise config\"}");
    return;
  }
  g_server.send(200, "application/json", json);
}

void handle_themes() {
  char json[256];
  size_t p = snprintf(json, sizeof json, "{\"selected\":%u,\"themes\":[",
                      static_cast<unsigned>(config_theme()));
  for (uint8_t i = 0; i < theme_count() && p < sizeof(json) - 2; i++) {
    p += snprintf(json + p, sizeof(json) - p, "%s\"%s\"", i ? "," : "", theme(i).name);
  }
  snprintf(json + p, sizeof(json) - p, "]}");
  g_server.send(200, "application/json", json);
}

void handle_not_found() { g_server.send(404, "text/plain", "not found"); }

void portal_task(void*) {
  g_server.on("/api/config", HTTP_GET, handle_config);
  g_server.on("/api/themes", HTTP_GET, handle_themes);
  g_server.onNotFound(handle_not_found);
  g_server.begin();
  Serial.println("portal: listening on :80");

  for (;;) {
    g_server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(2));  // yields to the fetch task on this core
  }
}

}  // namespace

void portal_begin() {
  // Core 0 alongside the fetcher (fetcher.cpp), leaving core 1 for drawing.
  xTaskCreatePinnedToCore(portal_task, "portal", 16384, nullptr, 1, nullptr, 0);
}
```

- [ ] **Step 3: Report the stack high-water mark**

The 16 KB stack (not the 8192 an earlier draft specified: Task 8 performs a full TLS handshake from this task, which is what the fetch task is given 16384 for) is still to be confirmed by measurement. Inside `portal_task`, alongside the `handleClient()` loop, log the headroom every 30 seconds the same way `fetcher.cpp:626` does:

```c
    static uint32_t last_hw = 0;
    const uint32_t now_ms = millis();
    if (now_ms - last_hw >= 30000) {
      last_hw = now_ms;
      Serial.printf("portal: stack free %u\n",
                    static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    }
```

- [ ] **Step 4: Start it from `setup()`**

In `src/main.cpp`, add `#include "portal.h"` inside the `#else` (live) branch, and call `portal_begin();` immediately after `fetcher_begin();`.

- [ ] **Step 5: Build**

Run: `pio run -e esp32`
Expected: SUCCESS. Note the flash figure; it should be roughly +25 KB on the Task 5 number.

- [ ] **Step 6: FLASH and verify**

**Ask the human for download mode: hold BOOT, tap EN, release BOOT.** Then `pio run -e esp32 -t upload` and `pio device monitor`.

Take the IP from the `wifi: connected, ip ...` line and check from the PC:

```bash
curl http://<ip>/api/config
curl http://<ip>/api/themes
curl -i http://<ip>/nope
```

Expected: the config document; `{"selected":0,"themes":["transit","ghibli"]}`; a 404.

Then the things that matter more than the endpoints:
1. `fps` stays ~15 in the monitor while you repeatedly curl the board
2. `portal: stack free` is comfortably above zero — if it is under about 1500, raise the stack from 16384 and reflash
3. `largest` in the existing report line does not fall away over a few minutes of requests

- [ ] **Step 7: Commit**

```bash
git add src/portal.h src/portal.cpp src/main.cpp
git commit -m "Serve config and themes from a core-0 portal task

handleClient() cannot run on the draw loop: Ghibli frames already spend
58-59 ms of a 66 ms budget, so a socket accept inside that would drop
frames. The task sits on core 0 with the fetcher and yields every 2 ms.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 7: Live theme switching — **FLASH**

**Every POST handler from here on reads `g_server.arg("plain")`.** `WebServer`
only puts the raw body there when the content type is *not*
`application/x-www-form-urlencoded` — that type gets parsed into named args
instead, leaving `"plain"` empty. `fetch()` with a string body sends
`text/plain`, so the page works; `curl -d` defaults to form-urlencoded, so every
`curl` below must pass `-H "Content-Type: application/json"` or it will look
like the handler is broken when it is not.

**Files:**
- Modify: `src/portal.cpp`

**Interfaces:**
- Consumes: `config_set_theme()` from Task 4
- Produces: `POST /api/theme` accepting `{"theme":<n>}`

- [ ] **Step 1: Add the handler**

In `src/portal.cpp`, add `#include <ArduinoJson.h>` and:

```c
void handle_set_theme() {
  JsonDocument doc;
  if (deserializeJson(doc, g_server.arg("plain"))) {
    g_server.send(400, "application/json", "{\"error\":\"bad JSON\"}");
    return;
  }
  if (doc["theme"].isNull()) {
    g_server.send(400, "application/json", "{\"error\":\"theme is required\"}");
    return;
  }
  const uint8_t t = doc["theme"].as<uint8_t>();
  if (t >= theme_count()) {
    g_server.send(400, "application/json", "{\"error\":\"no such theme\"}");
    return;
  }
  config_set_theme(t);
  char body[64];
  snprintf(body, sizeof body, "{\"theme\":%u}", static_cast<unsigned>(t));
  g_server.send(200, "application/json", body);
}
```

Register it in `portal_task` beside the others:

```c
  g_server.on("/api/theme", HTTP_POST, handle_set_theme);
```

- [ ] **Step 2: Build**

Run: `pio run -e esp32`
Expected: SUCCESS.

- [ ] **Step 3: FLASH and verify the live switch**

**Download mode first: hold BOOT, tap EN, release BOOT.** Then upload and monitor.

```bash
curl -X POST -H "Content-Type: application/json" -d '{"theme":1}' http://<ip>/api/theme
curl -X POST -H "Content-Type: application/json" -d '{"theme":0}' http://<ip>/api/theme
curl -X POST -H "Content-Type: application/json" -d '{"theme":9}' http://<ip>/api/theme
```

Expected:
1. The panel changes to Ghibli within one frame of the first call — **no reboot**
2. Back to transit on the second
3. The third gives 400 and the panel does not change
4. Power-cycle the board: it comes back on whichever theme was last set, and the log says `(nvs)` rather than `(compiled defaults)`
5. `fps` still ~15 on Ghibli, which is the expensive theme

- [ ] **Step 4: Commit**

```bash
git add src/portal.cpp
git commit -m "Switch theme live from the portal

The theme is the one value written at runtime: a uint8_t store is atomic
and the draw loop reads it once per frame, so there is nothing to lock.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 8: Stop-code validation — **FLASH**

**Files:**
- Modify: `src/portal.cpp`

**Interfaces:**
- Consumes: `StopInfo::stop_name` from Task 1; `at_get()` (`src/at_client.h`), `url_stop_by_code()`, `stop_filter()`, `parse_stop()` (`lib/core/src/at_api.h`)
- Produces: `POST /api/stop` accepting `{"stop_code":"8213"}`

This handler makes a TLS request from the portal task. 8 KB of stack is unlikely to be enough for `WiFiClientSecure` — the fetch task uses 16 KB for the same work. Watch the high-water mark closely here and raise the stack if it drops.

- [ ] **Step 1: Add the handler**

In `src/portal.cpp`, add `#include "at_api.h"` and `#include "at_client.h"`, then:

```c
void handle_check_stop() {
  JsonDocument req;
  if (deserializeJson(req, g_server.arg("plain"))) {
    g_server.send(400, "application/json", "{\"error\":\"bad JSON\"}");
    return;
  }
  const char* code = req["stop_code"] | "";
  if (code[0] == '\0') {
    g_server.send(400, "application/json", "{\"error\":\"stop_code is required\"}");
    return;
  }

  char url[256];
  if (!url_stop_by_code(url, sizeof url, code)) {
    g_server.send(400, "application/json", "{\"error\":\"stop code is not usable in a URL\"}");
    return;
  }

  JsonDocument filter;
  stop_filter(filter);
  JsonDocument doc;
  const int status = at_get(url, doc, filter);
  if (status != 200) {
    char body[96];
    snprintf(body, sizeof body, "{\"error\":\"AT returned %d\"}", status);
    g_server.send(502, "application/json", body);
    return;
  }

  StopInfo info{};
  const bool found = parse_stop(doc, &info);
  doc.clear();
  if (!found) {
    g_server.send(404, "application/json", "{\"error\":\"no stop with that code\"}");
    return;
  }

  JsonDocument out;
  out["stop_code"] = code;
  out["stop_name"] = info.stop_name;
  char body[192];
  serializeJson(out, body, sizeof body);
  g_server.send(200, "application/json", body);
}
```

Register it:

```c
  g_server.on("/api/stop", HTTP_POST, handle_check_stop);
```

- [ ] **Step 2: Build**

Run: `pio run -e esp32`
Expected: SUCCESS.

- [ ] **Step 3: FLASH and verify**

**Download mode: hold BOOT, tap EN, release BOOT.** Upload and monitor.

```bash
curl -X POST -H "Content-Type: application/json" -d '{"stop_code":"1060"}' http://<ip>/api/stop
curl -X POST -H "Content-Type: application/json" -d '{"stop_code":"8213"}' http://<ip>/api/stop
curl -X POST -H "Content-Type: application/json" -d '{"stop_code":"99999999"}' http://<ip>/api/stop
```

Expected: `{"stop_code":"1060","stop_name":"Wynyard Quarter"}`; a name for 8213; a 404 for the nonsense code.

Then the two that actually matter:
1. `portal: stack free` after a validation call — if this is under ~1500, raise the stack to 12288 or 16384 and reflash before continuing
2. `fps` holds ~15 during the call, and the departures keep updating: the portal doing TLS must not starve the fetch task on the same core

- [ ] **Step 4: Commit**

```bash
git add src/portal.cpp
git commit -m "Confirm a stop code against AT before it is saved

A typo would otherwise show as a silently empty lane, which is the
failure mode hardware-notes.md already warns about.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 9: Saving the config — **FLASH**

**Files:**
- Modify: `src/portal.cpp`

**Interfaces:**
- Consumes: `config_save_json()` and `cfg_error_text()` from Tasks 2 and 4
- Produces: `POST /api/config`, which persists and then reboots

- [ ] **Step 1: Add the handler**

The ordering here is the whole point: respond *then* reboot, and never touch the in-RAM config (spec §6).

```c
void handle_save_config() {
  const String& body = g_server.arg("plain");
  const CfgError e = config_save_json(body.c_str());
  if (e != CfgError::Ok) {
    JsonDocument out;
    out["error"] = cfg_error_text(e);
    char reply[192];
    serializeJson(out, reply, sizeof reply);
    g_server.send(400, "application/json", reply);
    return;
  }

  g_server.send(200, "application/json", "{\"saved\":true,\"restarting\":true}");
  g_server.client().flush();
  Serial.println("portal: config saved, restarting");
  delay(250);  // let the response leave before the stack goes down
  ESP.restart();
}
```

Register it:

```c
  g_server.on("/api/config", HTTP_POST, handle_save_config);
```

Note `/api/config` now has both a GET and a POST handler; `WebServer` dispatches on the method, so both registrations coexist.

- [ ] **Step 2: Build**

Run: `pio run -e esp32`
Expected: SUCCESS.

- [ ] **Step 3: FLASH and verify the save path**

**Download mode: hold BOOT, tap EN, release BOOT.** Upload and monitor.

First a rejection, which must *not* reboot:

```bash
curl -i -X POST -H "Content-Type: application/json" -d '{"v":1,"location":"X","theme":0,"watches":[]}' http://<ip>/api/config
```

Expected: 400 with `at least one enabled watch is required`, the board keeps running, the panel is unchanged.

Then a real save — change one watch to a different stop:

```bash
curl -i -X POST -H "Content-Type: application/json" -d '{"v":1,"location":"Kingsland","theme":0,"watches":[{"label":"to Wynyard Quarter","stop_code":"8213","route_short_name":"20","toward_stop_code":"1060","enabled":true}]}' http://<ip>/api/config
```

Expected:
1. The 200 arrives complete before the board resets — curl must not report a truncated response
2. The monitor shows the reboot, then `config: Kingsland, 1 watches, theme 0 (nvs)`
3. The panel comes back showing one lane, which then fills with departures
4. Power-cycle: the single-lane config survives

Finally restore two watches with another POST, and confirm the board returns to normal.

- [ ] **Step 4: Commit**

```bash
git add src/portal.cpp
git commit -m "Save the config to NVS and restart to pick it up

The response is flushed before ESP.restart() so the browser sees the 200,
and the in-RAM config is left alone: the fetch task is reading it on the
other core, and config_begin() re-reads NVS when nothing else is.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 10: The config page — **FLASH**

**Files:**
- Create: `src/portal_page.h`
- Modify: `src/portal.cpp` (serve it at `GET /`)

**Interfaces:**
- Consumes: every endpoint from Tasks 6–9
- Produces: `PORTAL_PAGE`, a `PROGMEM` string

The page is one self-contained file: no external CSS, no framework, no fonts. Nothing on the LAN can fetch from a CDN and the board cannot serve one.

- [ ] **Step 1: Write the page**

Create `src/portal_page.h`. `maxlength` values are one less than the caps in `config_schema.h` (23 for location, 31 for watch fields), so the browser stops input before `cfg_parse` has to reject it.

```c
#pragma once
#include <Arduino.h>

const char PORTAL_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Departure board setup</title>
<style>
 :root{color-scheme:light dark}
 body{font:15px system-ui,sans-serif;margin:0;padding:16px;max-width:760px}
 h1{font-size:20px;margin:0 0 16px}
 fieldset{border:1px solid #8886;border-radius:6px;margin:0 0 14px;padding:12px}
 legend{padding:0 6px;font-weight:600}
 label{display:block;font-size:13px;opacity:.8;margin:8px 0 2px}
 input[type=text]{width:100%;box-sizing:border-box;padding:6px;font:inherit;
   border:1px solid #8886;border-radius:4px;background:transparent;color:inherit}
 .row{display:flex;gap:10px;flex-wrap:wrap}
 .row>div{flex:1 1 150px}
 button{font:inherit;padding:6px 12px;border-radius:4px;border:1px solid #8886;
   background:transparent;color:inherit;cursor:pointer}
 button.primary{background:#2563eb;border-color:#2563eb;color:#fff;padding:8px 20px}
 .note{font-size:12px;opacity:.7;margin:4px 0 0}
 .ok{color:#15803d}.bad{color:#b91c1c}
 #status{margin-left:12px;font-size:13px}
</style>
</head>
<body>
<h1>Departure board setup</h1>

<fieldset><legend>Board</legend>
 <label for="loc">Location shown on the panel</label>
 <input type="text" id="loc" maxlength="23">
 <label for="theme">Theme</label>
 <select id="theme"></select>
 <p class="note">The theme changes on the panel straight away. Everything else
  applies when you save, which restarts the board.</p>
</fieldset>

<fieldset><legend>Watches</legend>
 <div id="watches"></div>
 <button type="button" id="add">Add watch</button>
 <p class="note">Maximum four. Stop code is the number on the pole. Leave the
  route blank to show every route at that stop. The toward stop is one further
  along in the direction you care about &mdash; it is not a compass direction.</p>
</fieldset>

<button type="button" class="primary" id="save">Save and restart</button>
<span id="status"></span>

<script>
const $ = s => document.querySelector(s);
let watches = [];

function render() {
  $('#watches').innerHTML = '';
  watches.forEach((w, i) => {
    const d = document.createElement('div');
    d.className = 'row';
    d.style.cssText = 'border-top:1px solid #8883;padding:10px 0;align-items:flex-end';
    d.innerHTML =
      '<div><label>Label</label><input type="text" maxlength="31" data-k="label"></div>' +
      '<div><label>Stop code</label><input type="text" maxlength="31" data-k="stop_code"></div>' +
      '<div><label>Route</label><input type="text" maxlength="31" data-k="route_short_name"></div>' +
      '<div><label>Toward stop</label><input type="text" maxlength="31" data-k="toward_stop_code"></div>' +
      '<div style="flex:0 0 auto"><label><input type="checkbox" data-k="enabled"> On</label></div>' +
      '<div style="flex:0 0 auto"><button type="button" data-a="check">Check</button> ' +
      '<button type="button" data-a="del">Remove</button></div>' +
      '<div style="flex:1 1 100%" class="note" data-r=""></div>';
    d.querySelectorAll('[data-k]').forEach(el => {
      const k = el.dataset.k;
      if (k === 'enabled') { el.checked = w.enabled; el.onchange = () => w.enabled = el.checked; }
      else { el.value = w[k] || ''; el.oninput = () => w[k] = el.value; }
    });
    d.querySelector('[data-a=del]').onclick = () => { watches.splice(i, 1); render(); };
    d.querySelector('[data-a=check]').onclick = () => check(i, d.querySelector('[data-r]'));
    $('#watches').appendChild(d);
  });
  $('#add').disabled = watches.length >= 4;
}

async function check(i, out) {
  out.textContent = 'checking…';
  out.className = 'note';
  try {
    const r = await fetch('/api/stop', {
      method: 'POST', body: JSON.stringify({stop_code: watches[i].stop_code})
    });
    const j = await r.json();
    out.textContent = r.ok ? j.stop_name : j.error;
    out.className = 'note ' + (r.ok ? 'ok' : 'bad');
  } catch (e) { out.textContent = 'the board did not answer'; out.className = 'note bad'; }
}

async function load() {
  const cfg = await (await fetch('/api/config')).json();
  $('#loc').value = cfg.location;
  watches = cfg.watches;
  render();
  const t = await (await fetch('/api/themes')).json();
  $('#theme').innerHTML = t.themes
    .map((n, i) => '<option value="' + i + '">' + n + '</option>').join('');
  $('#theme').value = t.selected;
}

$('#add').onclick = () => {
  if (watches.length >= 4) return;
  watches.push({label: '', stop_code: '', route_short_name: '',
                toward_stop_code: '', enabled: true});
  render();
};

$('#theme').onchange = async e => {
  await fetch('/api/theme', {
    method: 'POST', body: JSON.stringify({theme: Number(e.target.value)})
  });
};

$('#save').onclick = async () => {
  const s = $('#status');
  s.textContent = 'saving…';
  s.className = '';
  const body = JSON.stringify({
    v: 1, location: $('#loc').value, theme: Number($('#theme').value), watches: watches
  });
  let r, j;
  try { r = await fetch('/api/config', {method: 'POST', body: body}); j = await r.json(); }
  catch (e) { s.textContent = 'the board did not answer'; s.className = 'bad'; return; }
  if (!r.ok) { s.textContent = j.error; s.className = 'bad'; return; }
  s.textContent = 'restarting…';
  const wait = setInterval(async () => {
    try {
      await fetch('/api/config', {cache: 'no-store'});
      clearInterval(wait);
      s.textContent = 'back up';
      s.className = 'ok';
      load();
    } catch (e) { /* still down - keep waiting */ }
  }, 1000);
};

load();
</script>
</body>
</html>)HTML";
```

Two details worth not losing: `$('#add').disabled` is what enforces the four-watch cap in the page, and the board enforces it again with `CfgError::TooManyWatches`; and the save poller uses `cache: 'no-store'` so a cached 200 cannot make the page think the board is back before it is.

- [ ] **Step 2: Serve it**

In `src/portal.cpp`, add `#include "portal_page.h"` and:

```c
void handle_root() {
  g_server.send_P(200, "text/html", PORTAL_PAGE);
}
```

Register it first among the routes:

```c
  g_server.on("/", HTTP_GET, handle_root);
```

`send_P` streams straight from flash rather than copying the page into a `String`, which matters for the largest-free-block number.

- [ ] **Step 3: Build and check the size**

Run: `pio run -e esp32`
Expected: SUCCESS, and flash still well under 1,310,720. If the page has pushed the build past about 1,150,000, gzip it and serve with `Content-Encoding: gzip` as the spec's §5 reserve plan.

- [ ] **Step 4: FLASH and drive it from a browser**

**Download mode: hold BOOT, tap EN, release BOOT.** Upload, then open `http://<ip>/`.

Work through the whole thing by hand:
1. The page loads and shows the current location, watches and theme
2. Changing the theme in the dropdown changes the panel immediately, with no page reload and no reboot
3. Check on a good stop code shows the stop name; Check on a bad one shows an error
4. Adding a fifth watch is refused, by the page or by the 400 from the board
5. Save with an empty stop code shows the validation error and does not reboot
6. A real Save reboots the board and the page reconnects on its own
7. The panel shows the new watch afterwards
8. `fps` stays ~15 with the page open

- [ ] **Step 5: Commit**

```bash
git add src/portal_page.h src/portal.cpp
git commit -m "Add the config page

One self-contained file served straight from flash with send_P: nothing
on the LAN can reach a CDN, and copying the page into a String would cost
the largest free block the board watches.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 11: mDNS and the address on the panel — **FLASH**

**Files:**
- Modify: `src/portal.cpp` (mDNS), `src/main.cpp` (the boot screen)

**Interfaces:**
- Consumes: `portal_begin()` from Task 6
- Produces: `at-board.local`, and the address shown on the panel at boot

- [ ] **Step 1: Register the mDNS name**

In `src/portal.cpp`, add `#include <ESPmDNS.h>` and, at the top of `portal_task` before `g_server.begin()`:

```c
  if (MDNS.begin("at-board")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("portal: http://at-board.local");
  } else {
    Serial.println("portal: mDNS registration failed, use the IP");
  }
```

- [ ] **Step 2: Draw the address at boot**

In `src/main.cpp`, inside the live branch of `setup()` after the WiFi result is known and before `fetcher_begin()`, draw straight to `tft` — the `Board` model is not involved, so neither the simulator nor `tools/board/model.py` changes:

```c
  if (wifi_ok) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("Config", 12, 40, 4);
    tft.drawString("http://at-board.local", 12, 80, 2);
    tft.drawString(WiFi.localIP().toString().c_str(), 12, 104, 2);
    tft.drawString("or this address if that name fails", 12, 132, 1);
    delay(4000);
  }
```

Four seconds is dead time: the first fetch has not landed, so the lanes would say "starting" regardless.

- [ ] **Step 3: Build**

Run: `pio run -e esp32`
Expected: SUCCESS.

- [ ] **Step 4: FLASH and verify**

**Download mode: hold BOOT, tap EN, release BOOT.** Upload and monitor.

1. The panel shows the URL and IP for four seconds at boot, in ASCII only, nothing clipped at the right edge
2. It then goes to the normal board and departures arrive as usual
3. `curl http://at-board.local/api/config` works from the PC (on Windows this may need Bonjour; the IP is the documented fallback and must still work)
4. `fps` unaffected once running

- [ ] **Step 5: Commit**

```bash
git add src/portal.cpp src/main.cpp
git commit -m "Register at-board.local and show it at boot

The IP is drawn beside the name because mDNS does not resolve on Windows
without Bonjour. The boot screen uses time the board was already spending
waiting for its first fetch.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

### Task 12: Measure, then correct the docs

The spec's appendix predicted the cost. This closes the loop with the real number and fixes the one place in the repo that now says something false.

**Files:**
- Modify: `docs/hardware-notes.md:293` (the "Flash headroom" section)
- Modify: `README.md:54-60` (the "no setup portal yet" paragraph)
- Modify: `docs/superpowers/specs/2026-09-06-at-departure-board-design.md:8` (the status line)
- Modify: `docs/superpowers/specs/2026-09-19-config-portal-design.md` (status, and the appendix's real figure)

- [ ] **Step 1: Take the final measurements**

Run: `pio run -e esp32` and record the flash and RAM figures.

Then, with the board running and the config page open, record from the monitor: the `fps` line, `heap`, `largest`, `min-ever`, and `portal: stack free`.

- [ ] **Step 2: Run a soak**

Leave the board running for at least an hour with the page open in a browser tab. Check afterwards that `min-ever` and `largest` have not drifted downward — `WebServer` allocates a `String` per request, and fragmentation is the risk the existing report line exists to catch.

- [ ] **Step 3: Correct `docs/hardware-notes.md`**

Replace the "Flash headroom" section's prediction with the measured outcome: the portal cost, the resulting total, and the fact that the default partition table was sufficient after all — `huge_app.csv` was not needed. Keep it to the measured numbers.

- [ ] **Step 4: Update `README.md`**

The two-step setup instructions are no longer the whole story. `src/secrets.h` is still required, and `src/watch_config.h` is still where the defaults live, but stops and theme are now changed at `http://at-board.local` after first boot. Say that, and say that a reflash is only needed for WiFi credentials and the API key.

- [ ] **Step 5: Update both specs' status lines**

In the design spec, mark §3 as partially implemented, naming what landed and what is still deferred (WiFi credentials, API key, the AP, the captive portal). In `2026-09-19-config-portal-design.md`, change `Status: designed, not built` to built, and replace the appendix's projected delta with the measured one.

- [ ] **Step 6: Commit**

```bash
git add docs/ README.md
git commit -m "Record what the portal actually cost

hardware-notes predicted the portal would need huge_app.csv. It did not:
the default partition table had room. Replaces the prediction with the
measurement and points the README at at-board.local for stop and theme
changes.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Deferred

Out of scope for this plan, per spec §11 — each needs its own design pass:

- WiFi credentials and the AT API key in NVS
- The `at-board-setup` AP and the captive portal
- The `BOOT → PORTAL` state-machine edge
- Authentication on the page, which stops being optional the moment the page can disclose the API key
