#include "freshness.h"

void freshness_news(Freshness& f, int64_t now) {
  f.last_news = now;
  f.failed_since_news = false;
}

void freshness_failure(Freshness& f) { f.failed_since_news = true; }

int32_t freshness_interval(Freshness& f, bool anything_upcoming, bool within_30_min) {
  // A quiet night: the next news is the schedule refresh, so a healthy board
  // must not read stale for most of every 15 minutes. Only when healthy.
  int32_t interval = within_30_min ? RT_FAST_S : RT_SLOW_S;
  if (!anything_upcoming && !f.failed_since_news) interval = SCHEDULE_PERIOD_S;

  // Once something has failed, the interval may shrink but never grow until
  // news arrives: growing it would take seconds off stale_s, and an outage
  // spanning the last departure would make a stale board look fresh again.
  if (f.failed_since_news && f.interval > 0 && interval > f.interval) interval = f.interval;
  f.interval = interval;
  return interval;
}

int64_t board_last_ok(int64_t last_news, const Snapshot& s, const int64_t sched_ok_at[]) {
  int64_t out = last_news;
  for (int i = 0; i < s.n_watches; i++) {
    if (s.watches[i].state != WatchState::Ok) continue;
    const int64_t due = sched_ok_at[i] + SCHEDULE_PERIOD_S;
    if (due < out) out = due;
  }
  return out;
}
