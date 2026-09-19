#include "config.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include "theme.h"
#include "watch_config.h"

static_assert(N_WATCHES <= MAX_WATCHES,
              "watch_config.h declares more watches than a Config can hold");

namespace {

constexpr char NVS_NS[] = "board";
constexpr char NVS_KEY[] = "cfg";

Config g_cfg;
WatchConfig g_pub[MAX_WATCHES];
uint8_t g_n_pub = 0;

// The one value written at runtime. A uint8_t store is atomic on this target,
// which is the whole reason the theme may change without a reboot.
volatile uint8_t g_theme = 0;

Preferences g_prefs;

void seed_from_compiled_defaults() {
  Config c{};
  strncpy(c.location, LOCATION, sizeof c.location - 1);
  c.theme = 0;
  c.n_watches = 0;
  for (int i = 0; i < N_WATCHES && i < MAX_WATCHES; i++) {
    CfgWatch& d = c.watches[c.n_watches];
    strncpy(d.label, WATCHES[i].label, sizeof d.label - 1);
    strncpy(d.stop_code, WATCHES[i].stop_code, sizeof d.stop_code - 1);
    strncpy(d.route_short_name, WATCHES[i].route_short_name, sizeof d.route_short_name - 1);
    strncpy(d.toward_stop_code, WATCHES[i].toward_stop_code, sizeof d.toward_stop_code - 1);
    d.enabled = true;
    c.n_watches++;
  }
  g_cfg = c;

  // Route the compiled defaults through the same validator the JSON path
  // uses. This cannot catch a field already truncated by strncpy above (that
  // information is gone by now), but it does catch an empty stop_code, zero
  // enabled watches, and a too-long location - the achievable part of
  // reject-don't-truncate for a path that itself only truncates.
  char json[CFG_JSON_CAP];
  Config scratch{};
  if (cfg_serialize(g_cfg, json, sizeof json) == 0) {
    Serial.println(
        "config: watch_config.h defaults failed to serialise - "
        "watch_config.h is likely misconfigured");
  } else {
    const CfgError e = cfg_parse(json, &scratch, theme_count());
    if (e != CfgError::Ok) {
      Serial.printf(
          "config: watch_config.h defaults fail validation (%s) - "
          "watch_config.h is at fault\n",
          cfg_error_text(e));
    }
  }
}

}  // namespace

void config_begin() {
  char json[CFG_JSON_CAP];
  json[0] = '\0';

  bool loaded = false;
  if (g_prefs.begin(NVS_NS, true)) {  // read-only
    g_prefs.getString(NVS_KEY, json, sizeof json);
    g_prefs.end();
    const CfgError e = cfg_parse(json, &g_cfg, theme_count());
    if (e == CfgError::Ok) {
      loaded = true;
    } else if (json[0] != '\0') {
      Serial.printf("config: stored config rejected (%s), using defaults\n",
                    cfg_error_text(e));
    }
  }
  if (!loaded) seed_from_compiled_defaults();

  g_theme = g_cfg.theme;
  g_n_pub = cfg_publish(g_cfg, g_pub);
  Serial.printf("config: %s, %u watches, theme %u (%s)\n", g_cfg.location,
                static_cast<unsigned>(g_n_pub), static_cast<unsigned>(g_theme),
                loaded ? "nvs" : "compiled defaults");
}

const WatchConfig* config_watches() { return g_pub; }
uint8_t config_n_watches() { return g_n_pub; }
const char* config_location() { return g_cfg.location; }
uint8_t config_theme() { return g_theme; }

void config_set_theme(uint8_t t) {
  if (t >= theme_count()) return;
  g_theme = t;   // live, atomic
  g_cfg.theme = t;
  char json[CFG_JSON_CAP];
  if (cfg_serialize(g_cfg, json, sizeof json) == 0) return;
  if (g_prefs.begin(NVS_NS, false)) {
    g_prefs.putString(NVS_KEY, json);
    g_prefs.end();
  }
}

CfgError config_save_json(const char* json) {
  Config scratch{};
  const CfgError e = cfg_parse(json, &scratch, theme_count());
  if (e != CfgError::Ok) return e;

  // Re-serialise rather than storing the caller's bytes: this normalises the
  // document and guarantees what lands in NVS is something cfg_parse accepts.
  char clean[CFG_JSON_CAP];
  if (cfg_serialize(scratch, clean, sizeof clean) == 0) return CfgError::FieldTooLong;

  if (!g_prefs.begin(NVS_NS, false)) return CfgError::BadJson;
  g_prefs.putString(NVS_KEY, clean);
  g_prefs.end();
  return CfgError::Ok;  // caller reboots; g_cfg deliberately untouched
}

size_t config_to_json(char* out, size_t cap) {
  Config snapshot = g_cfg;  // plain data, safe to copy
  snapshot.theme = g_theme;  // the live value, which may be ahead of g_cfg
  return cfg_serialize(snapshot, out, cap);
}
