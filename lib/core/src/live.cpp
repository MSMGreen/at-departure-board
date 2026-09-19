#include "live.h"

#include <string.h>

namespace {

// A service scheduled this long ago may still be ahead of us: delays of -427 s
// are real, and so are late ones.
constexpr int64_t KEEP_PAST_S = 1800;

// How long past its scheduled time a trip realtime has never mentioned is
// still asked about. It may be late rather than gone.
constexpr int64_t NEVER_HEARD_S = 900;

struct Candidate {
  int32_t eta_s;
  bool live;
  bool cancelled;
};

// Shared by select_rows and select_serving_rows: the two differ only in which
// rows they admit, never in the keep-recent window, the copy or the sort.
bool route_passes(const char* route_id, const char* const route_ids[], int n_routes) {
  if (n_routes <= 0) return true;  // an unpinned watch takes whatever runs
  for (int k = 0; k < n_routes; k++) {
    if (strcmp(route_id, route_ids[k]) == 0) return true;
  }
  return false;
}

void emit_row(const StopTripRow& r, int64_t sched, LiveRow& o) {
  memset(&o, 0, sizeof o);
  strncpy(o.trip_id, r.trip_id, sizeof o.trip_id - 1);
  strncpy(o.stop_id, r.stop_id, sizeof o.stop_id - 1);
  o.sched_epoch = sched;
}

void sort_rows_by_schedule(LiveRow out[], int count) {
  for (int i = 1; i < count; i++) {  // by scheduled time, stable
    const LiveRow v = out[i];
    int j = i - 1;
    while (j >= 0 && out[j].sched_epoch > v.sched_epoch) {
      out[j + 1] = out[j];
      j--;
    }
    out[j + 1] = v;
  }
}

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
    if (!route_passes(r.route_id, route_ids, n_routes)) continue;
    const int64_t sched = gtfs_epoch(r.service_date, r.departure_s);
    if (sched < now - KEEP_PAST_S) continue;
    emit_row(r, sched, out[count]);
    count++;
  }
  sort_rows_by_schedule(out, count);
  return count;
}

int collect_route_dirs(const StopTripRow rows[], int n, RouteDir out[], int cap) {
  int count = 0;
  for (int i = 0; i < n; i++) {
    const StopTripRow& r = rows[i];
    if (r.direction_id < 0 || r.direction_id > 1) continue;
    bool seen = false;
    for (int p = 0; p < count; p++) {
      if (out[p].direction_id == r.direction_id && strcmp(out[p].route_id, r.route_id) == 0) {
        seen = true;
        break;
      }
    }
    if (seen) continue;  // the pair already has its representative trip
    if (count >= cap) break;
    RouteDir& o = out[count];
    memset(&o, 0, sizeof o);
    strncpy(o.route_id, r.route_id, sizeof o.route_id - 1);
    o.direction_id = r.direction_id;
    strncpy(o.trip_id, r.trip_id, sizeof o.trip_id - 1);
    strncpy(o.stop_id, r.stop_id, sizeof o.stop_id - 1);
    o.serves = false;
    count++;
  }
  return count;
}

int select_serving_rows(const StopTripRow rows[], int n, const RouteDir pairs[], int n_pairs,
                        const char* const route_ids[], int n_routes, int64_t now, LiveRow out[],
                        int cap) {
  int count = 0;
  for (int i = 0; i < n && count < cap; i++) {
    const StopTripRow& r = rows[i];
    bool serving = false;
    for (int p = 0; p < n_pairs; p++) {
      if (!pairs[p].serves) continue;
      if (pairs[p].direction_id != r.direction_id) continue;
      if (strcmp(pairs[p].route_id, r.route_id) != 0) continue;
      serving = true;
      break;
    }
    if (!serving) continue;
    if (!route_passes(r.route_id, route_ids, n_routes)) continue;
    const int64_t sched = gtfs_epoch(r.service_date, r.departure_s);
    if (sched < now - KEEP_PAST_S) continue;
    emit_row(r, sched, out[count]);
    count++;
  }
  sort_rows_by_schedule(out, count);
  return count;
}

WatchState verdict_for_pairs(const RouteDir pairs[], int n_pairs) {
  for (int p = 0; p < n_pairs; p++) {
    if (pairs[p].serves) return WatchState::Ok;
  }
  // Nothing reaches the target. Only a route running BOTH ways past this stop,
  // neither way reaching it, proves the target is wrong for this stop; one
  // direction of one line simply means nothing is due your way right now.
  for (int a = 0; a < n_pairs; a++) {
    for (int b = a + 1; b < n_pairs; b++) {
      if (pairs[a].direction_id == pairs[b].direction_id) continue;
      if (strcmp(pairs[a].route_id, pairs[b].route_id) == 0) return WatchState::CheckConfig;
    }
  }
  return WatchState::Ok;
}

void apply_realtime(LiveWatch& w, const RtEntity ents[], int n,
                    const char* const requested[], int n_requested) {
  for (int i = 0; i < w.n_rows; i++) {
    LiveRow& row = w.rows[i];
    bool matched = false;
    for (int e = 0; e < n; e++) {
      const RtEntity& ent = ents[e];
      if (strcmp(ent.trip_id, row.trip_id) != 0) continue;  // re-filter: the
                                                            // tripid query is inexact
      matched = true;
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
    if (matched) continue;
    // We asked about this trip and the feed said nothing: an old delay must
    // never keep being shown as live.
    for (int q = 0; q < n_requested; q++) {
      if (strcmp(requested[q], row.trip_id) == 0) {
        row.has_rt = false;
        row.delay = 0;
        row.cancelled = false;
        break;
      }
    }
  }
}

void carry_realtime(const LiveRow old_rows[], int n_old, LiveRow new_rows[], int n_new) {
  for (int i = 0; i < n_new; i++) {
    LiveRow& row = new_rows[i];
    for (int k = 0; k < n_old; k++) {
      if (strcmp(old_rows[k].trip_id, row.trip_id) != 0) continue;
      row.has_rt = old_rows[k].has_rt;
      row.delay = old_rows[k].delay;
      row.cancelled = old_rows[k].cancelled;
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
      if (row.has_rt) {
        if (row.sched_epoch + row.delay < now - 60) continue;  // it has left
      } else if (row.sched_epoch < now - NEVER_HEARD_S) {
        continue;  // no word of it for 15 minutes past its time: gone
      }
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
