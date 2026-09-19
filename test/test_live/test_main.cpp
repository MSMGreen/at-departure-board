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
