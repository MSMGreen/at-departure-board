#include "backlight.h"

#include <Arduino.h>

namespace {
constexpr int PIN = 32;
constexpr int CHANNEL = 0;
constexpr int FREQ_HZ = 5000;
constexpr int BITS = 8;
}  // namespace

void backlight_begin() {
  ledcSetup(CHANNEL, FREQ_HZ, BITS);
  ledcAttachPin(PIN, CHANNEL);
  backlight_set(255);
}

void backlight_set(uint8_t level) { ledcWrite(CHANNEL, level); }
