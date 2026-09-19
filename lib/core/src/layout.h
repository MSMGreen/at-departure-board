#pragma once
#include <stdint.h>

// Port of tools/board/layout.py. Pure geometry, no drawing.
//
// The one idea worth stating: a vehicle's x position IS its time to arrival.
// It enters at the left at HORIZON_S and touches the stop marker at zero.

constexpr int W = 320;
constexpr int H = 240;
constexpr int STATUS_H = 18;
constexpr int32_t HORIZON_S = 20 * 60;

constexpr int MARGIN = 4;         // screen edge to lane card
constexpr int PAD = 6;            // card edge to content
constexpr int MARKER_INSET = 58;  // right edge to the stop marker
constexpr int TRACK_LIFT = 12;    // track height above the card's bottom edge

constexpr int LARGE_MAX_LANES = 2;
constexpr int MAX_LANES = 4;

struct Rect {
  int x0, y0, x1, y1;
  int width() const { return x1 - x0; }
  int height() const { return y1 - y0; }
};

struct Lane {
  Rect rect;
  Rect badge;
  int headsign_x, headsign_y;
  int minutes_x, minutes_y;
  int following_x, following_y;
  Rect track;
  int marker_x;
  int sprite_baseline;
};

// At 3+ lanes large art collides with the badge, so compact is not a
// preference - it is the only thing that fits.
enum class SizeClass : uint8_t { Large, Compact };
SizeClass size_class(int n);

bool lane_rect(int index, int n, Rect* out);

// Precondition: 1 <= n <= MAX_LANES and 0 <= index < n. Out of range returns
// a zeroed Lane rather than throwing; there are no exceptions on the device.
Lane lane(int index, int n);

int vehicle_x(int32_t eta_s, const Lane& lane, int sprite_w);

// Seconds to the number actually shown. Never negative.
int display_minutes(int32_t eta_s);
