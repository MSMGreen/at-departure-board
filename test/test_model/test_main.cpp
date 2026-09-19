#include <string.h>
#include <unity.h>

#include "model.h"

void setUp() {}
void tearDown() {}

void test_next_and_following() {
  Watch w;
  watch_init(&w, "20", "to Wynyard Quarter", Kind::Bus, nullptr);
  TEST_ASSERT_NULL(w.next());
  TEST_ASSERT_NULL(w.following());
  TEST_ASSERT_TRUE(w.add({240, true, false}));
  TEST_ASSERT_EQUAL_INT32(240, w.next()->eta_s);
  TEST_ASSERT_NULL(w.following());
  TEST_ASSERT_TRUE(w.add({1020, false, false}));
  TEST_ASSERT_EQUAL_INT32(1020, w.following()->eta_s);
}

void test_add_refuses_when_full() {
  Watch w;
  watch_init(&w, "20", "x", Kind::Bus, nullptr);
  for (int i = 0; i < MAX_DEPARTURES; i++) TEST_ASSERT_TRUE(w.add({i * 60, false, false}));
  TEST_ASSERT_FALSE(w.add({999, false, false}));
  TEST_ASSERT_EQUAL_UINT8(MAX_DEPARTURES, w.n_deps);
}

void test_sort_is_by_eta_and_stable() {
  Watch w;
  watch_init(&w, "20", "x", Kind::Bus, nullptr);
  w.add({600, false, false});
  w.add({120, false, true});   // equal eta: first added stays first
  w.add({120, true, false});
  w.sort_departures();
  TEST_ASSERT_EQUAL_INT32(120, w.deps[0].eta_s);
  TEST_ASSERT_TRUE(w.deps[0].cancelled);
  TEST_ASSERT_TRUE(w.deps[1].live);
  TEST_ASSERT_EQUAL_INT32(600, w.deps[2].eta_s);
}

void test_watch_init_parses_route_colour_and_rejects_black() {
  Watch w;
  watch_init(&w, "E-W", "to Britomart", Kind::Train, "97C93D");
  TEST_ASSERT_TRUE(w.has_route_color);
  TEST_ASSERT_EQUAL_UINT8(151, w.route_color.r);
  watch_init(&w, "HUIA", "x", Kind::Bus, "000000");
  TEST_ASSERT_FALSE(w.has_route_color);
}

void test_watch_init_truncates_long_text_safely() {
  Watch w;
  watch_init(&w, "123456789012", "a headsign far longer than the forty bytes we keep", Kind::Bus, nullptr);
  TEST_ASSERT_EQUAL_size_t(sizeof w.badge - 1, strlen(w.badge));
  TEST_ASSERT_EQUAL_size_t(sizeof w.headsign - 1, strlen(w.headsign));
  TEST_ASSERT_EQUAL_UINT8(0, w.n_deps);
}

void test_stale_only_after_ninety_seconds() {
  Board b;
  memset(&b, 0, sizeof b);
  b.stale_s = 90;
  TEST_ASSERT_FALSE(b.is_stale());
  b.stale_s = 91;
  TEST_ASSERT_TRUE(b.is_stale());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_next_and_following);
  RUN_TEST(test_add_refuses_when_full);
  RUN_TEST(test_sort_is_by_eta_and_stable);
  RUN_TEST(test_watch_init_parses_route_colour_and_rejects_black);
  RUN_TEST(test_watch_init_truncates_long_text_safely);
  RUN_TEST(test_stale_only_after_ninety_seconds);
  return UNITY_END();
}
