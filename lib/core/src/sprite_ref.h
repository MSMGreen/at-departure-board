#pragma once
#include <stdint.h>

// A 4bpp indexed sprite as exported to src/sprites.h: two pixels per byte,
// high nibble leftmost, 0 transparent, other values sprite roles.
struct SpriteRef {
  uint8_t w, h, stride;
  const uint8_t* data;
};

inline uint8_t sprite_role(const SpriteRef& s, int x, int y) {
  const uint8_t b = s.data[y * s.stride + x / 2];
  return (x & 1) ? (b & 0x0F) : (b >> 4);
}
