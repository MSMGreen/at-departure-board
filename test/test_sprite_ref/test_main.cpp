#include <unity.h>

#include "sprite_ref.h"

void setUp() {}
void tearDown() {}

// 3x2 sprite, stride 2: row 0 = roles 1 2 3, row 1 = roles 0 10 0.
static const uint8_t DATA[] = {0x12, 0x30, 0x0A, 0x00};
static const SpriteRef S{3, 2, 2, DATA};

void test_high_nibble_is_the_left_pixel() {
  TEST_ASSERT_EQUAL_UINT8(1, sprite_role(S, 0, 0));
  TEST_ASSERT_EQUAL_UINT8(2, sprite_role(S, 1, 0));
  TEST_ASSERT_EQUAL_UINT8(3, sprite_role(S, 2, 0));
}

void test_rows_use_the_stride() {
  TEST_ASSERT_EQUAL_UINT8(0, sprite_role(S, 0, 1));
  TEST_ASSERT_EQUAL_UINT8(10, sprite_role(S, 1, 1));
  TEST_ASSERT_EQUAL_UINT8(0, sprite_role(S, 2, 1));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_high_nibble_is_the_left_pixel);
  RUN_TEST(test_rows_use_the_stride);
  return UNITY_END();
}
