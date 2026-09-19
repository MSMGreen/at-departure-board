#pragma once
#include <stddef.h>
#include <stdint.h>

#include "live.h"   // WatchConfig
#include "model.h"  // MAX_WATCHES

// Pure config logic: no Arduino headers, no NVS, no heap. Everything here is
// exercised by test/test_config_schema under `pio test -e native`.

constexpr int CFG_LOCATION_CAP = 24;  // matches Board::location
constexpr int CFG_FIELD_CAP = 32;
constexpr int CFG_JSON_CAP = 1024;  // four watches serialise to ~620 bytes
constexpr uint8_t CFG_SCHEMA_VERSION = 1;

struct CfgWatch {
  char label[CFG_FIELD_CAP];
  char stop_code[CFG_FIELD_CAP];
  char route_short_name[CFG_FIELD_CAP];  // "" means any route (spec 3)
  char toward_stop_code[CFG_FIELD_CAP];  // "" means no direction filter
  bool enabled;
};

// Plain data - no pointers into itself, so this is safe to copy.
struct Config {
  char location[CFG_LOCATION_CAP];
  uint8_t theme;
  uint8_t n_watches;  // every watch, enabled or not
  CfgWatch watches[MAX_WATCHES];
};

enum class CfgError : uint8_t {
  Ok,
  BadJson,          // would not deserialise
  BadVersion,       // "v" missing or not CFG_SCHEMA_VERSION
  TooManyWatches,   // more than MAX_WATCHES
  NoWatches,        // zero watches, or zero enabled ones
  MissingStopCode,  // a watch with an empty stop_code
  FieldTooLong,     // a string that will not fit its buffer
  LocationTooLong,
};

const char* cfg_error_text(CfgError e);

// Fills *out only on CfgError::Ok; *out is untouched otherwise, so a caller
// can keep its previous config on a failed save.
// theme is clamped to [0, theme_max), never rejected - a theme can disappear
// when the generated table changes and that must not brick the config.
CfgError cfg_parse(const char* json, Config* out, uint8_t theme_max);
