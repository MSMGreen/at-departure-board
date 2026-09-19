#include <string.h>
#include <unity.h>

#include "config_schema.h"

void setUp() {}
void tearDown() {}

namespace {
const char* GOOD =
    "{\"v\":1,\"location\":\"Kingsland\",\"theme\":1,\"watches\":["
    "{\"label\":\"to Wynyard Quarter\",\"stop_code\":\"8213\","
    "\"route_short_name\":\"20\",\"toward_stop_code\":\"1060\",\"enabled\":true},"
    "{\"label\":\"to Waitemata\",\"stop_code\":\"122\","
    "\"route_short_name\":\"\",\"toward_stop_code\":\"133\",\"enabled\":false}]}";
}  // namespace

void test_parses_a_good_document() {
  Config c{};
  TEST_ASSERT_TRUE(cfg_parse(GOOD, &c, 2) == CfgError::Ok);
  TEST_ASSERT_EQUAL_STRING("Kingsland", c.location);
  TEST_ASSERT_EQUAL_UINT8(1, c.theme);
  TEST_ASSERT_EQUAL_UINT8(2, c.n_watches);
  TEST_ASSERT_EQUAL_STRING("8213", c.watches[0].stop_code);
  TEST_ASSERT_EQUAL_STRING("20", c.watches[0].route_short_name);
  TEST_ASSERT_TRUE(c.watches[0].enabled);
  TEST_ASSERT_FALSE(c.watches[1].enabled);
}

void test_empty_route_short_name_is_allowed() {
  // "" means any route, which is what carries a rail watch through the CRL
  // rename. Rejecting it would break the shipped default config.
  Config c{};
  TEST_ASSERT_TRUE(cfg_parse(GOOD, &c, 2) == CfgError::Ok);
  TEST_ASSERT_EQUAL_STRING("", c.watches[1].route_short_name);
}

void test_theme_is_clamped_not_rejected() {
  Config c{};
  const char* j =
      "{\"v\":1,\"location\":\"X\",\"theme\":99,\"watches\":["
      "{\"label\":\"a\",\"stop_code\":\"1\",\"route_short_name\":\"\","
      "\"toward_stop_code\":\"\",\"enabled\":true}]}";
  TEST_ASSERT_TRUE(cfg_parse(j, &c, 2) == CfgError::Ok);
  TEST_ASSERT_EQUAL_UINT8(1, c.theme);  // clamped to theme_max - 1
}

void test_rejects_garbage_and_wrong_version() {
  Config c{};
  TEST_ASSERT_TRUE(cfg_parse("not json at all", &c, 2) == CfgError::BadJson);
  TEST_ASSERT_TRUE(cfg_parse("", &c, 2) == CfgError::BadJson);
  TEST_ASSERT_TRUE(cfg_parse("{\"v\":1,\"location\":\"X\"", &c, 2) == CfgError::BadJson);
  TEST_ASSERT_TRUE(
      cfg_parse("{\"v\":2,\"location\":\"X\",\"watches\":[]}", &c, 2) == CfgError::BadVersion);
  TEST_ASSERT_TRUE(
      cfg_parse("{\"location\":\"X\",\"watches\":[]}", &c, 2) == CfgError::BadVersion);
}

void test_rejects_too_many_watches() {
  Config c{};
  char j[CFG_JSON_CAP];
  int p = snprintf(j, sizeof j, "{\"v\":1,\"location\":\"X\",\"theme\":0,\"watches\":[");
  for (int i = 0; i < MAX_WATCHES + 1; i++) {
    p += snprintf(j + p, sizeof(j) - p,
                  "%s{\"label\":\"a\",\"stop_code\":\"1\",\"route_short_name\":\"\","
                  "\"toward_stop_code\":\"\",\"enabled\":true}",
                  i ? "," : "");
  }
  snprintf(j + p, sizeof(j) - p, "]}");
  TEST_ASSERT_TRUE(cfg_parse(j, &c, 2) == CfgError::TooManyWatches);
}

void test_rejects_no_watches_and_none_enabled() {
  Config c{};
  TEST_ASSERT_TRUE(
      cfg_parse("{\"v\":1,\"location\":\"X\",\"theme\":0,\"watches\":[]}", &c, 2) ==
      CfgError::NoWatches);
  const char* all_off =
      "{\"v\":1,\"location\":\"X\",\"theme\":0,\"watches\":["
      "{\"label\":\"a\",\"stop_code\":\"1\",\"route_short_name\":\"\","
      "\"toward_stop_code\":\"\",\"enabled\":false}]}";
  TEST_ASSERT_TRUE(cfg_parse(all_off, &c, 2) == CfgError::NoWatches);
}

void test_rejects_missing_stop_code() {
  Config c{};
  const char* j =
      "{\"v\":1,\"location\":\"X\",\"theme\":0,\"watches\":["
      "{\"label\":\"a\",\"stop_code\":\"\",\"route_short_name\":\"\","
      "\"toward_stop_code\":\"\",\"enabled\":true}]}";
  TEST_ASSERT_TRUE(cfg_parse(j, &c, 2) == CfgError::MissingStopCode);
}

void test_rejects_overlong_fields() {
  Config c{};
  char big[CFG_FIELD_CAP + 8];
  memset(big, 'x', sizeof big);
  big[sizeof(big) - 1] = '\0';
  char j[CFG_JSON_CAP];
  snprintf(j, sizeof j,
           "{\"v\":1,\"location\":\"X\",\"theme\":0,\"watches\":["
           "{\"label\":\"%s\",\"stop_code\":\"1\",\"route_short_name\":\"\","
           "\"toward_stop_code\":\"\",\"enabled\":true}]}",
           big);
  TEST_ASSERT_TRUE(cfg_parse(j, &c, 2) == CfgError::FieldTooLong);

  char loc[CFG_LOCATION_CAP + 8];
  memset(loc, 'y', sizeof loc);
  loc[sizeof(loc) - 1] = '\0';
  snprintf(j, sizeof j,
           "{\"v\":1,\"location\":\"%s\",\"theme\":0,\"watches\":["
           "{\"label\":\"a\",\"stop_code\":\"1\",\"route_short_name\":\"\","
           "\"toward_stop_code\":\"\",\"enabled\":true}]}",
           loc);
  TEST_ASSERT_TRUE(cfg_parse(j, &c, 2) == CfgError::LocationTooLong);
}

void test_failed_parse_leaves_the_target_alone() {
  // config_save_json must be able to reject without destroying live config.
  Config c{};
  TEST_ASSERT_TRUE(cfg_parse(GOOD, &c, 2) == CfgError::Ok);
  TEST_ASSERT_TRUE(cfg_parse("rubbish", &c, 2) == CfgError::BadJson);
  TEST_ASSERT_EQUAL_STRING("Kingsland", c.location);
  TEST_ASSERT_EQUAL_UINT8(2, c.n_watches);
}

void test_every_error_has_text() {
  const CfgError all[] = {CfgError::Ok,           CfgError::BadJson,
                          CfgError::BadVersion,   CfgError::TooManyWatches,
                          CfgError::NoWatches,    CfgError::MissingStopCode,
                          CfgError::FieldTooLong, CfgError::LocationTooLong};
  for (CfgError e : all) {
    const char* t = cfg_error_text(e);
    TEST_ASSERT_NOT_NULL(t);
    TEST_ASSERT_TRUE(strlen(t) > 0);
  }
}

void test_round_trips_through_serialise() {
  Config a{};
  TEST_ASSERT_TRUE(cfg_parse(GOOD, &a, 2) == CfgError::Ok);
  char buf[CFG_JSON_CAP];
  const size_t n = cfg_serialize(a, buf, sizeof buf);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_size_t(n, strlen(buf));

  Config b{};
  TEST_ASSERT_TRUE(cfg_parse(buf, &b, 2) == CfgError::Ok);
  TEST_ASSERT_EQUAL_STRING(a.location, b.location);
  TEST_ASSERT_EQUAL_UINT8(a.theme, b.theme);
  TEST_ASSERT_EQUAL_UINT8(a.n_watches, b.n_watches);
  for (int i = 0; i < a.n_watches; i++) {
    TEST_ASSERT_EQUAL_STRING(a.watches[i].label, b.watches[i].label);
    TEST_ASSERT_EQUAL_STRING(a.watches[i].stop_code, b.watches[i].stop_code);
    TEST_ASSERT_EQUAL_STRING(a.watches[i].route_short_name, b.watches[i].route_short_name);
    TEST_ASSERT_EQUAL_STRING(a.watches[i].toward_stop_code, b.watches[i].toward_stop_code);
    // The disabled watch must survive the round trip as disabled.
    TEST_ASSERT_EQUAL_INT(a.watches[i].enabled, b.watches[i].enabled);
  }
}

void test_serialise_refuses_a_small_buffer() {
  Config a{};
  TEST_ASSERT_TRUE(cfg_parse(GOOD, &a, 2) == CfgError::Ok);
  char small[16];
  TEST_ASSERT_EQUAL_size_t(0, cfg_serialize(a, small, sizeof small));
  TEST_ASSERT_EQUAL_STRING("", small);
}

void test_publish_compacts_around_a_disabled_watch() {
  const char* j =
      "{\"v\":1,\"location\":\"X\",\"theme\":0,\"watches\":["
      "{\"label\":\"first\",\"stop_code\":\"1\",\"route_short_name\":\"\","
      "\"toward_stop_code\":\"\",\"enabled\":true},"
      "{\"label\":\"middle\",\"stop_code\":\"2\",\"route_short_name\":\"\","
      "\"toward_stop_code\":\"\",\"enabled\":false},"
      "{\"label\":\"last\",\"stop_code\":\"3\",\"route_short_name\":\"\","
      "\"toward_stop_code\":\"\",\"enabled\":true}]}";
  Config c{};
  TEST_ASSERT_TRUE(cfg_parse(j, &c, 2) == CfgError::Ok);
  TEST_ASSERT_EQUAL_UINT8(3, c.n_watches);

  WatchConfig pub[MAX_WATCHES];
  TEST_ASSERT_EQUAL_UINT8(2, cfg_publish(c, pub));
  TEST_ASSERT_EQUAL_STRING("first", pub[0].label);
  TEST_ASSERT_EQUAL_STRING("1", pub[0].stop_code);
  TEST_ASSERT_EQUAL_STRING("last", pub[1].label);
  TEST_ASSERT_EQUAL_STRING("3", pub[1].stop_code);
}

void test_published_pointers_reach_into_the_config() {
  Config c{};
  TEST_ASSERT_TRUE(cfg_parse(GOOD, &c, 2) == CfgError::Ok);
  WatchConfig pub[MAX_WATCHES];
  TEST_ASSERT_EQUAL_UINT8(1, cfg_publish(c, pub));  // second watch is disabled
  TEST_ASSERT_EQUAL_PTR(c.watches[0].stop_code, pub[0].stop_code);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_parses_a_good_document);
  RUN_TEST(test_empty_route_short_name_is_allowed);
  RUN_TEST(test_theme_is_clamped_not_rejected);
  RUN_TEST(test_rejects_garbage_and_wrong_version);
  RUN_TEST(test_rejects_too_many_watches);
  RUN_TEST(test_rejects_no_watches_and_none_enabled);
  RUN_TEST(test_rejects_missing_stop_code);
  RUN_TEST(test_rejects_overlong_fields);
  RUN_TEST(test_failed_parse_leaves_the_target_alone);
  RUN_TEST(test_every_error_has_text);
  RUN_TEST(test_round_trips_through_serialise);
  RUN_TEST(test_serialise_refuses_a_small_buffer);
  RUN_TEST(test_publish_compacts_around_a_disabled_watch);
  RUN_TEST(test_published_pointers_reach_into_the_config);
  return UNITY_END();
}
