#include "sprite_table.h"

#include "sprites.h"  // included by this file only: it defines the arrays

#define REF(T) SpriteRef{SPRITE_##T##_W, SPRITE_##T##_H, SPRITE_##T##_STRIDE, SPRITE_##T##_DATA}

SpriteRef sprite_for(uint8_t theme, SizeClass size, Kind kind) {
  const bool large = size == SizeClass::Large;
  const bool bus = kind == Kind::Bus;
  if (theme == THEME_GHIBLI) {
    if (large) return bus ? REF(GHIBLI_LARGE_BUS) : REF(GHIBLI_LARGE_TRAIN);
    return bus ? REF(GHIBLI_COMPACT_BUS) : REF(GHIBLI_COMPACT_TRAIN);
  }
  if (large) return bus ? REF(TRANSIT_LARGE_BUS) : REF(TRANSIT_LARGE_TRAIN);
  return bus ? REF(TRANSIT_COMPACT_BUS) : REF(TRANSIT_COMPACT_TRAIN);
}
