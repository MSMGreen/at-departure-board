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
  int64_t last_ok;         // see board_last_ok (freshness.h); 0 if never
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

constexpr int MAX_ROUTE_DIRS = 6;  // a stop served by more routes than this
                                   // keeps the first six pairs it sees

// One (route_id, direction_id) combination seen in the window, with a trip to
// ask about and whether that trip reaches the target.
struct RouteDir {
  char route_id[ROUTE_ID_LEN];
  int8_t direction_id;
  char trip_id[TRIP_ID_LEN];  // representative trip, for trips/{id}/stops
  char stop_id[STOP_ID_LEN];  // our stop as that trip calls it (the platform)
  bool serves;                // filled in by the caller after fetching stops
};

// Distinct (route_id, direction_id) pairs in the rows, in first-seen order,
// each with the first trip seen for that pair. Direction is a property of the
// pair, not of a trip: at a station served by two lines, one line can reach
// the target and the other not (spec 3a).
int collect_route_dirs(const StopTripRow rows[], int n, RouteDir out[], int cap);

// Rows belonging to a pair whose `serves` is true, filtered by route as before,
// scheduled recently enough to still matter, sorted by scheduled time.
int select_serving_rows(const StopTripRow rows[], int n, const RouteDir pairs[],
                        int n_pairs, const char* const route_ids[], int n_routes,
                        int64_t now, LiveRow out[], int cap);

// Ok when at least one pair serves; CheckConfig when none does AND some route
// had both directions present; otherwise Ok with nothing to show.
WatchState verdict_for_pairs(const RouteDir pairs[], int n_pairs);

// Applies delays and cancellations to rows whose trip_id we asked for; entities
// for any other trip are discarded (the tripid filter is inexact). A row we DID
// ask about and heard nothing back for has its realtime cleared: an old delay
// must never keep being shown as live.
void apply_realtime(LiveWatch& w, const RtEntity ents[], int n,
                    const char* const requested[], int n_requested);

// A schedule refresh rebuilds every row from scratch, which would forget what
// realtime already said. For each new row, copy has_rt, delay and cancelled
// from the old row with the same trip_id, if there is one. Without this a bus
// running late is dropped as "departed" at every 15-minute refresh.
void carry_realtime(const LiveRow old_rows[], int n_old, LiveRow new_rows[], int n_new);

// Trip ids worth asking realtime about: those still to come, at most
// per_watch from each watch. A row realtime has spoken about is still to come
// until sched + delay is a minute gone; a row it never has may be late rather
// than gone, so it is asked about for 15 minutes past its scheduled time.
int realtime_ids(const Snapshot& s, int64_t now, const char* out[], int cap, int per_watch);

struct FetchWindow {
  CivilDate date;
  int start_hour;  // 1..26: never 0, which the API rejects with 400
  int hour_range;
};

// The stoptrips request(s) covering the next three hours. A service date runs
// past midnight (its late trips are 24:xx, 25:xx) and the API does not clamp
// hour_range at 24, so from 04:00 one request covers it. In the small hours the
// trains still running belong to yesterday's service date, so a second request
// asks yesterday from 24 + hour; today's own is asked from max(hour, 1).
// Returns 1 or 2.
int schedule_windows(const LocalTime& now, FetchWindow out[2]);

Board build_board(const Snapshot& s, const WatchConfig cfg[], const char* location,
                  int64_t now, uint8_t theme);
