#include <unity.h>

#include "demo.h"

void setUp() {}
void tearDown() {}

void test_all_scenes_present_in_python_order() {
  TEST_ASSERT_EQUAL_INT(9, demo_scene_count());
  TEST_ASSERT_EQUAL_STRING("single", demo_scene(0).name);
  TEST_ASSERT_EQUAL_STRING("arriving", demo_scene(4).name);
  TEST_ASSERT_EQUAL_STRING("dimmed", demo_scene(7).name);
  TEST_ASSERT_EQUAL_STRING("check_config", demo_scene(8).name);
}

void test_boot_shows_first_scene_in_first_theme() {
  const Board b = demo_board(0);
  TEST_ASSERT_EQUAL_UINT8(1, b.n_watches);
  TEST_ASSERT_EQUAL_STRING("20", b.watches[0].badge);
  TEST_ASSERT_EQUAL_STRING("to Wynyard Quarter", b.watches[0].headsign);
  TEST_ASSERT_EQUAL_INT32(240, b.watches[0].next()->eta_s);
  TEST_ASSERT_EQUAL_INT32(1020, b.watches[0].following()->eta_s);
  TEST_ASSERT_EQUAL_STRING("17:42", b.clock);
  TEST_ASSERT_EQUAL_UINT8(0, b.theme);
}

void test_etas_count_down_in_real_seconds() {
  TEST_ASSERT_EQUAL_INT32(235, demo_board(5000).watches[0].next()->eta_s);
  TEST_ASSERT_EQUAL_INT32(235, demo_board(5999).watches[0].next()->eta_s);
}

void test_departed_service_is_dropped_and_next_promoted() {
  // arriving, 16 s in: the bus due at 15 s has left.
  const Board b = demo_board(4 * DEMO_SCENE_MS + 16000);
  TEST_ASSERT_EQUAL_UINT8(1, b.watches[0].n_deps);
  TEST_ASSERT_EQUAL_INT32(1004, b.watches[0].next()->eta_s);
  TEST_ASSERT_EQUAL_INT32(404, b.watches[1].next()->eta_s);
}

void test_states_carry_through() {
  const Board stale = demo_board(3 * DEMO_SCENE_MS);
  TEST_ASSERT_TRUE(stale.is_stale());
  TEST_ASSERT_EQUAL_INT32(240, stale.stale_s);

  const Board cancelled = demo_board(5 * DEMO_SCENE_MS);
  TEST_ASSERT_TRUE(cancelled.watches[0].next()->cancelled);
  TEST_ASSERT_EQUAL_INT32(300, cancelled.watches[0].next()->eta_s);

  const Board empty = demo_board(6 * DEMO_SCENE_MS);
  TEST_ASSERT_EQUAL_UINT8(0, empty.watches[0].n_deps);
  TEST_ASSERT_NULL(empty.watches[1].next());
  TEST_ASSERT_EQUAL_STRING("01:12", empty.clock);

  TEST_ASSERT_TRUE(demo_board(7 * DEMO_SCENE_MS).dimmed);
}

void test_rail_route_colour_survives_the_round_trip() {
  const Board b = demo_board(1 * DEMO_SCENE_MS);  // two_up
  TEST_ASSERT_TRUE(b.watches[1].kind == Kind::Train);
  TEST_ASSERT_TRUE(b.watches[1].has_route_color);
  TEST_ASSERT_EQUAL_UINT8(151, b.watches[1].route_color.r);
  TEST_ASSERT_FALSE(b.watches[0].has_route_color);  // buses have none
}

void test_theme_advances_after_every_scene_has_played() {
  const uint32_t cycle = demo_scene_count() * DEMO_SCENE_MS;
  TEST_ASSERT_EQUAL_UINT8(0, demo_board(cycle - 1).theme);
  TEST_ASSERT_EQUAL_UINT8(1, demo_board(cycle).theme);
  TEST_ASSERT_EQUAL_UINT8(0, demo_board(2 * cycle).theme);
  TEST_ASSERT_EQUAL_UINT8(1, demo_board(cycle).n_watches);  // scene 0 again
}

void test_the_check_config_scene_carries_its_message_and_location() {
  const Board b = demo_board(8 * DEMO_SCENE_MS);
  TEST_ASSERT_EQUAL_STRING("check config", b.watches[0].message);
  TEST_ASSERT_EQUAL_STRING("", b.watches[1].message);
  TEST_ASSERT_EQUAL_STRING("Kingsland", b.location);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_all_scenes_present_in_python_order);
  RUN_TEST(test_boot_shows_first_scene_in_first_theme);
  RUN_TEST(test_etas_count_down_in_real_seconds);
  RUN_TEST(test_departed_service_is_dropped_and_next_promoted);
  RUN_TEST(test_states_carry_through);
  RUN_TEST(test_rail_route_colour_survives_the_round_trip);
  RUN_TEST(test_theme_advances_after_every_scene_has_played);
  RUN_TEST(test_the_check_config_scene_carries_its_message_and_location);
  return UNITY_END();
}
