#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

#include "dechunk.h"

void setUp() {}
void tearDown() {}

namespace {
struct Source {
  const char* p;
  size_t left;
};

int next_byte(void* ctx) {
  Source* s = static_cast<Source*>(ctx);
  if (s->left == 0) return -1;
  s->left--;
  return static_cast<unsigned char>(*s->p++);
}

std::string drain(const char* wire, size_t len, bool* failed = nullptr) {
  Source src{wire, len};
  Dechunker d(next_byte, &src);
  std::string out;
  for (int c = d.read(); c >= 0; c = d.read()) out.push_back(static_cast<char>(c));
  if (failed != nullptr) *failed = d.failed();
  return out;
}

// A reader that hands ArduinoJson the raw wire, exactly as the ESP32 would
// without a Dechunker in between.
struct RawReader {
  Source* src;
  int read() { return next_byte(src); }
  size_t readBytes(char* buffer, size_t length) {
    size_t n = 0;
    for (; n < length; n++) {
      const int c = read();
      if (c < 0) break;
      buffer[n] = static_cast<char>(c);
    }
    return n;
  }
};
}  // namespace

void test_unwraps_chunks() {
  const char wire[] = "4\r\nabcd\r\n3\r\nefg\r\n0\r\n\r\n";
  TEST_ASSERT_EQUAL_STRING("abcdefg", drain(wire, sizeof wire - 1).c_str());
}

void test_chunk_sizes_are_hexadecimal() {
  const char wire[] = "a\r\n0123456789\r\n0\r\n\r\n";
  TEST_ASSERT_EQUAL_STRING("0123456789", drain(wire, sizeof wire - 1).c_str());
}

void test_accepts_chunk_extensions() {
  const char wire[] = "4;name=value\r\nabcd\r\n0\r\n\r\n";
  TEST_ASSERT_EQUAL_STRING("abcd", drain(wire, sizeof wire - 1).c_str());
}

void test_skips_blank_lines_between_chunks() {
  const char wire[] = "2\r\nab\r\n\r\n\r\n2\r\ncd\r\n0\r\n\r\n";
  bool failed = true;
  TEST_ASSERT_EQUAL_STRING("abcd", drain(wire, sizeof wire - 1, &failed).c_str());
  TEST_ASSERT_FALSE(failed);
}

void test_a_long_run_of_blank_lines_costs_no_stack() {
  // Each blank line used to be one more recursive call. A peer sending blank
  // lines must not be able to walk the fetch task off the end of its stack.
  std::string wire = "2\r\nab\r\n";
  for (int i = 0; i < 1000000; i++) wire += "\r\n";
  wire += "0\r\n\r\n";
  bool failed = true;
  TEST_ASSERT_EQUAL_STRING("ab", drain(wire.data(), wire.size(), &failed).c_str());
  TEST_ASSERT_FALSE(failed);
}

void test_a_clean_end_is_not_a_failure() {
  bool failed = true;
  const char wire[] = "1\r\nx\r\n0\r\n\r\n";
  drain(wire, sizeof wire - 1, &failed);
  TEST_ASSERT_FALSE(failed);
}

void test_a_truncated_body_is_a_failure() {
  bool failed = false;
  const char wire[] = "8\r\nabc";  // promised 8 bytes, source dies after 3
  TEST_ASSERT_EQUAL_STRING("abc", drain(wire, sizeof wire - 1, &failed).c_str());
  TEST_ASSERT_TRUE(failed);
}

void test_a_malformed_size_line_is_a_failure() {
  bool failed = false;
  const char wire[] = "zz\r\nabcd\r\n0\r\n\r\n";
  TEST_ASSERT_EQUAL_STRING("", drain(wire, sizeof wire - 1, &failed).c_str());
  TEST_ASSERT_TRUE(failed);
}

void test_read_bytes_fills_the_buffer() {
  const char wire[] = "4\r\nabcd\r\n3\r\nefg\r\n0\r\n\r\n";
  Source src{wire, sizeof wire - 1};
  Dechunker d(next_byte, &src);
  char buf[8] = {0};
  TEST_ASSERT_EQUAL_size_t(7, d.readBytes(buf, sizeof buf - 1));
  TEST_ASSERT_EQUAL_STRING("abcdefg", buf);
}

void test_arduinojson_parses_through_it() {
  // The whole point: this is what AT's GTFS API puts on the wire.
  const char wire[] = "10\r\n{\"data\":[{\"a\":1}\r\n2\r\n]}\r\n0\r\n\r\n";
  Source src{wire, sizeof wire - 1};
  Dechunker d(next_byte, &src);
  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, d));
  TEST_ASSERT_EQUAL_INT(1, doc["data"][0]["a"].as<int>());
}

void test_without_dechunking_a_filtered_parse_silently_returns_nothing() {
  // The firmware always parses with a filter, and that is what makes the
  // failure silent: the chunk-size line parses as a number, the filter drops
  // it, and deserializeJson reports success with an empty document. Zero
  // departures, no error, forever (docs/hardware-notes.md).
  JsonDocument filter;
  filter["data"][0]["attributes"]["trip_id"] = true;
  const char* wire = "2561\r\n{\"data\":[{\"attributes\":{\"trip_id\":\"x\"}}]}";
  Source src{wire, strlen(wire)};
  RawReader raw{&src};
  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, raw, DeserializationOption::Filter(filter)));
  TEST_ASSERT_EQUAL_INT(0, static_cast<int>(doc["data"].size()));
}

void test_dechunking_the_same_filtered_parse_returns_the_row() {
  JsonDocument filter;
  filter["data"][0]["attributes"]["trip_id"] = true;
  const char* wire = "29\r\n{\"data\":[{\"attributes\":{\"trip_id\":\"x\"}}]}\r\n0\r\n\r\n";
  Source src{wire, strlen(wire)};
  Dechunker d(next_byte, &src);
  JsonDocument doc;
  TEST_ASSERT_FALSE(deserializeJson(doc, d, DeserializationOption::Filter(filter)));
  TEST_ASSERT_EQUAL_INT(1, static_cast<int>(doc["data"].size()));
  TEST_ASSERT_EQUAL_STRING("x", doc["data"][0]["attributes"]["trip_id"]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_unwraps_chunks);
  RUN_TEST(test_chunk_sizes_are_hexadecimal);
  RUN_TEST(test_accepts_chunk_extensions);
  RUN_TEST(test_skips_blank_lines_between_chunks);
  RUN_TEST(test_a_long_run_of_blank_lines_costs_no_stack);
  RUN_TEST(test_a_clean_end_is_not_a_failure);
  RUN_TEST(test_a_truncated_body_is_a_failure);
  RUN_TEST(test_a_malformed_size_line_is_a_failure);
  RUN_TEST(test_read_bytes_fills_the_buffer);
  RUN_TEST(test_arduinojson_parses_through_it);
  RUN_TEST(test_without_dechunking_a_filtered_parse_silently_returns_nothing);
  RUN_TEST(test_dechunking_the_same_filtered_parse_returns_the_row);
  return UNITY_END();
}
