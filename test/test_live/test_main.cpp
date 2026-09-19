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

void test_route_dirs_are_collected_per_route_and_direction() {
  // Kingsland in one window: two lines, both directions of each.
  const StopTripRow rows[] = {
      row("ew0", "E-W-201", 0, 15 * 3600),
      row("ow0", "O-W-201", 0, 15 * 3600 + 60),
      row("ew1", "E-W-201", 1, 15 * 3600 + 120),
      row("ow1", "O-W-201", 1, 15 * 3600 + 180),
      row("ew0b", "E-W-201", 0, 15 * 3600 + 240),  // same pair again
  };
  RouteDir pairs[MAX_ROUTE_DIRS];
  TEST_ASSERT_EQUAL_INT(4, collect_route_dirs(rows, 5, pairs, MAX_ROUTE_DIRS));
  TEST_ASSERT_EQUAL_STRING("E-W-201", pairs[0].route_id);
  TEST_ASSERT_EQUAL_INT(0, pairs[0].direction_id);
  TEST_ASSERT_EQUAL_STRING("ew0", pairs[0].trip_id);  // first trip of that pair
  TEST_ASSERT_EQUAL_STRING("O-W-201", pairs[1].route_id);
  TEST_ASSERT_EQUAL_STRING("ew1", pairs[2].trip_id);
}

void test_only_the_serving_line_is_shown() {
  // The bug: O-W does not reach Waitemata, E-W does. Rows of the line that
  // does not go your way must never appear.
  const StopTripRow rows[] = {
      row("ow1", "O-W-201", 1, 15 * 3600 + 60),
      row("ew1", "E-W-201", 1, 15 * 3600 + 120),
  };
  RouteDir pairs[MAX_ROUTE_DIRS];
  const int n = collect_route_dirs(rows, 2, pairs, MAX_ROUTE_DIRS);
  for (int i = 0; i < n; i++) pairs[i].serves = strcmp(pairs[i].route_id, "E-W-201") == 0;

  LiveRow out[8];
  TEST_ASSERT_EQUAL_INT(1, select_serving_rows(rows, 2, pairs, n, nullptr, 0, T_15_00, out, 8));
  TEST_ASSERT_EQUAL_STRING("ew1", out[0].trip_id);
}

void test_verdict_is_check_config_only_when_a_route_runs_both_ways_in_vain() {
  RouteDir pairs[MAX_ROUTE_DIRS]{};
  // One line, both directions, neither reaches the target: the target is wrong
  // for this stop.
  strncpy(pairs[0].route_id, "O-W-201", sizeof pairs[0].route_id - 1);
  pairs[0].direction_id = 0;
  strncpy(pairs[1].route_id, "O-W-201", sizeof pairs[1].route_id - 1);
  pairs[1].direction_id = 1;
  TEST_ASSERT_TRUE(verdict_for_pairs(pairs, 2) == WatchState::CheckConfig);

  // One line, one direction, not reaching it: nothing is due your way, which is
  // an empty board rather than a misconfiguration.
  TEST_ASSERT_TRUE(verdict_for_pairs(pairs, 1) == WatchState::Ok);

  // Anything serving at all is Ok.
  pairs[1].serves = true;
  TEST_ASSERT_TRUE(verdict_for_pairs(pairs, 2) == WatchState::Ok);
}

void test_no_pairs_at_all_is_an_empty_board() {
  RouteDir pairs[MAX_ROUTE_DIRS]{};
  TEST_ASSERT_TRUE(verdict_for_pairs(pairs, 0) == WatchState::Ok);
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

  const char* requested[] = {"t1", "t2"};
  apply_realtime(w, ents, 3, requested, 2);
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
  const char* requested[] = {"t1"};
  apply_realtime(w, &e, 1, requested, 1);
  TEST_ASSERT_TRUE(w.rows[0].cancelled);
}

void test_a_requested_trip_the_feed_forgets_stops_being_live() {
  LiveWatch w{};
  w.n_rows = 2;
  strncpy(w.rows[0].trip_id, "asked", sizeof w.rows[0].trip_id - 1);
  w.rows[0].has_rt = true;
  w.rows[0].delay = 240;
  w.rows[0].cancelled = true;
  strncpy(w.rows[1].trip_id, "not-asked", sizeof w.rows[1].trip_id - 1);
  w.rows[1].has_rt = true;
  w.rows[1].delay = 60;

  // The feed answered, but said nothing about "asked".
  RtEntity ents[1]{};
  strncpy(ents[0].trip_id, "someone-else", sizeof ents[0].trip_id - 1);
  ents[0].has_delay = true;
  ents[0].delay = 12;
  const char* requested[] = {"asked"};

  apply_realtime(w, ents, 1, requested, 1);

  TEST_ASSERT_FALSE(w.rows[0].has_rt);      // no longer claimed as live
  TEST_ASSERT_EQUAL_INT32(0, w.rows[0].delay);
  TEST_ASSERT_FALSE(w.rows[0].cancelled);
  TEST_ASSERT_TRUE(w.rows[1].has_rt);       // never asked about: untouched
  TEST_ASSERT_EQUAL_INT32(60, w.rows[1].delay);
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

void test_a_stale_board_dims_its_lanes() {
  // Spec 8: keep the last good data, show "stale 4m", dim the lanes.
  const WatchConfig cfg[] = {{"to Wynyard Quarter", "8213", "20", "1060"}};
  Snapshot s{};
  s.n_watches = 1;
  s.watches[0].state = WatchState::Ok;
  s.poll_interval_s = 30;

  s.last_ok = T_15_00 - 30;
  TEST_ASSERT_FALSE(build_board(s, cfg, "K", T_15_00, 0).dimmed);

  s.last_ok = T_15_00 - 270;
  const Board b = build_board(s, cfg, "K", T_15_00, 0);
  TEST_ASSERT_TRUE(b.is_stale());
  TEST_ASSERT_TRUE(b.dimmed);
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

void test_a_late_bus_survives_a_schedule_refresh() {
  // C1: scheduled 15:00, realtime says +300. At 15:02 the 15-minute schedule
  // refresh rebuilds the rows. The bus is still 3 minutes out and must stay.
  const StopTripRow rows[] = {row("late", "20-202", 0, 15 * 3600),
                              row("next", "20-202", 0, 15 * 3600 + 1200)};
  RouteDir pairs[MAX_ROUTE_DIRS];
  const int n_pairs = collect_route_dirs(rows, 2, pairs, MAX_ROUTE_DIRS);
  pairs[0].serves = true;

  Snapshot s{};
  s.n_watches = 1;
  LiveWatch& w = s.watches[0];
  w.state = WatchState::Ok;
  w.n_rows = static_cast<uint8_t>(
      select_serving_rows(rows, 2, pairs, n_pairs, nullptr, 0, T_15_00 - 600, w.rows, MAX_ROWS));
  TEST_ASSERT_EQUAL_UINT8(2, w.n_rows);

  RtEntity e{};
  strncpy(e.trip_id, "late", sizeof e.trip_id - 1);
  e.has_delay = true;
  e.delay = 300;
  const char* asked[] = {"late", "next"};
  apply_realtime(w, &e, 1, asked, 2);
  TEST_ASSERT_TRUE(w.rows[0].has_rt);

  // The refresh at sched + 120.
  const int64_t now = T_15_00 + 120;
  LiveRow fresh[MAX_ROWS];
  const int n = select_serving_rows(rows, 2, pairs, n_pairs, nullptr, 0, now, fresh, MAX_ROWS);
  carry_realtime(w.rows, w.n_rows, fresh, n);
  memcpy(w.rows, fresh, sizeof fresh);
  w.n_rows = static_cast<uint8_t>(n);

  TEST_ASSERT_TRUE(w.rows[0].has_rt);
  TEST_ASSERT_EQUAL_INT32(300, w.rows[0].delay);
  TEST_ASSERT_FALSE(w.rows[1].has_rt);  // nothing was said about it

  const char* ids[8];
  const int n_ids = realtime_ids(s, now, ids, 8, 6);
  TEST_ASSERT_EQUAL_INT(2, n_ids);
  TEST_ASSERT_EQUAL_STRING("late", ids[0]);

  s.last_ok = now;
  s.poll_interval_s = 30;
  const WatchConfig cfg[] = {{"to Wynyard Quarter", "8213", "20", "1060"}};
  const Board b = build_board(s, cfg, "K", now, 0);
  TEST_ASSERT_EQUAL_UINT8(2, b.watches[0].n_deps);
  TEST_ASSERT_EQUAL_INT32(180, b.watches[0].next()->eta_s);
  TEST_ASSERT_TRUE(b.watches[0].next()->live);
}

void test_carry_realtime_matches_by_trip_id_not_position() {
  LiveRow old_rows[2]{};
  strncpy(old_rows[0].trip_id, "a", sizeof old_rows[0].trip_id - 1);
  strncpy(old_rows[1].trip_id, "b", sizeof old_rows[1].trip_id - 1);
  old_rows[1].has_rt = true;
  old_rows[1].delay = -45;
  old_rows[1].cancelled = true;

  LiveRow new_rows[2]{};
  strncpy(new_rows[0].trip_id, "b", sizeof new_rows[0].trip_id - 1);
  strncpy(new_rows[1].trip_id, "c", sizeof new_rows[1].trip_id - 1);
  carry_realtime(old_rows, 2, new_rows, 2);

  TEST_ASSERT_TRUE(new_rows[0].has_rt);
  TEST_ASSERT_EQUAL_INT32(-45, new_rows[0].delay);
  TEST_ASSERT_TRUE(new_rows[0].cancelled);
  TEST_ASSERT_FALSE(new_rows[1].has_rt);  // a new trip starts with no realtime
  TEST_ASSERT_EQUAL_INT32(0, new_rows[1].delay);
  TEST_ASSERT_FALSE(new_rows[1].cancelled);
}

void test_a_never_heard_of_row_five_minutes_past_is_still_asked_about() {
  Snapshot s{};
  s.n_watches = 1;
  s.watches[0].state = WatchState::Ok;
  s.watches[0].n_rows = 1;
  strncpy(s.watches[0].rows[0].trip_id, "maybe-late", sizeof s.watches[0].rows[0].trip_id - 1);
  s.watches[0].rows[0].sched_epoch = T_15_00 - 300;
  const char* ids[4];
  TEST_ASSERT_EQUAL_INT(1, realtime_ids(s, T_15_00, ids, 4, 6));
  TEST_ASSERT_EQUAL_STRING("maybe-late", ids[0]);
}

void test_a_never_heard_of_row_twenty_minutes_past_is_not() {
  Snapshot s{};
  s.n_watches = 1;
  s.watches[0].state = WatchState::Ok;
  s.watches[0].n_rows = 1;
  strncpy(s.watches[0].rows[0].trip_id, "gone", sizeof s.watches[0].rows[0].trip_id - 1);
  s.watches[0].rows[0].sched_epoch = T_15_00 - 1200;
  const char* ids[4];
  TEST_ASSERT_EQUAL_INT(0, realtime_ids(s, T_15_00, ids, 4, 6));
}

void test_a_heard_of_row_that_has_left_is_not_asked_about() {
  Snapshot s{};
  s.n_watches = 1;
  s.watches[0].state = WatchState::Ok;
  s.watches[0].n_rows = 1;
  strncpy(s.watches[0].rows[0].trip_id, "left", sizeof s.watches[0].rows[0].trip_id - 1);
  s.watches[0].rows[0].sched_epoch = T_15_00 - 300;
  s.watches[0].rows[0].has_rt = true;
  s.watches[0].rows[0].delay = 120;  // left three minutes ago
  const char* ids[4];
  TEST_ASSERT_EQUAL_INT(0, realtime_ids(s, T_15_00, ids, 4, 6));
}

namespace {
LocalTime at(int y, int m, int d, int hour) {
  LocalTime t{};
  t.y = y;
  t.m = m;
  t.d = d;
  t.hour = hour;
  return t;
}

void expect_window(const FetchWindow& w, int y, int m, int d, int start, int range) {
  TEST_ASSERT_EQUAL_INT(y, w.date.y);
  TEST_ASSERT_EQUAL_INT(m, w.date.m);
  TEST_ASSERT_EQUAL_INT(d, w.date.d);
  TEST_ASSERT_EQUAL_INT(start, w.start_hour);
  TEST_ASSERT_EQUAL_INT(range, w.hour_range);
}
}  // namespace

void test_schedule_windows_cover_three_hours_from_now() {
  FetchWindow w[2];
  TEST_ASSERT_EQUAL_INT(1, schedule_windows(at(2026, 9, 19, 17), w));
  expect_window(w[0], 2026, 9, 19, 17, 3);
}

void test_schedule_windows_run_past_midnight_on_the_same_service_date() {
  // The API does not clamp at midnight: 23:00 for three hours returns 23:04
  // through 25:25 against one date (verified live 2026-09-19).
  FetchWindow w[2];
  TEST_ASSERT_EQUAL_INT(1, schedule_windows(at(2026, 9, 19, 23), w));
  expect_window(w[0], 2026, 9, 19, 23, 3);
}

void test_schedule_windows_at_midnight_ask_yesterday_for_its_late_services() {
  // 00:xx on the 20th: the trains still running are yesterday's 24:xx, and
  // today's own day is asked from hour 1 because the API rejects 0.
  FetchWindow w[2];
  TEST_ASSERT_EQUAL_INT(2, schedule_windows(at(2026, 9, 20, 0), w));
  expect_window(w[0], 2026, 9, 19, 24, 3);
  expect_window(w[1], 2026, 9, 20, 1, 3);
}

void test_schedule_windows_in_the_small_hours() {
  FetchWindow w[2];
  TEST_ASSERT_EQUAL_INT(2, schedule_windows(at(2026, 9, 20, 2), w));
  expect_window(w[0], 2026, 9, 19, 26, 3);
  expect_window(w[1], 2026, 9, 20, 2, 3);

  // From 04:00 one window is enough again.
  TEST_ASSERT_EQUAL_INT(1, schedule_windows(at(2026, 9, 20, 4), w));
  expect_window(w[0], 2026, 9, 20, 4, 3);
}

void test_schedule_windows_roll_back_over_a_month_end() {
  FetchWindow w[2];
  TEST_ASSERT_EQUAL_INT(2, schedule_windows(at(2026, 10, 1, 0), w));
  expect_window(w[0], 2026, 9, 30, 24, 3);
  expect_window(w[1], 2026, 10, 1, 1, 3);

  TEST_ASSERT_EQUAL_INT(2, schedule_windows(at(2027, 1, 1, 1), w));  // and a year end
  expect_window(w[0], 2026, 12, 31, 25, 3);
  expect_window(w[1], 2027, 1, 1, 1, 3);
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
  RUN_TEST(test_route_dirs_are_collected_per_route_and_direction);
  RUN_TEST(test_only_the_serving_line_is_shown);
  RUN_TEST(test_verdict_is_check_config_only_when_a_route_runs_both_ways_in_vain);
  RUN_TEST(test_no_pairs_at_all_is_an_empty_board);
  RUN_TEST(test_choose_direction);
  RUN_TEST(test_select_rows_filters_by_direction_and_route);
  RUN_TEST(test_select_rows_keeps_recent_departures_because_delays_can_be_large);
  RUN_TEST(test_apply_realtime_prefers_our_own_stop_time_update);
  RUN_TEST(test_apply_realtime_marks_cancellations);
  RUN_TEST(test_a_requested_trip_the_feed_forgets_stops_being_live);
  RUN_TEST(test_build_board_turns_a_snapshot_into_what_the_screen_draws);
  RUN_TEST(test_build_board_drops_departures_that_have_left);
  RUN_TEST(test_build_board_orders_by_the_time_the_service_will_actually_leave);
  RUN_TEST(test_staleness_is_measured_against_the_polling_interval);
  RUN_TEST(test_a_stale_board_dims_its_lanes);
  RUN_TEST(test_realtime_ids_asks_only_about_services_still_to_come);
  RUN_TEST(test_a_late_bus_survives_a_schedule_refresh);
  RUN_TEST(test_carry_realtime_matches_by_trip_id_not_position);
  RUN_TEST(test_a_never_heard_of_row_five_minutes_past_is_still_asked_about);
  RUN_TEST(test_a_never_heard_of_row_twenty_minutes_past_is_not);
  RUN_TEST(test_a_heard_of_row_that_has_left_is_not_asked_about);
  RUN_TEST(test_schedule_windows_cover_three_hours_from_now);
  RUN_TEST(test_schedule_windows_run_past_midnight_on_the_same_service_date);
  RUN_TEST(test_schedule_windows_at_midnight_ask_yesterday_for_its_late_services);
  RUN_TEST(test_schedule_windows_in_the_small_hours);
  RUN_TEST(test_schedule_windows_roll_back_over_a_month_end);
  RUN_TEST(test_state_messages);
  return UNITY_END();
}
