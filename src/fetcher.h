#pragma once
#include "live.h"

// Owns every network call. Runs as a FreeRTOS task on core 0 so that nothing
// it does can stall the 15 fps render loop on core 1 (a TLS handshake alone
// takes longer than a frame). Publishes a snapshot the loop copies.
void fetcher_begin();
void fetcher_snapshot(Snapshot* out);
