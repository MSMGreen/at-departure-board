#include "fetcher.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>
#include <time.h>

#include "at_api.h"
#include "at_client.h"
#include "secrets.h"
#include "watch_config.h"

// The three loops of spec 5, folded into one task so that only one thing ever
// holds a TLS session: schedule every 15 minutes (and on a date rollover),
// realtime every 30 s while something is close and every 2 minutes otherwise,
// and stop/route resolution once, lazily, per watch.
//
// Everything decision-shaped - which direction is city-bound, which rows to
// keep, which delay to believe - already lives in lib/core and is tested on a
// laptop. What is left here is I/O, cadence and the error surfaces of spec 8.

namespace {

static_assert(N_WATCHES <= MAX_WATCHES, "watch_config declares more watches than a Snapshot holds");

// --- cadences (spec 5) ------------------------------------------------------
constexpr int32_t SCHEDULE_PERIOD_S = 15 * 60;
constexpr int32_t RT_FAST_S = 30;
constexpr int32_t RT_SLOW_S = 120;
constexpr int32_t RT_FAST_WITHIN_S = 30 * 60;
constexpr int32_t BACKOFF_CAP_S = 300;
constexpr int32_t NO_LINK_RETRY_S = 5;   // no WiFi or no clock yet
constexpr int32_t RESOLVE_RETRY_S = 60;  // a 400/401 must not become a request storm
constexpr int32_t SCHEDULE_RETRY_S = 60; // after an inconclusive refresh: soon, not every tick

// --- buffer sizes -----------------------------------------------------------
constexpr int MAX_SCHED_ROWS = 48;  // 3 hours at a busy stop, both windows
constexpr int MAX_TRIP_STOPS = 96;  // a full rail line end to end, with room
constexpr int MAX_ENTITIES = 32;    // 24 asked for; the tripid filter overshoots
constexpr int MAX_RT_IDS = 24;
constexpr int RT_PER_WATCH = 6;
constexpr int MAX_RAIL = 12;  // 8 rail routes today, both sides of the rename
constexpr int MAX_PINNED = 2;

// at_get returns an HTTP status or a negative; this is the one extra outcome
// the callers need: the server said 200 and the data array was empty.
constexpr int AT_EMPTY = 204;

// --- state ------------------------------------------------------------------
// Every buffer below is file scope. A Snapshot on its own is larger than the
// Arduino loop task's whole stack, and what a TLS session needs is most of
// what this task's 16 KB has.

SemaphoreHandle_t g_lock = nullptr;
Snapshot g_published;  // what the render loop copies
Snapshot g_work;       // what this task mutates

struct Resolved {
  char stop_id[STOP_ID_LEN];
  char target_stop_id[STOP_ID_LEN];
  char route_ids[MAX_PINNED][ROUTE_ID_LEN];
  int n_routes;
  bool resolved;
  int64_t next_try;    // resolution is rate limited; see RESOLVE_RETRY_S
  int64_t next_sched;  // when this watch's schedule is next due
  CivilDate sched_date;
};
Resolved g_res[MAX_WATCHES];

RouteInfo g_rail[MAX_RAIL];
int g_n_rail = 0;
bool g_rail_loaded = false;

RouteInfo g_routes[8];  // the answer to one filter[route_short_name]
StopTripRow g_rows[MAX_SCHED_ROWS];
// The (route, direction) pairs of the watch being refreshed. Shared across
// watches like g_rows: they are only read inside the refresh that fills them.
RouteDir g_pairs[MAX_ROUTE_DIRS];
bool g_pair_failed[MAX_ROUTE_DIRS];  // its trips/{id}/stops did not answer 200
char g_log[200];  // the "dirs" line: six pairs of up to ~26 characters
TripStop g_stops[MAX_TRIP_STOPS];
RtEntity g_ents[MAX_ENTITIES];
const char* g_ids[MAX_RT_IDS];  // pointers into g_work's own rows
FetchWindow g_win[2];

JsonDocument g_doc;
JsonDocument g_filter;
char g_url[1536];  // 24 trip ids of up to 47 characters, plus the base

int32_t g_backoff = 0;  // 0 when not backing off
bool g_time_started = false;
WatchState g_logged[MAX_WATCHES];

// Folded over every request in a pass, to decide backoff and last_ok once.
struct Pass {
  // Set only by a conclusive schedule refresh or a realtime 200. A resolve or a
  // rail lookup that happens to 200 is not news about departures, and an
  // inconclusive refresh is not evidence of anything: counting either would let
  // a board drain while it still looked fresh.
  bool news;
  bool backoff;
};
Pass g_pass;

// --- plumbing ---------------------------------------------------------------

void publish() {
  xSemaphoreTake(g_lock, portMAX_DELAY);
  memcpy(&g_published, &g_work, sizeof g_published);
  xSemaphoreGive(g_lock);
}

// Spec 8: 429, 5xx and anything that never reached the server. A 400 or a 401
// is a configuration fault - backing off would only delay the fix.
bool should_backoff(int status) {
  return status == AT_TRANSPORT_ERROR || status == AT_PARSE_ERROR || status == 429 || status >= 500;
}

// One GET against g_url with the given response filter. The doc and the filter
// are file scope so neither lands on the stack next to a TLS session.
int get(void (*make_filter)(JsonDocument&)) {
  g_filter.clear();
  make_filter(g_filter);
  const int status = at_get(g_url, g_doc, g_filter);
  if (should_backoff(status)) g_pass.backoff = true;
  return status;
}

bool ensure_wifi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  static uint32_t last_begin = 0;
  const uint32_t ms = millis();
  if (last_begin == 0 || ms - last_begin > 15000) {  // let an attempt finish first
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    last_begin = ms;
  }
  return false;
}

bool ensure_time() {
  if (!g_time_started) {
    configTime(0, 0, "pool.ntp.org");  // UTC; nztime does the local conversion
    g_time_started = true;
  }
  return time(nullptr) > 1700000000;
}

// A 401 is not transient and is not backed off, so both the resolution and the
// schedule retries are rate limited by hand: a wrong key must cost one request
// a minute, not one every 30 seconds.
void set_all(WatchState s, int64_t now) {
  for (int i = 0; i < g_work.n_watches; i++) {
    g_work.watches[i].state = s;
    g_res[i].next_try = now + RESOLVE_RETRY_S;
    if (g_res[i].next_sched < now + RESOLVE_RETRY_S) g_res[i].next_sched = now + RESOLVE_RETRY_S;
  }
}

// --- badges and colours -----------------------------------------------------

const RouteInfo* rail_route(const char* route_id) {
  for (int i = 0; i < g_n_rail; i++) {
    if (strcmp(g_rail[i].route_id, route_id) == 0) return &g_rail[i];
  }
  return nullptr;
}

// A route in the rail list is a train, badged and coloured from the feed. Any
// other route is a bus, badged with its route_id up to the last '-' ("20-202"
// -> "20"), and uncoloured: AT does not colour buses (docs/at-api-notes.md).
void style_from_route(LiveWatch& w, const char* route_id) {
  const RouteInfo* r = rail_route(route_id);
  if (r != nullptr) {
    w.kind = Kind::Train;
    strncpy(w.badge, r->short_name, sizeof w.badge - 1);
    w.badge[sizeof w.badge - 1] = '\0';
    w.has_route_color = r->has_color;
    w.route_color = r->color;
    return;
  }
  w.kind = Kind::Bus;
  const char* dash = strrchr(route_id, '-');
  size_t len = dash != nullptr ? static_cast<size_t>(dash - route_id) : strlen(route_id);
  if (len > sizeof w.badge - 1) len = sizeof w.badge - 1;
  memcpy(w.badge, route_id, len);
  w.badge[len] = '\0';
  w.has_route_color = false;
}

bool route_wanted(const Resolved& r, const char* route_id) {
  if (r.n_routes == 0) return true;  // an unpinned watch takes whatever runs
  for (int k = 0; k < r.n_routes; k++) {
    if (strcmp(r.route_ids[k], route_id) == 0) return true;
  }
  return false;
}

const char* route_of(const char* trip_id, int n) {
  for (int k = 0; k < n; k++) {
    if (strcmp(g_rows[k].trip_id, trip_id) == 0) return g_rows[k].route_id;
  }
  return nullptr;
}

// True when some row's (route, direction) did not make it into g_pairs, i.e.
// collect_route_dirs really truncated rather than finding exactly six pairs.
bool pair_cap_dropped(int n) {
  for (int k = 0; k < n; k++) {
    const StopTripRow& row = g_rows[k];
    if (row.direction_id < 0 || row.direction_id > 1) continue;
    bool found = false;
    for (int p = 0; p < MAX_ROUTE_DIRS; p++) {
      if (g_pairs[p].direction_id == row.direction_id &&
          strcmp(g_pairs[p].route_id, row.route_id) == 0) {
        found = true;
        break;
      }
    }
    if (!found) return true;
  }
  return false;
}

// "dirs 122: E-W-201/0 no  E-W-201/1 yes  O-W-201/1 err -> ok". A pair whose
// probe failed says err, never no, so a partial failure shows on serial.
void log_pairs(const char* stop_code, int n_pairs, const char* outcome) {
  int p = 0;
  for (int k = 0; k < n_pairs && p < static_cast<int>(sizeof g_log) - 1; k++) {
    const char* said = g_pair_failed[k] ? "err" : g_pairs[k].serves ? "yes" : "no";
    p += snprintf(g_log + p, sizeof g_log - p, "%s%s/%d %s", k == 0 ? "" : "  ",
                  g_pairs[k].route_id, static_cast<int>(g_pairs[k].direction_id), said);
  }
  if (p == 0) snprintf(g_log, sizeof g_log, "none");
  Serial.printf("dirs %s: %s -> %s\n", stop_code, g_log, outcome);
}

// --- the three loops --------------------------------------------------------

// The rail route table, fetched once. It is what decides Kind, and it supplies
// the badge and colour for a watch that pins no route - which is how a rail
// watch survives a line rename (docs/at-api-notes.md, the CRL changeover).
void load_rail() {
  if (!url_rail_routes(g_url, sizeof g_url)) return;
  const int status = get(routes_filter);
  if (status != 200) {
    Serial.printf("rail: HTTP %d\n", status);
    return;
  }
  g_n_rail = parse_routes(g_doc, g_rail, MAX_RAIL);
  g_rail_loaded = true;
  Serial.printf("rail: %d routes\n", g_n_rail);
}

// stop_code -> stop_id. *out is untouched unless this returns 200.
int lookup_stop(const char* stop_code, char* out, size_t n) {
  if (!url_stop_by_code(g_url, sizeof g_url, stop_code)) return AT_PARSE_ERROR;
  const int status = get(stop_filter);
  if (status != 200) return status;
  StopInfo info{};
  if (!parse_stop(g_doc, &info)) return AT_EMPTY;
  strncpy(out, info.stop_id, n - 1);
  out[n - 1] = '\0';
  return 200;
}

// The stop, the stop it points toward, and the route ids behind a pinned short
// name. Status mapping is the whole of spec 8 for this step.
void resolve(int i, int64_t now) {
  Resolved& r = g_res[i];
  LiveWatch& w = g_work.watches[i];
  const WatchConfig& cfg = WATCHES[i];

  int status = lookup_stop(cfg.stop_code, r.stop_id, sizeof r.stop_id);
  if (status == 200 && cfg.toward_stop_code[0] != '\0') {
    status = lookup_stop(cfg.toward_stop_code, r.target_stop_id, sizeof r.target_stop_id);
  }
  if (status == 200 && cfg.route_short_name[0] != '\0') {
    if (!url_routes_by_short_name(g_url, sizeof g_url, cfg.route_short_name)) {
      status = AT_PARSE_ERROR;
    } else {
      status = get(routes_filter);
      // A 400 here is a route short name AT will not accept - a config fault,
      // not the stop-code filter being withdrawn. Map it to "check config".
      if (status == 400) status = AT_EMPTY;
      if (status == 200) {
        const int n = parse_routes(g_doc, g_routes, 8);
        if (n == 0) {
          status = AT_EMPTY;
        } else {
          // Every returned route_id: a short name spans both sides of a rename.
          r.n_routes = 0;
          for (int k = 0; k < n && r.n_routes < MAX_PINNED; k++) {
            strncpy(r.route_ids[r.n_routes], g_routes[k].route_id, ROUTE_ID_LEN - 1);
            r.route_ids[r.n_routes][ROUTE_ID_LEN - 1] = '\0';
            r.n_routes++;
          }
          // A badge before the first schedule pass, so a watch waiting on an
          // empty window still says what it is waiting for.
          style_from_route(w, g_routes[0].route_id);
          if (g_routes[0].route_type == 2) w.kind = Kind::Train;
        }
      }
    }
  }

  switch (status) {
    case 200:
      r.resolved = true;
      w.state = WatchState::Starting;  // rows arrive with the schedule pass
      Serial.printf("resolve %s -> %s  toward %s -> %s  routes %d\n", cfg.stop_code, r.stop_id,
                    cfg.toward_stop_code, r.target_stop_id, r.n_routes);
      break;
    case AT_EMPTY:  // 200 with an empty data array: the code is not a real stop
      w.state = WatchState::CheckConfig;
      r.next_try = now + RESOLVE_RETRY_S;
      break;
    case 400:  // the undocumented filter[stop_code] has been withdrawn (spec 8)
      w.state = WatchState::StopLookupGone;
      r.next_try = now + RESOLVE_RETRY_S;
      break;
    case 401:
      set_all(WatchState::CheckKey, now);
      break;
    default:  // transient: leave it unresolved and try again, under backoff
      Serial.printf("resolve %s: HTTP %d\n", cfg.stop_code, status);
      break;
  }
}

// One schedule refresh for one watch. Returns false when nothing conclusive
// came back, in which case the cached rows stay up and the pass retries.
bool refresh_schedule(int i, int64_t now, const LocalTime& lt) {
  Resolved& r = g_res[i];
  LiveWatch& w = g_work.watches[i];
  const WatchConfig& cfg = WATCHES[i];

  // Stamped before the requests, not after: a failed refresh must fall back to
  // next_sched for its retry rather than looking like an unhandled rollover
  // every tick for the rest of the day.
  r.sched_date = CivilDate{lt.y, lt.m, lt.d};

  // 1 or 2 requests: schedule_windows already handles the midnight split and
  // the hour 0 the API rejects.
  const int n_win = schedule_windows(lt, g_win);
  int n = 0;
  for (int k = 0; k < n_win; k++) {
    if (!url_stoptrips(g_url, sizeof g_url, r.stop_id, g_win[k].date, g_win[k].start_hour,
                       g_win[k].hour_range)) {
      continue;
    }
    const int status = get(stoptrips_filter);
    if (status == 404) {
      // "No services in this window" - the same status as an unknown stop, and
      // never a reason to re-resolve one (spec 8, docs/at-api-notes.md). A
      // real answer: it feeds a conclusive refresh like any 200.
      continue;
    }
    if (status == 401) {
      set_all(WatchState::CheckKey, now);
      return false;
    }
    if (status != 200) {
      r.next_sched = now + SCHEDULE_RETRY_S;  // inconclusive: keep what we had
      return false;
    }
    n += parse_stoptrips(g_doc, g_rows + n, MAX_SCHED_ROWS - n);
  }

  if (r.n_routes > 0) {
    // A pinned watch never shows another route's rows, so drop them before the
    // pairs are collected: at a busy stop the pair cap would otherwise fill up
    // with other routes and could squeeze out the one this watch is for.
    int keep = 0;
    for (int k = 0; k < n; k++) {
      if (!route_wanted(r, g_rows[k].route_id)) continue;
      if (keep != k) g_rows[keep] = g_rows[k];
      keep++;
    }
    n = keep;
  }

  // Direction is derived every refresh and is a property of
  // (route_id, direction_id), never of a trip (spec 3a). Kingsland proved why:
  // it is served by E-W, which reaches Waitemata, and O-W, which runs to
  // Onehunga via Newmarket. One candidate trip per direction_id asked about
  // O-W both ways and concluded the target was unreachable, while the trains
  // the board wanted were due. So probe every pair in the window.
  const int n_pairs = collect_route_dirs(g_rows, n, g_pairs, MAX_ROUTE_DIRS);
  if (n_pairs == MAX_ROUTE_DIRS && pair_cap_dropped(n)) {
    Serial.printf("dirs %s: pair cap reached\n", cfg.stop_code);
  }

  int probed = 0, failed = 0;
  bool any_serves = false;
  for (int p = 0; p < n_pairs; p++) {
    RouteDir& pair = g_pairs[p];
    pair.serves = false;
    g_pair_failed[p] = true;  // until a 200 says otherwise
    if (!url_trip_stops(g_url, sizeof g_url, pair.trip_id)) {
      failed++;
      continue;
    }
    const int status = get(trip_stops_filter);
    if (status == 401) {
      set_all(WatchState::CheckKey, now);
      return false;
    }
    if (status != 200) {
      failed++;  // serves stays false, and that is not proof of anything
      continue;
    }
    g_pair_failed[p] = false;
    const int n_stops = parse_trip_stops(g_doc, g_stops, MAX_TRIP_STOPS);
    // Our stop here is the pair's own stop_id: a station's rows carry the
    // platform, and a trip's stop list contains platforms, not stations.
    pair.serves =
        trip_serves(g_stops, n_stops, pair.stop_id, cfg.toward_stop_code, r.target_stop_id);
    if (pair.serves) any_serves = true;
    probed++;
  }

  // Inconclusive is not evidence. A refresh is conclusive only when every pair
  // was probed, or when at least one pair proved it serves. Otherwise a failed
  // pair may be the very one that reaches the target - at Kingsland, one 5xx on
  // E-W/1 with three honest "no"s would read as "nothing goes your way". So
  // leave the previous state and rows exactly as they are, do not count this
  // as news, and come back in a minute rather than in fifteen.
  if (failed > 0 && !any_serves) {
    r.next_sched = now + SCHEDULE_RETRY_S;
    log_pairs(cfg.stop_code, n_pairs, "inconclusive, kept previous");
    Serial.printf("sched %s: parsed %d pairs %d probed %d failed %d -> kept %u rows\n",
                  cfg.stop_code, n, n_pairs, probed, failed, static_cast<unsigned>(w.n_rows));
    return false;
  }

  // From here the answer is conclusive: every pair spoke, or one proved it
  // serves (and a failed pair can then only be missing rows, never a lie).
  const WatchState verdict = verdict_for_pairs(g_pairs, n_pairs);
  if (verdict != WatchState::Ok) {
    // A service runs both ways past this stop and neither way gets you there.
    // A watch that cannot prove which way it points must not show a time.
    w.state = verdict;
    w.n_rows = 0;
  } else {
    const char* rids[MAX_PINNED] = {r.route_ids[0], r.route_ids[1]};
    w.n_rows = static_cast<uint8_t>(select_serving_rows(g_rows, n, g_pairs, n_pairs, rids,
                                                       r.n_routes, now, w.rows, MAX_ROWS));
    if (w.n_rows > 0) {
      const char* rid = route_of(w.rows[0].trip_id, n);
      if (rid != nullptr) style_from_route(w, rid);
    }
    w.state = WatchState::Ok;  // no rows is an empty board, not an error
  }

  r.next_sched = now + SCHEDULE_PERIOD_S;
  g_pass.news = true;
  const char* m = state_message(w.state);
  log_pairs(cfg.stop_code, n_pairs, m[0] != '\0' ? m : "ok");
  Serial.printf("sched %s: parsed %d pairs %d probed %d failed %d -> %u rows\n", cfg.stop_code, n,
                n_pairs, probed, failed, static_cast<unsigned>(w.n_rows));
  return true;
}

// One request covering every watch. The tripid filter is inexact, so the same
// id list goes to apply_realtime: a row we asked about and heard nothing back
// for loses its realtime rather than keeping a stale delay.
void refresh_realtime(int64_t now) {
  const int n = realtime_ids(g_work, now, g_ids, MAX_RT_IDS, RT_PER_WATCH);
  if (n == 0) return;  // nothing upcoming to ask about; not a failure
  if (!url_realtime(g_url, sizeof g_url, g_ids, n)) {
    Serial.printf("rt: url did not fit (ids %d)\n", n);
    return;
  }

  // Diagnostic (task 7a): on hardware this asked 6 ids and parsed 0 entities
  // while the same query from a laptop returned 6. Print exactly what was sent.
  Serial.printf("rt: ids %d url %.200s\n", n, g_url);
  const int status = get(realtime_filter);
  if (status == 401) {
    set_all(WatchState::CheckKey, now);
    return;
  }
  if (status != 200) {
    Serial.printf("rt: HTTP %d (asked %d)\n", status, n);
    return;
  }
  const int n_ents = parse_realtime(g_doc, g_ents, MAX_ENTITIES);
  for (int i = 0; i < g_work.n_watches; i++) {
    apply_realtime(g_work.watches[i], g_ents, n_ents, g_ids, n);
  }
  g_pass.news = true;  // folded into last_ok once, at the end of the pass

  // Realtime is the only endpoint AT sends with a content-length rather than
  // chunked, so a parse here is the proof that at_client's other branch works.
  if (n_ents > 0) {
    int s = 0;  // a sample entity, preferring one that actually carries a delay
    for (int e = 0; e < n_ents; e++) {
      if (g_ents[e].has_delay) {
        s = e;
        break;
      }
    }
    Serial.printf("rt: 200 content-length ok, asked %d entities %d, %s delay %+ld%s%s\n", n, n_ents,
                  g_ents[s].trip_id, static_cast<long>(g_ents[s].delay),
                  g_ents[s].has_delay ? "" : " (none)", g_ents[s].cancelled ? " CANCELLED" : "");
  } else {
    Serial.printf("rt: 200 content-length ok, asked %d entities 0\n", n);
  }
}

// --- the task ---------------------------------------------------------------

bool anything_soon(int64_t now) {
  for (int i = 0; i < g_work.n_watches; i++) {
    const LiveWatch& w = g_work.watches[i];
    if (w.state != WatchState::Ok) continue;
    for (int r = 0; r < w.n_rows; r++) {
      const LiveRow& row = w.rows[r];
      const int64_t eta = row.sched_epoch + (row.has_rt ? row.delay : 0) - now;
      if (eta >= 0 && eta <= RT_FAST_WITHIN_S) return true;
    }
  }
  return false;
}

// The cadence the board would be polling at if nothing were wrong. This is what
// gets published, never the backed-off pacing: poll_interval_s exists so that a
// normal two-minute gap does not read as stale, not to record how badly
// fetching is going. Publishing the backoff would hide a total outage for
// longer the worse it got - stale data that does not look stale, which is the
// one failure spec 8 is written against.
int32_t nominal_interval(int64_t now) { return anything_soon(now) ? RT_FAST_S : RT_SLOW_S; }

// What gets published: when the board next expects news. With nothing upcoming
// to ask realtime about - a quiet night - the next news is the schedule
// refresh, so a healthy board must not read stale for most of every 15 minutes
// just because realtime had nothing to do.
int32_t expected_interval(int64_t now) {
  if (realtime_ids(g_work, now, g_ids, MAX_RT_IDS, RT_PER_WATCH) == 0) return SCHEDULE_PERIOD_S;
  return nominal_interval(now);
}

void log_states() {
  for (int i = 0; i < g_work.n_watches; i++) {
    if (g_work.watches[i].state == g_logged[i]) continue;
    g_logged[i] = g_work.watches[i].state;
    const char* m = state_message(g_logged[i]);
    Serial.printf("watch %d (%s): %s\n", i, WATCHES[i].stop_code, m[0] != '\0' ? m : "ok");
  }
}

void log_pass(bool wifi, bool clock_ok, int64_t now, int32_t interval) {
  char rows[48];
  int p = 0;
  for (int i = 0; i < g_work.n_watches && p < static_cast<int>(sizeof rows) - 1; i++) {
    p += snprintf(rows + p, sizeof rows - p, "%s%u", i == 0 ? "" : "/",
                  static_cast<unsigned>(g_work.watches[i].n_rows));
  }
  if (p == 0) {
    rows[0] = '-';
    rows[1] = '\0';
  }

  long sched = -1;
  for (int i = 0; i < g_work.n_watches; i++) {
    if (!g_res[i].resolved) continue;
    long d = static_cast<long>(g_res[i].next_sched - now);
    if (d < 0) d = 0;
    if (sched < 0 || d < sched) sched = d;
  }
  char sched_s[16];
  if (sched < 0) {
    snprintf(sched_s, sizeof sched_s, "-");
  } else {
    snprintf(sched_s, sizeof sched_s, "%lds", sched);
  }

  char ok_s[20];
  if (g_work.last_ok == 0) {
    snprintf(ok_s, sizeof ok_s, "never");
  } else {
    long age = static_cast<long>(now - g_work.last_ok);  // now is 0 before NTP
    if (age < 0) age = 0;
    snprintf(ok_s, sizeof ok_s, "%lds ago", age);
  }

  // On ESP-IDF the high-water mark is in bytes (StackType_t is uint8_t): the
  // least free stack this task has ever had, out of 16384.
  Serial.printf("fetch: wifi %d time %d sched %s rt %lds rows %s last_ok %s heap %u stack %u%s\n",
                wifi ? 1 : 0, clock_ok ? 1 : 0, sched_s, static_cast<long>(interval), rows, ok_s,
                static_cast<unsigned>(ESP.getFreeHeap()),
                static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
                g_backoff > 0 ? " backoff" : "");
}

void task(void*) {
  for (;;) {
    g_pass = Pass{};

    const bool wifi = ensure_wifi();
    const bool clock_ok = wifi && ensure_time();
    if (!clock_ok) {
      // Nothing can be fetched and nothing is claimed: the last good snapshot
      // stays up and goes stale on its own, at the nominal rate. time() is
      // still the right clock here - it only stops being trustworthy before
      // the first sync, and then there are no rows to age anyway.
      const int64_t now = time(nullptr);
      g_work.poll_interval_s = expected_interval(now);
      publish();
      log_pass(wifi, false, now, NO_LINK_RETRY_S);
      vTaskDelay(pdMS_TO_TICKS(NO_LINK_RETRY_S * 1000));
      continue;
    }

    const int64_t now = time(nullptr);
    const LocalTime lt = nz_local(now);

    if (!g_rail_loaded) load_rail();

    for (int i = 0; i < g_work.n_watches; i++) {
      if (!g_res[i].resolved && now >= g_res[i].next_try) resolve(i, now);
    }
    for (int i = 0; i < g_work.n_watches; i++) {
      const Resolved& r = g_res[i];
      if (!r.resolved) continue;
      const bool rolled =
          r.sched_date.y != lt.y || r.sched_date.m != lt.m || r.sched_date.d != lt.d;
      if (now >= r.next_sched || rolled) refresh_schedule(i, now, lt);
    }
    refresh_realtime(now);

    if (g_pass.news) g_work.last_ok = now;

    // 30 s while something is close, 2 minutes otherwise; doubling to a
    // 5 minute cap on 429, 5xx or a transport error, with the last good data
    // left exactly where it was (spec 8).
    const int32_t base = nominal_interval(now);
    int32_t pace;
    if (g_pass.backoff) {
      const int32_t prev = g_backoff > 0 ? g_backoff : base;
      g_backoff = prev > BACKOFF_CAP_S / 2 ? BACKOFF_CAP_S : prev * 2;
      pace = g_backoff;
    } else {
      g_backoff = 0;
      pace = base;
    }

    // The backoff paces this task; it is never published. What is published is
    // when the board next expects news, so staleness grows from the last news
    // at the honest rate however far the backoff stretches.
    g_work.poll_interval_s = expected_interval(now);
    publish();
    log_states();
    log_pass(true, true, now, pace);  // the log shows the real pacing
    vTaskDelay(pdMS_TO_TICKS(static_cast<uint32_t>(pace) * 1000));
  }
}

}  // namespace

void fetcher_begin() {
  g_lock = xSemaphoreCreateMutex();
  if (g_lock == nullptr) {
    // Without the lock nothing can be published safely, so nothing is fetched.
    // fetcher_snapshot sees the null lock and hands back an empty snapshot.
    Serial.println("FATAL: fetcher mutex allocation failed, fetch task not started");
    return;
  }

  memset(&g_work, 0, sizeof g_work);
  g_work.n_watches = N_WATCHES;
  g_work.poll_interval_s = RT_SLOW_S;
  for (int i = 0; i < N_WATCHES; i++) {
    g_work.watches[i].state = WatchState::Starting;
    g_work.watches[i].kind = Kind::Bus;
    memset(&g_res[i], 0, sizeof g_res[i]);
    g_logged[i] = WatchState::Starting;
  }
  publish();  // the render loop has something honest to draw immediately

  // 16 KB: a TLS handshake needs far more stack than the default. Core 0, so
  // that nothing here can stall the render loop on core 1.
  xTaskCreatePinnedToCore(task, "fetch", 16384, nullptr, 1, nullptr, 0);
}

void fetcher_snapshot(Snapshot* out) {
  if (g_lock == nullptr) {
    memset(out, 0, sizeof *out);
    return;
  }
  xSemaphoreTake(g_lock, portMAX_DELAY);
  memcpy(out, &g_published, sizeof *out);
  xSemaphoreGive(g_lock);
}
