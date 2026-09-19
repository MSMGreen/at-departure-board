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
