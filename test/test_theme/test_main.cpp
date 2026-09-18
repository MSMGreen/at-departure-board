#include <string.h>
#include <unity.h>

#include "theme.h"

void setUp() {}
void tearDown() {}

static void assert_rgb(Rgb want, Rgb got) {
  TEST_ASSERT_EQUAL_UINT8(want.r, got.r);
  TEST_ASSERT_EQUAL_UINT8(want.g, got.g);
  TEST_ASSERT_EQUAL_UINT8(want.b, got.b);
}

void test_both_themes_in_sprite_header_order() {
  TEST_ASSERT_EQUAL_UINT8(2, theme_count());
  TEST_ASSERT_EQUAL_STRING("transit", theme(0).name);
  TEST_ASSERT_EQUAL_STRING("ghibli", theme(1).name);
  TEST_ASSERT_EQUAL_STRING("transit", theme(99).name);  // out of range -> default
}

void test_generated_colours_match_python() {
  assert_rgb({12, 16, 24}, theme(0).colours[C_BG]);
  assert_rgb({32, 42, 60}, theme(0).colours[C_PANEL_HI]);
  assert_rgb({20, 24, 30}, theme(0).colours[C_DARK]);
  assert_rgb({14, 12, 34}, theme(1).colours[C_BG]);
  TEST_ASSERT_TRUE(theme(0).scenery == SCENERY_TRANSIT);
  TEST_ASSERT_TRUE(theme(1).scenery == SCENERY_GHIBLI);
}

void test_transit_honours_route_colour_and_falls_back_by_kind() {
  Watch bus, train, huia;
  watch_init(&bus, "20", "x", Kind::Bus, nullptr);
  watch_init(&train, "E-W", "x", Kind::Train, "97C93D");
  watch_init(&huia, "HUIA", "x", Kind::Bus, "000000");
  assert_rgb({0, 168, 224}, badge_colour(theme(0), bus));
  assert_rgb({151, 201, 61}, badge_colour(theme(0), train));
  assert_rgb({0, 168, 224}, badge_colour(theme(0), huia));
}

void test_ghibli_ignores_route_colour() {
  Watch train;
  watch_init(&train, "E-W", "x", Kind::Train, "97C93D");
  assert_rgb({150, 178, 226}, badge_colour(theme(1), train));
}

void test_role_colours_match_python() {
  Rgb out[ROLE_SLOTS];
  role_colours(theme(0), {0, 168, 224}, out);
  assert_rgb({0, 168, 224}, out[1]);     // B body
  assert_rgb({40, 255, 255}, out[2]);    // H bright
  assert_rgb({0, 92, 123}, out[3]);      // S shade
  assert_rgb({0, 92, 123}, out[4]);      // M shade
  assert_rgb({214, 242, 255}, out[5]);   // W fixed
  assert_rgb({255, 255, 255}, out[6]);   // G fixed
  assert_rgb({10, 20, 30}, out[7]);      // D fixed
  assert_rgb({16, 26, 36}, out[8]);      // K fixed
  assert_rgb({250, 200, 70}, out[9]);    // L fixed
  assert_rgb({0, 140, 190}, out[10]);    // A fixed
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_both_themes_in_sprite_header_order);
  RUN_TEST(test_generated_colours_match_python);
  RUN_TEST(test_transit_honours_route_colour_and_falls_back_by_kind);
  RUN_TEST(test_ghibli_ignores_route_colour);
  RUN_TEST(test_role_colours_match_python);
  return UNITY_END();
}
