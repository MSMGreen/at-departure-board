#pragma once
#include <stdint.h>

#include "color.h"

// Port of tools/board/model.py: what the screen is showing, independent of how
// it is drawn. Fixed-size arrays, no heap - the board runs for months.

enum class Kind : uint8_t { Bus, Train };

constexpr int MAX_WATCHES = 4;
constexpr int MAX_DEPARTURES = 4;
constexpr int32_t STALE_AFTER_S = 90;

struct Departure {
  int32_t eta_s;
  bool live;
  bool cancelled;
};

struct Watch {
  char badge[8];
  char headsign[40];
  Kind kind;
  bool has_route_color;
  Rgb route_color;
  // Set when the board cannot honestly show times for this watch (spec 8).
  // A watch with a message shows no departures at all.
  char message[32];
  Departure deps[MAX_DEPARTURES];
  uint8_t n_deps;

  const Departure* next() const { return n_deps > 0 ? &deps[0] : nullptr; }
  const Departure* following() const { return n_deps > 1 ? &deps[1] : nullptr; }
  bool add(Departure d);    // false when full
  void sort_departures();   // by eta, stable - as Python's sorted()
};

struct Board {
  Watch watches[MAX_WATCHES];
  uint8_t n_watches;
  char clock[6];  // "17:42"
  int32_t stale_s;
  bool dimmed;
  uint8_t theme;  // index into the generated theme table
  char location[24];

  bool is_stale() const { return stale_s > STALE_AFTER_S; }
};

// Zeroes *w and fills it. Text is truncated to fit; route_color_hex may be
// null, and #000000 counts as absent (see parse_hex).
void watch_init(Watch* w, const char* badge, const char* headsign, Kind kind,
                const char* route_color_hex);
void watch_set_message(Watch* w, const char* message);  // null or "" clears it
void board_set_location(Board* b, const char* location);
