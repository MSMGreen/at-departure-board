// Firmware entry point. Only DEMO_MODE exists until the data-path plan lands:
// the canonical scenes from tools/board/scenes.py, counting down in real time.
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <esp_heap_caps.h>

#include "backlight.h"
#include "demo.h"
#include "ui.h"

#ifndef DEMO_MODE
#error "Only DEMO_MODE is implemented; the live data path is a later plan."
#endif

namespace {

TFT_eSPI tft;
Ui ui(tft);

constexpr uint32_t FRAME_MS = 1000 / 15;  // 15 fps, spec section 5
constexpr uint32_t REPORT_MS = 5000;

void report(uint32_t now, uint32_t frames, uint32_t draw_ms_total, uint32_t since) {
  Serial.printf("fps %.1f  draw %lums  heap %u  largest %u  min-ever %u\n",
                frames * 1000.0f / (now - since),
                static_cast<unsigned long>(draw_ms_total / frames), ESP.getFreeHeap(),
                heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), ESP.getMinFreeHeap());
}

}  // namespace

void setup() {
  Serial.begin(115200);
  backlight_begin();
  tft.init();
  tft.setRotation(1);
  if (!ui.begin()) {
    Serial.println("FATAL: band sprite allocation failed");
    tft.fillScreen(TFT_RED);
    for (;;) delay(1000);
  }
  Serial.println("BOOT-OK demo");
}

void loop() {
  static uint32_t frames = 0, draw_ms_total = 0, last_report = 0;
  static int last_scene = -1;

  const uint32_t start = millis();
  const int scene = static_cast<int>((start / DEMO_SCENE_MS) % demo_scene_count());
  if (scene != last_scene) {
    const Board b = demo_board(start);
    Serial.printf("scene %s  theme %u\n", demo_scene(scene).name, b.theme);
    last_scene = scene;
  }

  ui.draw(demo_board(start), start / 1000.0f);

  const uint32_t took = millis() - start;
  frames++;
  draw_ms_total += took;
  if (start - last_report >= REPORT_MS) {
    report(start, frames, draw_ms_total, last_report);
    frames = 0;
    draw_ms_total = 0;
    last_report = start;
  }
  if (took < FRAME_MS) delay(FRAME_MS - took);
}
