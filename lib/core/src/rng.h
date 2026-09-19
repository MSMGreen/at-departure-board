#pragma once
#include <stdint.h>

// Port of tools/board/scenery.Rng: Numerical Recipes' 32-bit LCG.
//
// Scenery is redrawn every frame (and every band of every frame), so it must
// land in the same place every time AND in the same place the Python put it.
// uint32_t arithmetic wraps exactly as the Python's `& 0xFFFFFFFF` does.
struct Rng {
  uint32_t state;

  explicit Rng(uint32_t seed) : state(seed * 1664525u + 1013904223u) {}

  uint32_t next() {
    state = state * 1664525u + 1013904223u;
    return state;
  }

  // 0 <= result < n, for n up to 65535. High bits: an LCG's low bits are weak.
  uint32_t below(uint32_t n) { return ((next() >> 16) * n) >> 16; }

  // Inclusive of both ends, matching random.randint.
  int between(int lo, int hi) { return lo + static_cast<int>(below(static_cast<uint32_t>(hi - lo + 1))); }

  bool chance(uint32_t percent) { return below(100) < percent; }
};
