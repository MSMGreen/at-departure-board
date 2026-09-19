#include "portal.h"

// The whole implementation is live-only. DEMO_MODE never calls portal_begin()
// (see main.cpp), but that alone is not enough: WebServer below is a
// non-trivial global with a constructor that runs unconditionally at boot,
// so leaving it unguarded would pull the network stack into the demo binary
// even though nothing in it is ever reached. Compare fetcher.cpp, which gets
// away without this guard only because its globals are plain data with no
// constructor to run.
#ifndef DEMO_MODE

#include <Arduino.h>
#include <WebServer.h>

#include "config.h"
#include "theme.h"

namespace {

WebServer g_server(80);

void handle_config() {
  char json[CFG_JSON_CAP];
  if (config_to_json(json, sizeof json) == 0) {
    g_server.send(500, "application/json", "{\"error\":\"could not serialise config\"}");
    return;
  }
  g_server.send(200, "application/json", json);
}

void handle_themes() {
  char json[256];
  size_t p = snprintf(json, sizeof json, "{\"selected\":%u,\"themes\":[",
                      static_cast<unsigned>(config_theme()));
  for (uint8_t i = 0; i < theme_count() && p < sizeof(json) - 2; i++) {
    p += snprintf(json + p, sizeof(json) - p, "%s\"%s\"", i ? "," : "", theme(i).name);
  }
  snprintf(json + p, sizeof(json) - p, "]}");
  g_server.send(200, "application/json", json);
}

void handle_not_found() { g_server.send(404, "text/plain", "not found"); }

void portal_task(void*) {
  g_server.on("/api/config", HTTP_GET, handle_config);
  g_server.on("/api/themes", HTTP_GET, handle_themes);
  g_server.onNotFound(handle_not_found);
  g_server.begin();
  Serial.println("portal: listening on :80");

  for (;;) {
    g_server.handleClient();

    static uint32_t last_hw = 0;
    const uint32_t now_ms = millis();
    if (now_ms - last_hw >= 30000) {
      last_hw = now_ms;
      Serial.printf("portal: stack free %u\n",
                    static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    }

    vTaskDelay(pdMS_TO_TICKS(2));  // yields to the fetch task on this core
  }
}

}  // namespace

void portal_begin() {
  // Core 0 alongside the fetcher (fetcher.cpp), leaving core 1 for drawing.
  // 16384, not the 8192 an earlier draft used: Task 8 performs a full TLS
  // handshake from this task, the same work fetcher.cpp:718 sizes 16384 for.
  xTaskCreatePinnedToCore(portal_task, "portal", 16384, nullptr, 1, nullptr, 0);
}

#endif  // DEMO_MODE
