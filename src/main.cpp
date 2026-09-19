// Firmware entry point. DEMO_MODE plays the canonical scenes from
// tools/board/scenes.py, counting down in real time; otherwise this is the
// live data path (spec section 9).
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <esp_heap_caps.h>
#include <time.h>

#include "backlight.h"
#include "ui.h"

#ifdef DEMO_MODE
#include "demo.h"
#else
#include <WiFi.h>

#include "at_client.h"
#include "live.h"
#include "secrets.h"
#include "watch_config.h"
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

#ifdef DEMO_MODE
Board board_now(uint32_t ms, int64_t) { return demo_board(ms); }
#else
Snapshot snap;  // static storage: a Snapshot is far too big for a task stack
Board board_now(uint32_t, int64_t now) { return build_board(snap, WATCHES, LOCATION, now, 0); }
#endif

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

#ifdef DEMO_MODE
  Serial.println("BOOT-OK demo");
#else
  snap.n_watches = N_WATCHES;  // every watch left Starting until fetched

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  bool wifi_ok = false;
  for (int i = 0; i < 20; i++) {
    if (WiFi.status() == WL_CONNECTED) {
      wifi_ok = true;
      break;
    }
    Serial.printf("wifi: status %d\n", WiFi.status());
    delay(1000);
  }
  if (wifi_ok) {
    Serial.printf("wifi: connected, ip %s rssi %d\n", WiFi.localIP().toString().c_str(),
                  WiFi.RSSI());
  } else {
    Serial.printf("wifi: FAILED status %d\n", WiFi.status());
    Serial.println("skipping self-check: no wifi");
  }

  bool time_ok = false;
  if (wifi_ok) {
    configTime(0, 0, "pool.ntp.org");  // UTC - nztime does the local conversion
    for (int i = 0; i < 15; i++) {
      if (time(nullptr) > 1700000000) {
        time_ok = true;
        break;
      }
      delay(1000);
    }
    if (time_ok) {
      Serial.printf("time: %lld\n", static_cast<int64_t>(time(nullptr)));
    } else {
      Serial.println("time: FAILED");
      Serial.println("skipping self-check: no time");
    }
  }

  if (wifi_ok && time_ok) {
    JsonDocument filter, doc;
    stop_filter(filter);
    char url[256];
    for (int i = 0; i < N_WATCHES; i++) {
      url_stop_by_code(url, sizeof url, WATCHES[i].stop_code);
      const int status = at_get(url, doc, filter);
      StopInfo info{};
      const bool ok = status == 200 && parse_stop(doc, &info);
      Serial.printf("stop %s -> HTTP %d %s (location_type %d)\n", WATCHES[i].stop_code,
                    status, ok ? info.stop_id : "unresolved", info.location_type);
    }
  }

  Serial.println("BOOT-OK live");
#endif
}

void loop() {
  static uint32_t frames = 0, draw_ms_total = 0, last_report = 0;
#ifdef DEMO_MODE
  static int last_scene = -1;

  const uint32_t start = millis();
  const int scene = static_cast<int>((start / DEMO_SCENE_MS) % demo_scene_count());
  if (scene != last_scene) {
    const Board b = demo_board(start);
    Serial.printf("scene %s  theme %u\n", demo_scene(scene).name, b.theme);
    last_scene = scene;
  }
#else
  const uint32_t start = millis();
#endif

  ui.draw(board_now(start, time(nullptr)), start);

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
