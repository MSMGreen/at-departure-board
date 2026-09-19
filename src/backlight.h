#pragma once
#include <stdint.h>

// The backlight is on GPIO32 through LEDC rather than tied to 3V3, which is
// what lets the board dim instead of only switching off (spec section 8).
void backlight_begin();
void backlight_set(uint8_t level);  // 0 = off, 255 = full
