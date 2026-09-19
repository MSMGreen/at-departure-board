#pragma once
#include "config_schema.h"

// The live configuration: NVS-backed, seeded from src/watch_config.h when NVS
// holds nothing valid. Spec section 6 - nothing but the theme is written at
// runtime, because the fetch task reads the watches on core 0 with no lock.

void config_begin();  // call once in setup(), before fetcher_begin()

const WatchConfig* config_watches();
uint8_t config_n_watches();
const char* config_location();

uint8_t config_theme();
void config_set_theme(uint8_t t);  // applies live AND persists

// Validates, then persists to NVS ONLY. The in-RAM config is deliberately not
// updated: the caller reboots so config_begin() picks the new values up at the
// one moment nothing else is reading them.
CfgError config_save_json(const char* json);

size_t config_to_json(char* out, size_t cap);
