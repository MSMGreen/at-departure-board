#include <unity.h>

#include "freshness.h"
#include "live.h"

void setUp() {}
void tearDown() {}

namespace {
constexpr int64_t T = 1789268400;  // 2026-09-13 15:00:00 NZST

// What the screen would show, from what the fetcher would publish.
int32_t stale_at(int64_t last_ok, int32_t interval, int64_t now) {
  Snapshot s{};
  s.last_ok = last_ok;
  s.poll_interval_s = interval;
  return build_board(s, nullptr, "K", now, 0).stale_s;
}
}  // namespace

void test_the_normal_cadence() {
  Freshness f{};
  freshness_news(f, T);
  TEST_ASSERT_EQUAL_INT32(RT_FAST_S, freshness_interval(f, true, true));
  TEST_ASSERT_EQUAL_INT32(RT_SLOW_S, freshness_interval(f, true, false));
}

void test_a_healthy_quiet_night_publishes_the_schedule_period_and_stays_fresh() {
  Freshness f{};
  freshness_news(f, T);  // the schedule refresh: nothing more tonight
  for (int64_t now = T; now < T + SCHEDULE_PERIOD_S; now += RT_SLOW_S) {
    const int32_t interval = freshness_interval(f, false, false);
    TEST_ASSERT_EQUAL_INT32(SCHEDULE_PERIOD_S, interval);
    TEST_ASSERT_EQUAL_INT32(0, stale_at(f.last_news, interval, now));
  }
}

void test_an_outage_across_the_last_departure_never_unstales() {
  // The last departure is 10 minutes out when the internet goes. Realtime
  // fails every pass until the departure has gone; after that nothing is
  // upcoming, so the fetcher makes no request at all - and so records no new
  // failure. None of that is news, and the board must only get staler.
  Freshness f{};
  freshness_news(f, T);
  int32_t interval = freshness_interval(f, true, true);
  int32_t prev = stale_at(f.last_news, interval, T);
  bool was_stale = false;
  for (int64_t now = T + RT_FAST_S; now < T + 3600; now += RT_FAST_S) {
    const bool upcoming = now < T + 600;
    if (upcoming) freshness_failure(f);  // a request was made and failed
    interval = freshness_interval(f, upcoming, upcoming);
    const int32_t stale = stale_at(f.last_news, interval, now);
    TEST_ASSERT_TRUE_MESSAGE(stale >= prev, "stale_s went down without news");
    if (was_stale) TEST_ASSERT_TRUE_MESSAGE(stale > STALE_AFTER_S, "stale disappeared");
    was_stale = stale > STALE_AFTER_S;
    prev = stale;
  }
  TEST_ASSERT_TRUE(was_stale);
}

void test_no_link_counts_as_a_failure_even_when_nothing_is_upcoming() {
  Freshness f{};
  freshness_news(f, T);
  TEST_ASSERT_EQUAL_INT32(RT_SLOW_S, freshness_interval(f, true, false));
  freshness_failure(f);  // WiFi gone
  TEST_ASSERT_EQUAL_INT32(RT_SLOW_S, freshness_interval(f, false, false));
}

void test_after_a_failure_the_interval_does_not_even_grow_from_fast_to_slow() {
  Freshness f{};
  freshness_news(f, T);
  TEST_ASSERT_EQUAL_INT32(RT_FAST_S, freshness_interval(f, true, true));
  freshness_failure(f);
  TEST_ASSERT_EQUAL_INT32(RT_FAST_S, freshness_interval(f, true, false));
  TEST_ASSERT_EQUAL_INT32(RT_FAST_S, freshness_interval(f, false, false));
}

void test_news_clears_the_failure() {
  Freshness f{};
  freshness_news(f, T);
  freshness_failure(f);
  TEST_ASSERT_TRUE(f.failed_since_news);
  TEST_ASSERT_EQUAL_INT32(RT_SLOW_S, freshness_interval(f, false, false));

  freshness_news(f, T + 300);
  TEST_ASSERT_FALSE(f.failed_since_news);
  TEST_ASSERT_EQUAL_INT64(T + 300, f.last_news);
  TEST_ASSERT_EQUAL_INT32(SCHEDULE_PERIOD_S, freshness_interval(f, false, false));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_normal_cadence);
  RUN_TEST(test_a_healthy_quiet_night_publishes_the_schedule_period_and_stays_fresh);
  RUN_TEST(test_an_outage_across_the_last_departure_never_unstales);
  RUN_TEST(test_no_link_counts_as_a_failure_even_when_nothing_is_upcoming);
  RUN_TEST(test_after_a_failure_the_interval_does_not_even_grow_from_fast_to_slow);
  RUN_TEST(test_news_clears_the_failure);
  return UNITY_END();
}
