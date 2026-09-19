#include "shapes.h"

#include <cstdint>
#include <math.h>

// static_cast<int> truncates toward zero, as Python's int() does.
int hill(int dx) { return static_cast<int>(7 + 5 * sin(dx / 26.0) + 3 * sin(dx / 9.0)); }

int shore(int dx) { return static_cast<int>(4 + 3 * sin(dx / 18.0)); }

// Precomputed tables for hill() and shore() over the range [0, TERRAIN_DX_MAX).
// Values stay in roughly [0, 15], so int8_t is sufficient.
// Out-of-range dx (< 0 or >= TERRAIN_DX_MAX) falls back to direct computation.
int hill_at(int dx) {
  if (dx < 0 || dx >= TERRAIN_DX_MAX) return hill(dx);

  static bool filled = false;
  static int8_t table[TERRAIN_DX_MAX];

  if (!filled) {
    for (int i = 0; i < TERRAIN_DX_MAX; i++) {
      table[i] = hill(i);
    }
    filled = true;
  }

  return table[dx];
}

int shore_at(int dx) {
  if (dx < 0 || dx >= TERRAIN_DX_MAX) return shore(dx);

  static bool filled = false;
  static int8_t table[TERRAIN_DX_MAX];

  if (!filled) {
    for (int i = 0; i < TERRAIN_DX_MAX; i++) {
      table[i] = shore(i);
    }
    filled = true;
  }

  return table[dx];
}
