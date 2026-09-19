#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <string>

#include "at_api.h"
#include "nztime.h"

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

void test_start_hour_accepts_after_midnight_hours_but_not_zero() {
  // The API answers 400 for start_hour=0 (verified live 2026-09-19): a
  // required-tag zero-value check. It accepts 24 and above, which is how a
  // service day's after-midnight trips are asked for (24:04 is 00:04 the next
  // morning, filed under the previous service date).
  char u[256];
  TEST_ASSERT_FALSE(url_stoptrips(u, sizeof u, "122-34ecc043", {2026, 9, 19}, 0, 3));
  TEST_ASSERT_TRUE(url_stoptrips(u, sizeof u, "122-34ecc043", {2026, 9, 19}, 24, 2));
  TEST_ASSERT_EQUAL_STRING(
      "https://api.at.govt.nz/gtfs/v3/stops/122-34ecc043/stoptrips"
      "?filter%5Bdate%5D=2026-09-19&filter%5Bstart_hour%5D=24&filter%5Bhour_range%5D=2", u);
  TEST_ASSERT_TRUE(url_stoptrips(u, sizeof u, "122-34ecc043", {2026, 9, 19}, 47, 3));
  TEST_ASSERT_FALSE(url_stoptrips(u, sizeof u, "122-34ecc043", {2026, 9, 19}, 48, 3));
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

void test_parse_stoptrips_after_midnight() {
  // Kingsland, captured live on the night of Saturday 2026-09-19 with
  // date=2026-09-19&start_hour=24&hour_range=2. After-midnight trains are
  // filed under the previous service date with hours of 24 and more.
  JsonDocument filter, doc;
  stoptrips_filter(filter);
  load("test/fixtures/post-crl/stoptrips_kingsland_after_midnight.json", doc, filter);
  StopTripRow rows[48];
  const int n = parse_stoptrips(doc, rows, 48);
  TEST_ASSERT_EQUAL_INT(11, n);
  TEST_ASSERT_EQUAL_INT32(24 * 3600 + 4 * 60, rows[0].departure_s);  // "24:04:00"
  TEST_ASSERT_EQUAL_INT(2026, rows[0].service_date.y);
  TEST_ASSERT_EQUAL_INT(9, rows[0].service_date.m);
  TEST_ASSERT_EQUAL_INT(19, rows[0].service_date.d);
  // 2026-09-20 00:04 NZST, checked with Python zoneinfo (Pacific/Auckland).
  TEST_ASSERT_EQUAL_INT64(1789819440, gtfs_epoch(rows[0].service_date, rows[0].departure_s));
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
  RUN_TEST(test_start_hour_accepts_after_midnight_hours_but_not_zero);
  RUN_TEST(test_parse_stop);
  RUN_TEST(test_an_unknown_stop_code_returns_an_empty_list_not_an_error);
  RUN_TEST(test_parse_routes);
  RUN_TEST(test_parse_stoptrips);
  RUN_TEST(test_parse_stoptrips_after_midnight);
  RUN_TEST(test_parse_stoptrips_respects_the_cap);
  RUN_TEST(test_parse_trip_stops_keeps_sequence_order);
  RUN_TEST(test_parse_realtime);
  return UNITY_END();
}
