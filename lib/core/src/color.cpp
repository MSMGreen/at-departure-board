#include "color.h"

#include <ctype.h>
#include <string.h>

namespace {

int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

uint8_t clamp8(int v) { return v < 0 ? 0 : v > 255 ? 255 : static_cast<uint8_t>(v); }

}  // namespace

bool parse_hex(const char* s, Rgb* out) {
  if (s == nullptr) return false;
  while (isspace(static_cast<unsigned char>(*s))) s++;
  while (*s == '#') s++;
  size_t n = strlen(s);
  while (n > 0 && isspace(static_cast<unsigned char>(s[n - 1]))) n--;
  if (n != 6) return false;
  int v[6];
  for (int i = 0; i < 6; i++) {
    v[i] = hexval(s[i]);
    if (v[i] < 0) return false;
  }
  const Rgb c{static_cast<uint8_t>(v[0] * 16 + v[1]),
              static_cast<uint8_t>(v[2] * 16 + v[3]),
              static_cast<uint8_t>(v[4] * 16 + v[5])};
  if (c.r == 0 && c.g == 0 && c.b == 0) return false;
  *out = c;
  return true;
}

Rgb shade(Rgb c, double factor) {
  return {clamp8(static_cast<int>(c.r * factor)),
          clamp8(static_cast<int>(c.g * factor)),
          clamp8(static_cast<int>(c.b * factor))};
}

Rgb bright(Rgb c, double factor, int floor) {
  return {clamp8(static_cast<int>(c.r * factor) + floor),
          clamp8(static_cast<int>(c.g * factor) + floor),
          clamp8(static_cast<int>(c.b * factor) + floor)};
}

Rgb dimmed(Rgb c) { return shade(c, DIM_FACTOR); }

uint16_t to565(Rgb c) {
  return static_cast<uint16_t>(((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3));
}
