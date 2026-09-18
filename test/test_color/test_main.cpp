#include <unity.h>

#include "color.h"

void setUp() {}
void tearDown() {}

static void assert_rgb(Rgb want, Rgb got) {
  TEST_ASSERT_EQUAL_UINT8(want.r, got.r);
  TEST_ASSERT_EQUAL_UINT8(want.g, got.g);
  TEST_ASSERT_EQUAL_UINT8(want.b, got.b);
}

void test_parse_hex_accepts_bare_hashed_and_lowercase() {
  Rgb c{};
  TEST_ASSERT_TRUE(parse_hex("97C93D", &c));
  assert_rgb({151, 201, 61}, c);
  TEST_ASSERT_TRUE(parse_hex("#97c93d", &c));
  assert_rgb({151, 201, 61}, c);
  TEST_ASSERT_TRUE(parse_hex("  97C93D ", &c));
  assert_rgb({151, 201, 61}, c);
}

void test_parse_hex_treats_black_as_absent() {
  // HUIA's route_color is #000000: invisible on our ground.
  Rgb c{1, 2, 3};
  TEST_ASSERT_FALSE(parse_hex("000000", &c));
  assert_rgb({1, 2, 3}, c);  // untouched
}

void test_parse_hex_rejects_garbage() {
  Rgb c{};
  TEST_ASSERT_FALSE(parse_hex(nullptr, &c));
  TEST_ASSERT_FALSE(parse_hex("", &c));
  TEST_ASSERT_FALSE(parse_hex("zz", &c));
  TEST_ASSERT_FALSE(parse_hex("12345", &c));
  TEST_ASSERT_FALSE(parse_hex("1234567", &c));
  TEST_ASSERT_FALSE(parse_hex("GGGGGG", &c));
}

void test_shade_matches_python() {
  assert_rgb({83, 110, 33}, shade({151, 201, 61}));
  assert_rgb({60, 80, 24}, shade({151, 201, 61}, 0.4));
}

void test_bright_matches_python() {
  assert_rgb({255, 255, 137}, bright({151, 201, 61}));
  assert_rgb({44, 51, 64}, bright({22, 29, 42}, 1.0, 22));
  assert_rgb({80, 89, 106}, bright({22, 29, 42}, 1.3, 52));
}

void test_dimmed_matches_render_dim_factor() {
  assert_rgb({104, 107, 110}, dimmed({233, 238, 245}));
  assert_rgb({5, 7, 10}, dimmed({12, 16, 24}));
}

void test_to565() {
  TEST_ASSERT_EQUAL_HEX16(0xF800, to565({255, 0, 0}));
  TEST_ASSERT_EQUAL_HEX16(0x07E0, to565({0, 255, 0}));
  TEST_ASSERT_EQUAL_HEX16(0x001F, to565({0, 0, 255}));
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, to565({255, 255, 255}));
  TEST_ASSERT_EQUAL_HEX16(0x055C, to565({0, 168, 224}));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parse_hex_accepts_bare_hashed_and_lowercase);
  RUN_TEST(test_parse_hex_treats_black_as_absent);
  RUN_TEST(test_parse_hex_rejects_garbage);
  RUN_TEST(test_shade_matches_python);
  RUN_TEST(test_bright_matches_python);
  RUN_TEST(test_dimmed_matches_render_dim_factor);
  RUN_TEST(test_to565);
  return UNITY_END();
}
