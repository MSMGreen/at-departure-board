#pragma once
#include <stddef.h>
#include <stdint.h>

// Pacific/Auckland without the C library. Windows' UCRT mis-handles the POSIX
// TZ string (it placed 2026-09-28 12:00 NZDT at 11:00 UTC), so the rules live
// here and behave identically on the device and in the native tests.
//
// NZDT (UTC+13) runs from 02:00 NZST on the last Sunday in September to
// 03:00 NZDT on the first Sunday in April. NZST (UTC+12) otherwise.

struct CivilDate {
  int y, m, d;
};

struct LocalTime {
  int y, m, d, hour, min, sec;
  int offset_s;  // 43200 (NZST) or 46800 (NZDT)
};

// Days since 1970-01-01 (Howard Hinnant's civil algorithms).
int64_t days_from_civil(int y, int m, int d);
CivilDate civil_from_days(int64_t z);

int nz_offset_s(int64_t epoch);
LocalTime nz_local(int64_t epoch);

// Wall-clock to epoch. A time in the spring gap resolves with the offset in
// force before the jump; an ambiguous autumn time takes the first occurrence.
int64_t nz_epoch(int y, int m, int d, int32_t secs_of_day);

// "HH:MM:SS", hours 0..47 - GTFS uses 25:10:00 for 01:10 the next day.
bool parse_gtfs_time(const char* s, int32_t* secs);
bool parse_iso_date(const char* s, CivilDate* out);

// GTFS measures a departure as elapsed seconds from noon minus 12 hours of the
// service day, in absolute time, so noon stays noon when the clocks change.
int64_t gtfs_epoch(CivilDate service_date, int32_t secs);

// Any epoch before this is a clock NTP has not set yet (2023-11-15).
constexpr int64_t CLOCK_SET_AFTER = 1700000000;

void format_clock(int64_t epoch, char* out, size_t n);      // "17:42", or "--:--"
                                                           // before CLOCK_SET_AFTER
void format_iso_date(CivilDate d, char* out, size_t n);     // "2026-09-19"
