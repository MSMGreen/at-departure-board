// THROWAWAY SPIKE - stage 4 (stage 3 + conditional de-chunking).
//
// Stage 2 found that AT sends Transfer-Encoding: chunked, and feeding
// http.getStream() straight to ArduinoJson makes it read the chunk-size line
// ("2561\r\n") as a JSON number, succeed, and return an empty document. No
// error. Zero departures, forever.
//
// This stage proves a de-chunking Stream fixes it, and measures realtime the
// way the firmware will actually call it - with ?tripid= - rather than pulling
// the whole 750 KB Auckland feed.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "secrets.h"

static const char *TZ_AUCKLAND = "NZST-12NZDT,M9.5.0,M4.1.0/3";
static const size_t SPRITE_BYTES = 320 * 56 * 2;
static uint8_t *g_sprite = nullptr;

static void heap(const char *when) {
  Serial.printf("%-30s free %6u  largest %6u  min-ever %6u\n", when,
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
                (unsigned)ESP.getMinFreeHeap());
}

static void banner(const char *s) {
  Serial.printf("\n=== %s ===\n", s);
}

// --- the fix -----------------------------------------------------------------
// Unwraps HTTP chunked transfer-encoding so a JSON parser sees only the body.
// Memory stays flat: one chunk header at a time, never the whole response.
class ChunkedStream : public Stream {
 public:
  explicit ChunkedStream(Stream &inner) : _in(inner) {}

  int available() override { return _done ? 0 : 1; }
  void flush() override {}
  size_t write(uint8_t) override { return 0; }

  int read() override {
    if (!ensure()) return -1;
    int c = _in.read();
    if (c < 0) return -1;
    if (--_left == 0) skipCrlf();
    return c;
  }

  int peek() override {
    if (!ensure()) return -1;
    return _in.peek();
  }

 private:
  // Block until a byte is available, or give up.
  int blockingRead() {
    uint32_t t0 = millis();
    while (!_in.available()) {
      if (millis() - t0 > 5000) return -1;
      delay(1);
    }
    return _in.read();
  }

  void skipCrlf() {
    blockingRead();  // \r
    blockingRead();  // \n
  }

  // Make sure _left > 0, reading the next chunk header if needed.
  bool ensure() {
    if (_done) return false;
    if (_left > 0) return true;

    char line[16];
    size_t n = 0;
    for (;;) {
      int c = blockingRead();
      if (c < 0) { _done = true; return false; }
      if (c == '\n') break;
      if (c != '\r' && n < sizeof(line) - 1) line[n++] = (char)c;
    }
    line[n] = 0;
    if (n == 0) return ensure();  // stray blank line between chunks

    long size = strtol(line, nullptr, 16);  // chunk size is HEX
    if (size <= 0) { _done = true; return false; }
    _left = (size_t)size;
    return true;
  }

  Stream &_in;
  size_t _left = 0;
  bool _done = false;
};

static bool fetch(const char *label, const String &url, JsonDocument &doc,
                  const JsonDocument &filter, bool dechunk) {
  WiFiClientSecure client;
  client.setInsecure();  // spike only; production pins the CA
  HTTPClient http;
  http.setReuse(false);
  if (!http.begin(client, url)) { Serial.printf("%s: begin failed\n", label); return false; }
  http.addHeader("Ocp-Apim-Subscription-Key", AT_API_KEY);

  int code = http.GET();
  heap("  after TLS + headers");
  if (code <= 0) {
    Serial.printf("%s: GET failed %s\n", label, http.errorToString(code).c_str());
    http.end();
    return false;
  }
  Serial.printf("%s: HTTP %d, declared %d\n", label, code, http.getSize());

  // Branch on what the server actually sent, never on what we expected.
  // AT's GTFS API is chunked (getSize() == -1); the realtime API sends a
  // Content-Length. De-chunking a non-chunked body reads the JSON's first
  // bytes as a hex chunk header and yields EmptyInput.
  const bool chunked = (http.getSize() < 0);
  Serial.printf("  transfer: %s\n",
                chunked ? "chunked -> de-chunking" : "content-length -> direct");
  DeserializationError err;
  if (chunked && dechunk) {
    ChunkedStream cs(http.getStream());
    err = deserializeJson(doc, cs, DeserializationOption::Filter(filter));
  } else {
    err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  }
  heap("  after streaming parse");
  http.end();
  if (err) { Serial.printf("%s: parse error %s\n", label, err.c_str()); return false; }
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  g_sprite = (uint8_t *)malloc(SPRITE_BYTES);
  if (g_sprite) memset(g_sprite, 0xA5, SPRITE_BYTES);
  banner("sprite held for the whole run");
  heap("with sprite held");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) { delay(250); }
  if (WiFi.status() != WL_CONNECTED) { Serial.println("WIFI FAILED"); Serial.println("SPIKE-STAGE-4-COMPLETE"); return; }
  configTzTime(TZ_AUCKLAND, "pool.ntp.org");
  struct tm tm {};
  getLocalTime(&tm, 15000);
  char date[16];
  strftime(date, sizeof date, "%Y-%m-%d", &tm);
  Serial.printf("wifi up, %s %02d:%02d isdst=%d\n", date, tm.tm_hour, tm.tm_min, tm.tm_isdst);
  heap("after wifi + ntp");

  String stopUrl = String("https://api.at.govt.nz/gtfs/v3/stops/122-34ecc043"
                          "/stoptrips?filter%5Bdate%5D=") + date +
                   "&filter%5Bstart_hour%5D=" + String(tm.tm_hour) +
                   "&filter%5Bhour_range%5D=2";

  JsonDocument stopFilter;
  {
    JsonObject f = stopFilter["data"][0]["attributes"].to<JsonObject>();
    f["departure_time"] = true;
    f["trip_id"] = true;
    f["route_id"] = true;
    f["direction_id"] = true;
  }

  // --- A: the bug, reproduced ------------------------------------------------
  banner("A. stoptrips WITHOUT de-chunking (the stage 2 bug)");
  {
    JsonDocument doc;
    bool ok = fetch("raw", stopUrl, doc, stopFilter, false);
    JsonArray rows = doc["data"].as<JsonArray>();
    Serial.printf("deserializeJson reported: %s\n", ok ? "SUCCESS" : "error");
    Serial.printf("departures parsed: %u   <-- silently wrong\n", (unsigned)rows.size());
  }

  // --- B: the fix ------------------------------------------------------------
  banner("B. stoptrips WITH de-chunking");
  String tripIds;
  int count = 0;
  {
    JsonDocument doc;
    bool ok = fetch("dechunked", stopUrl, doc, stopFilter, true);
    JsonArray rows = doc["data"].as<JsonArray>();
    count = rows.size();
    Serial.printf("deserializeJson reported: %s\n", ok ? "SUCCESS" : "error");
    Serial.printf("departures parsed: %u\n", (unsigned)count);
    int shown = 0;
    for (JsonObject r : rows) {
      JsonObject a = r["attributes"];
      if (shown < 3) {
        Serial.printf("   %s  %s  dir %d\n", a["departure_time"] | "?",
                      a["route_id"] | "?", (int)(a["direction_id"] | -1));
      }
      if (shown < 4) {
        if (tripIds.length()) tripIds += ",";
        tripIds += (const char *)(a["trip_id"] | "");
      }
      shown++;
    }
  }
  heap("after stoptrips released");

  // --- C: realtime as the firmware will really call it -----------------------
  banner("C. realtime WITH ?tripid= (how the firmware calls it)");
  if (tripIds.length()) {
    JsonDocument filter;
    JsonObject e = filter["response"]["entity"][0].to<JsonObject>();
    e["trip_update"]["trip"]["trip_id"] = true;
    e["trip_update"]["delay"] = true;

    JsonDocument doc;
    String url = "https://api.at.govt.nz/realtime/legacy/tripupdates?tripid=" + tripIds;
    Serial.printf("asking for %d trips\n", 4);
    if (fetch("realtime", url, doc, filter, true)) {
      JsonArray ents = doc["response"]["entity"].as<JsonArray>();
      Serial.printf("entities: %u\n", (unsigned)ents.size());
      for (JsonObject en : ents) {
        JsonObject tu = en["trip_update"];
        Serial.printf("   %s delay %d\n", tu["trip"]["trip_id"] | "?",
                      (int)(tu["delay"] | 0));
      }
    }
  } else {
    Serial.println("no trip ids to ask about");
  }
  heap("after realtime released");

  banner("verdict");
  Serial.printf("sprite still held: %s\n", g_sprite ? "yes" : "no");
  Serial.printf("lowest heap across the whole run: %u\n", (unsigned)ESP.getMinFreeHeap());
  Serial.printf("free now %u, largest block %u\n", (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());
  Serial.println("\nSPIKE-STAGE-4-COMPLETE");
}

void loop() { delay(1000); }
