#pragma once
#include <stdint.h>

// Port of tools/board/palette.py plus the RGB565 conversion the panel needs.
// Arithmetic is in double and truncates, exactly as Python's int() does, so a
// colour computed here is the colour the simulator computed.

struct Rgb {
  uint8_t r, g, b;
};

constexpr double DIM_FACTOR = 0.45;  // render.py DIM_FACTOR

// '97C93D' or '#97C93D' -> true. False, leaving *out untouched, for anything
// unusable - including #000000, which AT sends for HUIA and which would be
// invisible on our ground.
bool parse_hex(const char* s, Rgb* out);

Rgb shade(Rgb c, double factor = 0.55);
Rgb bright(Rgb c, double factor = 1.6, int floor = 40);
Rgb dimmed(Rgb c);
uint16_t to565(Rgb c);
