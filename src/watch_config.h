#pragma once
#include "live.h"

// The watches, until the setup portal lands (spec 3). Stop codes are the
// numbers on the pole; nothing here is secret.
//
// route_short_name "" means any route, which is what carries a rail watch
// through a line rename - see docs/at-api-notes.md on the CRL changeover.
// toward_stop_code is the stop you are travelling toward, never a direction:
// direction is derived at every refresh (spec 3a).
static const char* const LOCATION = "Kingsland";

static const WatchConfig WATCHES[] = {
    {"to Wynyard Quarter", "8213", "20", "1060"},
    {"to Waitemata", "122", "", "133"},  // no macron: the panel font is ASCII
};
static constexpr int N_WATCHES = sizeof WATCHES / sizeof WATCHES[0];
