// Skeleton: proves the build, the display config and backlight PWM.
#include <Arduino.h>
#include <TFT_eSPI.h>

#include "backlight.h"

namespace {
TFT_eSPI tft;
}

void setup() {
  Serial.begin(115200);
  backlight_begin();
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("firmware skeleton", 160, 100, 2);
  Serial.println("BOOT-OK skeleton");
}

void loop() {
  // Step the backlight so PWM on GPIO32 is visibly working, not just on.
  static const uint8_t levels[] = {255, 128, 32, 128};
  static uint8_t i = 0;
  backlight_set(levels[i]);
  char buf[24];
  snprintf(buf, sizeof buf, "backlight %3u", levels[i]);
  tft.fillRect(80, 130, 160, 20, TFT_BLACK);
  tft.drawString(buf, 160, 140, 2);
  Serial.println(buf);
  i = (i + 1) % 4;
  delay(1500);
}
