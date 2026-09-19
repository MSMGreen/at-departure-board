#include "config_schema.h"

#include <ArduinoJson.h>
#include <string.h>

namespace {

// false when src will not fit dst (cap includes the NUL).
bool copy_field(char* dst, size_t cap, const char* src) {
  if (src == nullptr) src = "";
  if (strlen(src) >= cap) return false;
  strcpy(dst, src);
  return true;
}

}  // namespace

const char* cfg_error_text(CfgError e) {
  switch (e) {
    case CfgError::Ok: return "ok";
    case CfgError::BadJson: return "could not parse the config as JSON";
    case CfgError::BadVersion: return "config version is not supported";
    case CfgError::TooManyWatches: return "too many watches (maximum four)";
    case CfgError::NoWatches: return "at least one enabled watch is required";
    case CfgError::MissingStopCode: return "every watch needs a stop code";
    case CfgError::FieldTooLong: return "a watch field is too long";
    case CfgError::LocationTooLong: return "the location name is too long";
  }
  return "unknown error";
}

CfgError cfg_parse(const char* json, Config* out, uint8_t theme_max) {
  if (json == nullptr || json[0] == '\0') return CfgError::BadJson;

  JsonDocument doc;
  if (deserializeJson(doc, json)) return CfgError::BadJson;
  if (doc["v"].isNull() || doc["v"].as<uint8_t>() != CFG_SCHEMA_VERSION) {
    return CfgError::BadVersion;
  }

  Config c{};
  if (!copy_field(c.location, sizeof c.location, doc["location"] | "")) {
    return CfgError::LocationTooLong;
  }

  const uint8_t t = doc["theme"] | 0;
  c.theme = (theme_max == 0 || t < theme_max) ? t : static_cast<uint8_t>(theme_max - 1);

  JsonArrayConst ws = doc["watches"].as<JsonArrayConst>();
  if (ws.size() > MAX_WATCHES) return CfgError::TooManyWatches;

  uint8_t n_enabled = 0;
  for (JsonObjectConst w : ws) {
    CfgWatch& d = c.watches[c.n_watches];
    if (!copy_field(d.label, sizeof d.label, w["label"] | "") ||
        !copy_field(d.stop_code, sizeof d.stop_code, w["stop_code"] | "") ||
        !copy_field(d.route_short_name, sizeof d.route_short_name,
                    w["route_short_name"] | "") ||
        !copy_field(d.toward_stop_code, sizeof d.toward_stop_code,
                    w["toward_stop_code"] | "")) {
      return CfgError::FieldTooLong;
    }
    if (d.stop_code[0] == '\0') return CfgError::MissingStopCode;
    d.enabled = w["enabled"] | true;
    if (d.enabled) n_enabled++;
    c.n_watches++;
  }

  if (n_enabled == 0) return CfgError::NoWatches;

  *out = c;
  return CfgError::Ok;
}
