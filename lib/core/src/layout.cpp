#include "layout.h"

#include <math.h>

SizeClass size_class(int n) {
  return n <= LARGE_MAX_LANES ? SizeClass::Large : SizeClass::Compact;
}

bool lane_rect(int index, int n, Rect* out) {
  if (n < 1 || n > MAX_LANES || index < 0 || index >= n) return false;
  const int lh = (H - STATUS_H) / n;
  *out = {0, STATUS_H + index * lh, W, STATUS_H + (index + 1) * lh};
  return true;
}

Lane lane(int index, int n) {
  Lane l{};
  Rect r{};
  if (!lane_rect(index, n, &r)) return l;

  const int badge_h = 18;
  const int track_y = r.y1 - TRACK_LIFT;

  l.rect = r;
  l.badge = {r.x0 + 10, r.y0 + PAD, r.x0 + 10 + 34, r.y0 + PAD + badge_h};
  l.headsign_x = l.badge.x1 + 8;
  l.headsign_y = r.y0 + PAD + 2;
  l.minutes_x = W - 12;
  l.minutes_y = r.y0 + PAD;
  l.following_x = W - 12;
  l.following_y = r.y0 + PAD + 22;
  l.marker_x = W - MARKER_INSET;
  l.track = {r.x0 + 10, track_y, l.marker_x, track_y + 2};
  l.sprite_baseline = track_y;
  return l;
}

int vehicle_x(int32_t eta_s, const Lane& l, int sprite_w) {
  const int x_start = l.track.x0;
  const int x_stop = l.marker_x - sprite_w;
  const int32_t clamped = eta_s < 0 ? 0 : eta_s > HORIZON_S ? HORIZON_S : eta_s;
  const double progress = 1.0 - static_cast<double>(clamped) / HORIZON_S;
  // Python's round() is round-half-to-even. nearbyint() in the default
  // rounding mode is too; lround() is not, and would drift a pixel on halves.
  return static_cast<int>(nearbyint(x_start + progress * (x_stop - x_start)));
}

int display_minutes(int32_t eta_s) { return eta_s < 0 ? 0 : static_cast<int>(eta_s / 60); }
