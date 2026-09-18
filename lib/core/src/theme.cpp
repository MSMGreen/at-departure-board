#include "theme.h"

#include "theme_data.h"

uint8_t theme_count() { return THEME_DATA_COUNT; }

const Theme& theme(uint8_t index) { return THEME_DATA[index < THEME_DATA_COUNT ? index : 0]; }

Rgb badge_colour(const Theme& th, const Watch& w) {
  if (th.use_route_color && w.has_route_color) return w.route_color;
  return w.kind == Kind::Bus ? th.fallback_bus : th.fallback_train;
}

void role_colours(const Theme& th, Rgb body, Rgb out[ROLE_SLOTS]) {
  for (int i = 0; i < ROLE_SLOTS; i++) {
    switch (th.roles[i].mode) {
      case ROLE_IS_BODY:   out[i] = body; break;
      case ROLE_IS_SHADE:  out[i] = shade(body); break;
      case ROLE_IS_BRIGHT: out[i] = bright(body); break;
      case ROLE_FIXED:     out[i] = th.roles[i].fixed; break;
      default:             out[i] = {0, 0, 0}; break;
    }
  }
}
