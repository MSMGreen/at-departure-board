// THROWAWAY SPIKE - stage 2 of 2.
//
// The decisive question: do TLS + a streaming JSON parse + the 35 KB lane
// sprite coexist on the real chip? The paper budget was ~85 KB of ~300 KB.
//
// The sprite is allocated FIRST and held for the whole run, because that is
// the worst case the firmware will actually hit - the display is never torn
// down to make room for a fetch.
//
// Also validates NTP + Pacific/Auckland, since stoptrips needs today's service
// date and getting DST wrong makes the board an hour wrong in three weeks.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "secrets.h"

// NZ DST: starts last Sunday of September, ends first Sunday of April at 03:00.
static const char *TZ_AUCKLAND = "NZST-12NZDT,M9.5.0,M4.1.0/3";

static const size_t SPRITE_BYTES = 320 * 56 * 2;  // one lane, 16bpp
static uint8_t *g_sprite = nullptr;

static void heap(const char *when) {
  Serial.printf("%-30s free %6u  largest %6u  min-ever %6u\n", when,
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap(),
                (unsigned)ESP.getMinFreeHeap());
}

static void banner(const char *s) {
  Serial.println();
  Serial.print("=== ");
  Serial.print(s);
  Serial.println(" ===");
}

// Fetch `url`, stream-parse with `filter`, report heap while the parse is live.
static bool fetch(const char *label, const String &url, JsonDocument &doc,
                  const JsonDocument &filter) {
  WiFiClientSecure client;
  client.setInsecure();  // spike only; production pins the CA
  HTTPClient http;
  http.setReuse(false);

  if (!http.begin(client, url)) {
    Serial.printf("%s: begin() failed\n", label);
    return false;
  }
  http.addHeader("Ocp-Apim-Subscription-Key", AT_API_KEY);

  heap("  before TLS handshake");
  int code = http.GET();
  heap("  after TLS + headers");

  if (code <= 0) {
    Serial.printf("%s: GET failed, %s\n", label, http.errorToString(code).c_str());
    http.end();
    return false;
  }
  int len = http.getSize();
  Serial.printf("%s: HTTP %d, %d bytes declared\n", label, code, len);

  DeserializationError err = deserializeJson(
      doc, http.getStream(), DeserializationOption::Filter(filter));
  heap("  after streaming parse");

  http.end();
  heap("  after connection closed");

  if (err) {
    Serial.printf("%s: parse error: %s\n", label, err.c_str());
    return false;
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  banner("hold the sprite for the whole run");
  g_sprite = (uint8_t *)malloc(SPRITE_BYTES);
  Serial.printf("sprite %u bytes: %s\n", (unsigned)SPRITE_BYTES,
                g_sprite ? "ALLOCATED" : "FAILED");
  if (g_sprite) memset(g_sprite, 0xA5, SPRITE_BYTES);
  heap("with sprite held");

  banner("wifi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WIFI FAILED - check secrets.h");
    Serial.println("SPIKE-STAGE-2-COMPLETE");
    return;
  }
  Serial.printf("connected, ip %s, rssi %d\n", WiFi.localIP().toString().c_str(),
                (int)WiFi.RSSI());
  heap("after wifi up");

  banner("ntp + Pacific/Auckland");
  configTzTime(TZ_AUCKLAND, "pool.ntp.org", "time.nist.gov");
  struct tm tm {};
  if (!getLocalTime(&tm, 15000)) {
    Serial.println("NTP FAILED");
  } else {
    char buf[64];
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S %Z (dst=%%d)", &tm);
    Serial.printf("local time: ");
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S %Z", &tm);
    Serial.printf("%s  isdst=%d\n", buf, tm.tm_isdst);
  }
  heap("after ntp");

  char date[16];
  strftime(date, sizeof date, "%Y-%m-%d", &tm);

  // --- the big one: stoptrips at Kingsland station, ~11 KB response ---------
  banner("stoptrips (largest response we ever parse)");
  {
    JsonDocument filter;
    JsonObject f = filter["data"][0]["attributes"].to<JsonObject>();
    f["departure_time"] = true;
    f["trip_id"] = true;
    f["route_id"] = true;
    f["direction_id"] = true;
    f["trip_headsign"] = true;

    String url = String("https://api.at.govt.nz/gtfs/v3/stops/122-34ecc043"
                        "/stoptrips?filter%5Bdate%5D=") +
                 date + "&filter%5Bstart_hour%5D=" + String(tm.tm_hour) +
                 "&filter%5Bhour_range%5D=2";

    JsonDocument doc;
    if (fetch("stoptrips", url, doc, filter)) {
      JsonArray rows = doc["data"].as<JsonArray>();
      Serial.printf("parsed %u departures, doc uses %u bytes\n",
                    (unsigned)rows.size(), (unsigned)doc.memoryUsage());
      int shown = 0;
      for (JsonObject r : rows) {
        JsonObject a = r["attributes"];
        if (shown++ < 4) {
          Serial.printf("   %s  %s  dir %d  %s\n",
                        a["departure_time"] | "?", a["route_id"] | "?",
                        (int)(a["direction_id"] | -1), a["trip_headsign"] | "?");
        }
      }
    }
  }
  heap("after stoptrips released");

  // --- realtime ------------------------------------------------------------
  banner("realtime tripupdates");
  {
    JsonDocument filter;
    JsonObject e = filter["response"]["entity"][0].to<JsonObject>();
    e["trip_update"]["trip"]["trip_id"] = true;
    e["trip_update"]["delay"] = true;

    JsonDocument doc;
    String url = "https://api.at.govt.nz/realtime/legacy/tripupdates";
    if (fetch("realtime", url, doc, filter)) {
      JsonArray ents = doc["response"]["entity"].as<JsonArray>();
      Serial.printf("parsed %u entities, doc uses %u bytes\n",
                    (unsigned)ents.size(), (unsigned)doc.memoryUsage());
      Serial.println("(unfiltered feed - this is the WORST case, the firmware");
      Serial.println(" always passes ?tripid= and gets a fraction of this)");
    }
  }
  heap("after realtime released");

  banner("verdict");
  Serial.printf("sprite still held: %s\n", g_sprite ? "yes" : "no");
  Serial.printf("lowest heap seen at any point: %u bytes\n",
                (unsigned)ESP.getMinFreeHeap());
  Serial.printf("free now: %u, largest block: %u\n", (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getMaxAllocHeap());
  Serial.println();
  Serial.println("SPIKE-STAGE-2-COMPLETE");
}

void loop() { delay(1000); }
