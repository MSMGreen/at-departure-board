#pragma once
#include <stdint.h>

#include "live.h"

// How fresh the board may claim to be. Pure, so the rule that keeps stale data
// from looking live is tested on a laptop rather than trusted to the fetcher.
//
// The board computes stale_s = now - last_ok - poll_interval_s. Both of those
// are published by the fetcher, and both are decided here.

// Cadences (spec 5).
constexpr int32_t SCHEDULE_PERIOD_S = 15 * 60;
constexpr int32_t RT_FAST_S = 30;
constexpr int32_t RT_SLOW_S = 120;

struct Freshness {
  int64_t last_news;       // epoch of the last conclusive news, 0 = never
  bool failed_since_news;  // a request failed, or there was no link or clock
  int32_t interval;        // the interval last published, 0 = none yet
};

// Conclusive news arrived: a conclusive schedule refresh or a realtime 200.
void freshness_news(Freshness& f, int64_t now);

// A request failed (anything but 200 or 404), or there was no link or clock.
void freshness_failure(Freshness& f);

// The interval to publish, remembered in f. SCHEDULE_PERIOD_S only when
// nothing is upcoming AND nothing has failed since the last news; otherwise
// the nominal cadence, RT_FAST_S within 30 minutes of a departure and
// RT_SLOW_S beyond. Once anything has failed since the last news, the
// published interval never grows until news arrives: a growing interval would
// shrink stale_s, and an outage would un-stale the board.
int32_t freshness_interval(Freshness& f, bool anything_upcoming, bool within_30_min);

// The last_ok to publish. A realtime 200 is news for every watch at once, so
// on its own it would let one watch whose schedule keeps failing run out of
// rows under a green "live" while the other watch kept last_news fresh. So the
// board is only as fresh as its stalest schedule: last_ok is the earlier of
// last_news and, over the watches in state Ok, the oldest sched_ok_at (the
// epoch of that watch's last conclusive schedule refresh) plus
// SCHEDULE_PERIOD_S. sched_ok_at has one entry per watch in s.
int64_t board_last_ok(int64_t last_news, const Snapshot& s, const int64_t sched_ok_at[]);
