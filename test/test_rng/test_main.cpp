#include <unity.h>

#include "rng.h"
#include "shapes.h"

void setUp() {}
void tearDown() {}

void test_next_matches_python() {
  Rng r(7);
  TEST_ASSERT_EQUAL_UINT32(3923423697u, r.next());
  TEST_ASSERT_EQUAL_UINT32(2630631676u, r.next());
  TEST_ASSERT_EQUAL_UINT32(3981355051u, r.next());
}

void test_below_matches_python() {
  Rng r(7);
  const uint32_t want[] = {91, 61, 92, 4, 85};
  for (uint32_t w : want) TEST_ASSERT_EQUAL_UINT32(w, r.below(100));
}

void test_between_is_inclusive_and_matches_python() {
  Rng r(9);
  const int want[] = {10, 20, 9, 13, 13};
  for (int w : want) TEST_ASSERT_EQUAL_INT(w, r.between(9, 20));
}

void test_chance_matches_python() {
  Rng r(7);  // below(100) runs 91, 61, 92, 4
  TEST_ASSERT_FALSE(r.chance(35));
  TEST_ASSERT_FALSE(r.chance(35));
  TEST_ASSERT_FALSE(r.chance(35));
  TEST_ASSERT_TRUE(r.chance(35));
}

void test_same_seed_same_sequence() {
  Rng a(11), b(11);
  for (int i = 0; i < 100; i++) TEST_ASSERT_EQUAL_UINT32(a.next(), b.next());
}

void test_hill_matches_python() {
  TEST_ASSERT_EQUAL_INT(7, hill(0));
  TEST_ASSERT_EQUAL_INT(11, hill(10));
  TEST_ASSERT_EQUAL_INT(9, hill(50));
  TEST_ASSERT_EQUAL_INT(0, hill(100));
  TEST_ASSERT_EQUAL_INT(5, hill(300));
}

void test_shore_matches_python() {
  TEST_ASSERT_EQUAL_INT(4, shore(0));
  TEST_ASSERT_EQUAL_INT(5, shore(10));
  TEST_ASSERT_EQUAL_INT(5, shore(50));
  TEST_ASSERT_EQUAL_INT(1, shore(300));
}

void test_terrain_tables_equal_the_direct_functions() {
  for (int dx = -5; dx < TERRAIN_DX_MAX + 5; dx++) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(hill(dx), hill_at(dx), "hill");
    TEST_ASSERT_EQUAL_INT_MESSAGE(shore(dx), shore_at(dx), "shore");
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_next_matches_python);
  RUN_TEST(test_below_matches_python);
  RUN_TEST(test_between_is_inclusive_and_matches_python);
  RUN_TEST(test_chance_matches_python);
  RUN_TEST(test_same_seed_same_sequence);
  RUN_TEST(test_hill_matches_python);
  RUN_TEST(test_shore_matches_python);
  RUN_TEST(test_terrain_tables_equal_the_direct_functions);
  return UNITY_END();
}
