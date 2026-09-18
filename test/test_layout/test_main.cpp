#include <unity.h>

#include "layout.h"

void setUp() {}
void tearDown() {}

static void assert_rect(Rect want, Rect got) {
  TEST_ASSERT_EQUAL_INT(want.x0, got.x0);
  TEST_ASSERT_EQUAL_INT(want.y0, got.y0);
  TEST_ASSERT_EQUAL_INT(want.x1, got.x1);
  TEST_ASSERT_EQUAL_INT(want.y1, got.y1);
}

void test_lane_rects_split_the_space_below_the_status_bar() {
  Rect r{};
  TEST_ASSERT_TRUE(lane_rect(0, 1, &r));
  assert_rect({0, 18, 320, 240}, r);
  TEST_ASSERT_TRUE(lane_rect(1, 2, &r));
  assert_rect({0, 129, 320, 240}, r);
  TEST_ASSERT_TRUE(lane_rect(3, 4, &r));
  assert_rect({0, 183, 320, 238}, r);  // 222 / 4 = 55, two rows spare
}

void test_lane_rect_rejects_out_of_range() {
  Rect r{};
  TEST_ASSERT_FALSE(lane_rect(0, 0, &r));
  TEST_ASSERT_FALSE(lane_rect(0, 5, &r));
  TEST_ASSERT_FALSE(lane_rect(2, 2, &r));
  TEST_ASSERT_FALSE(lane_rect(-1, 2, &r));
}

void test_lane_matches_python_two_up_second_lane() {
  const Lane l = lane(1, 2);
  assert_rect({0, 129, 320, 240}, l.rect);
  assert_rect({10, 135, 44, 153}, l.badge);
  TEST_ASSERT_EQUAL_INT(52, l.headsign_x);
  TEST_ASSERT_EQUAL_INT(137, l.headsign_y);
  TEST_ASSERT_EQUAL_INT(308, l.minutes_x);
  TEST_ASSERT_EQUAL_INT(135, l.minutes_y);
  TEST_ASSERT_EQUAL_INT(308, l.following_x);
  TEST_ASSERT_EQUAL_INT(157, l.following_y);
  assert_rect({10, 228, 262, 230}, l.track);
  TEST_ASSERT_EQUAL_INT(262, l.marker_x);
  TEST_ASSERT_EQUAL_INT(228, l.sprite_baseline);
}

void test_lane_matches_python_four_up_second_lane() {
  const Lane l = lane(1, 4);
  assert_rect({0, 73, 320, 128}, l.rect);
  assert_rect({10, 79, 44, 97}, l.badge);
  assert_rect({10, 116, 262, 118}, l.track);
  TEST_ASSERT_EQUAL_INT(116, l.sprite_baseline);
}

void test_size_class() {
  TEST_ASSERT_TRUE(size_class(1) == SizeClass::Large);
  TEST_ASSERT_TRUE(size_class(2) == SizeClass::Large);
  TEST_ASSERT_TRUE(size_class(3) == SizeClass::Compact);
  TEST_ASSERT_TRUE(size_class(4) == SizeClass::Compact);
}

void test_vehicle_x_matches_python() {
  const Lane l = lane(0, 2);
  TEST_ASSERT_EQUAL_INT(190, vehicle_x(-5, l, 72));    // departed: at the marker
  TEST_ASSERT_EQUAL_INT(190, vehicle_x(0, l, 72));
  TEST_ASSERT_EQUAL_INT(188, vehicle_x(15, l, 72));
  TEST_ASSERT_EQUAL_INT(154, vehicle_x(240, l, 72));
  TEST_ASSERT_EQUAL_INT(92, vehicle_x(600, l, 88));
  TEST_ASSERT_EQUAL_INT(10, vehicle_x(1199, l, 36));
  TEST_ASSERT_EQUAL_INT(10, vehicle_x(1200, l, 72));
  TEST_ASSERT_EQUAL_INT(10, vehicle_x(5000, l, 72));   // beyond the horizon: parked left
}

void test_vehicle_x_rounds_halves_to_even_like_python() {
  // Raw positions 188.5 and 185.5. Python's round() gives 188 and 186;
  // lround() would give 189 and 186 and put the bus a pixel off.
  const Lane l = lane(0, 2);
  TEST_ASSERT_EQUAL_INT(188, vehicle_x(10, l, 72));
  TEST_ASSERT_EQUAL_INT(186, vehicle_x(30, l, 72));
  TEST_ASSERT_EQUAL_INT(182, vehicle_x(50, l, 72));
}

void test_display_minutes_never_negative() {
  TEST_ASSERT_EQUAL_INT(0, display_minutes(-5));
  TEST_ASSERT_EQUAL_INT(0, display_minutes(59));
  TEST_ASSERT_EQUAL_INT(1, display_minutes(60));
  TEST_ASSERT_EQUAL_INT(4, display_minutes(240));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_lane_rects_split_the_space_below_the_status_bar);
  RUN_TEST(test_lane_rect_rejects_out_of_range);
  RUN_TEST(test_lane_matches_python_two_up_second_lane);
  RUN_TEST(test_lane_matches_python_four_up_second_lane);
  RUN_TEST(test_size_class);
  RUN_TEST(test_vehicle_x_matches_python);
  RUN_TEST(test_vehicle_x_rounds_halves_to_even_like_python);
  RUN_TEST(test_display_minutes_never_negative);
  return UNITY_END();
}
