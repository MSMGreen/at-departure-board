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
#include <ArduinoJson.h>
#include <WebServer.h>
#include <ctype.h>
#include <stdarg.h>
#include <string.h>

#include "at_api.h"
#include "at_client.h"
#include "config.h"
#include "theme.h"

namespace {

WebServer g_server(80);

// Appends to json[0..cap) at *p via snprintf, refusing to let *p run past
// the buffer. snprintf returns the length it WOULD have written, not what
// fit, so an unchecked `p += snprintf(...)` can walk p past cap; the next
// call's `cap - p` then underflows to a huge size_t and `json + p` points
// out of bounds. Returns false (and leaves the buffer unusable) on a
// negative/encoding-error return or on truncation.
bool json_append(char* json, size_t cap, size_t* p, const char* fmt, ...) {
  if (*p >= cap) return false;
  va_list args;
  va_start(args, fmt);
  int n = vsnprintf(json + *p, cap - *p, fmt, args);
  va_end(args);
  if (n < 0 || static_cast<size_t>(n) >= cap - *p) return false;
  *p += static_cast<size_t>(n);
  return true;
}

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
  size_t p = 0;
  bool ok = json_append(json, sizeof json, &p, "{\"selected\":%u,\"themes\":[",
                        static_cast<unsigned>(config_theme()));
  for (uint8_t i = 0; ok && i < theme_count(); i++) {
    ok = json_append(json, sizeof json, &p, "%s\"%s\"", i ? "," : "", theme(i).name);
  }
  if (ok) ok = json_append(json, sizeof json, &p, "]}");

  if (!ok) {
    g_server.send(500, "application/json", "{\"error\":\"theme list too long\"}");
    return;
  }
  g_server.send(200, "application/json", json);
}

void handle_set_theme() {
  JsonDocument doc;
  if (deserializeJson(doc, g_server.arg("plain"))) {
    g_server.send(400, "application/json", "{\"error\":\"bad JSON\"}");
    return;
  }
  if (doc["theme"].isNull()) {
    g_server.send(400, "application/json", "{\"error\":\"theme is required\"}");
    return;
  }
  if (!doc["theme"].is<int>()) {
    g_server.send(400, "application/json", "{\"error\":\"theme must be an integer\"}");
    return;
  }
  const int v = doc["theme"].as<int>();
  if (v < 0 || v >= theme_count()) {
    g_server.send(400, "application/json", "{\"error\":\"no such theme\"}");
    return;
  }
  const uint8_t t = static_cast<uint8_t>(v);
  config_set_theme(t);
  char body[64];
  snprintf(body, sizeof body, "{\"theme\":%u}", static_cast<unsigned>(t));
  g_server.send(200, "application/json", body);
}

void handle_check_stop() {
  JsonDocument req;
  if (deserializeJson(req, g_server.arg("plain"))) {
    g_server.send(400, "application/json", "{\"error\":\"bad JSON\"}");
    return;
  }
  const char* code = req["stop_code"] | "";
  if (code[0] == '\0') {
    g_server.send(400, "application/json", "{\"error\":\"stop_code is required\"}");
    return;
  }
  // Reject anything that is not plain ASCII alphanumeric before it reaches
  // url_stop_by_code(), which only checks that the built URL fits and would
  // otherwise pass '&', '#', whitespace or CR/LF straight into a request
  // made with this device's API key. The length cap matches CFG_FIELD_CAP
  // so anything accepted here also fits a saved watch's stop_code later.
  const size_t code_len = strlen(code);
  if (code_len >= CFG_FIELD_CAP) {
    g_server.send(400, "application/json",
                  "{\"error\":\"stop code must be letters and digits\"}");
    return;
  }
  for (size_t i = 0; i < code_len; i++) {
    if (!isalnum(static_cast<unsigned char>(code[i]))) {
      g_server.send(400, "application/json",
                    "{\"error\":\"stop code must be letters and digits\"}");
      return;
    }
  }

  char url[256];
  if (!url_stop_by_code(url, sizeof url, code)) {
    g_server.send(400, "application/json", "{\"error\":\"stop code is not usable in a URL\"}");
    return;
  }

  JsonDocument filter;
  stop_filter(filter);
  JsonDocument doc;
  const int status = at_get(url, doc, filter);
  if (status != 200) {
    char body[96];
    snprintf(body, sizeof body, "{\"error\":\"AT returned %d\"}", status);
    g_server.send(502, "application/json", body);
    return;
  }

  StopInfo info{};
  const bool found = parse_stop(doc, &info);
  doc.clear();
  if (!found) {
    g_server.send(404, "application/json", "{\"error\":\"no stop with that code\"}");
    return;
  }

  JsonDocument out;
  out["stop_code"] = code;
  out["stop_name"] = info.stop_name;
  char body[192];
  // serializeJson() does NOT null-terminate on an exact fill or truncation
  // (it only writes the terminator when n < sizeof body); handing such a
  // buffer to g_server.send()'s const char* overload lets its strlen-driven
  // copy read past the array and ship whatever memory follows it. Fail
  // closed instead of trusting the buffer was terminated.
  const size_t n = serializeJson(out, body, sizeof body);
  if (n == 0 || n >= sizeof body) {
    g_server.send(500, "application/json", "{\"error\":\"response too large\"}");
    return;
  }
  g_server.send(200, "application/json", body);
}

void handle_not_found() { g_server.send(404, "text/plain", "not found"); }

void portal_task(void*) {
  g_server.on("/api/config", HTTP_GET, handle_config);
  g_server.on("/api/themes", HTTP_GET, handle_themes);
  g_server.on("/api/theme", HTTP_POST, handle_set_theme);
  g_server.on("/api/stop", HTTP_POST, handle_check_stop);
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
