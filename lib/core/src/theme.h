#pragma once
#include <stdint.h>

#include "color.h"
#include "model.h"

// A theme is data: colours, what each sprite role means, and which scenery to
// draw. The data itself is generated from tools/board/themes/ into
// theme_data.h, so the simulator and the firmware cannot disagree.

enum ColourKey : uint8_t {
  C_BG, C_PANEL, C_PANEL_HI, C_LINE, C_TEXT, C_DIM,
  C_LIVE, C_WARN, C_ROAD, C_RAIL, C_WINDOW, C_DARK,
  COLOUR_COUNT
};

enum RoleMode : uint8_t { ROLE_UNUSED, ROLE_FIXED, ROLE_IS_BODY, ROLE_IS_SHADE, ROLE_IS_BRIGHT };

enum SceneryKind : uint8_t { SCENERY_NONE, SCENERY_TRANSIT, SCENERY_GHIBLI };

// Slot i is sprite role i as numbered in src/sprites.h (0 = transparent).
constexpr int ROLE_SLOTS = 11;

struct RoleSpec {
  RoleMode mode;
  Rgb fixed;  // used when mode == ROLE_FIXED
};

struct Theme {
  const char* name;
  Rgb colours[COLOUR_COUNT];
  Rgb fallback_bus;
  Rgb fallback_train;
  RoleSpec roles[ROLE_SLOTS];
  bool use_route_color;
  SceneryKind scenery;
};

uint8_t theme_count();
const Theme& theme(uint8_t index);  // out of range -> theme 0

// AT's own route_color when the theme honours it and the route has one;
// otherwise the theme's colour for the vehicle kind.
Rgb badge_colour(const Theme& th, const Watch& w);

// Resolve every sprite role to a colour for a vehicle whose body is `body`.
void role_colours(const Theme& th, Rgb body, Rgb out[ROLE_SLOTS]);
