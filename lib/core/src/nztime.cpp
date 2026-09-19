#include "nztime.h"

#include <stdio.h>
#include <string.h>

namespace {

constexpr int64_t DAY = 86400;
constexpr int NZST = 12 * 3600;
constexpr int NZDT = 13 * 3600;

// 0 = Sunday.
int weekday_from_days(int64_t z) { return static_cast<int>((z + 4) % 7 + 7) % 7; }

// Day-of-month of the last `weekday` in a month.
int last_weekday_of(int y, int m, int weekday) {
  static const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int last = mdays[m - 1];
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) last = 29;
  const int wd = weekday_from_days(days_from_civil(y, m, last));
  return last - ((wd - weekday + 7) % 7);
}

int first_weekday_of(int y, int m, int weekday) {
  const int wd = weekday_from_days(days_from_civil(y, m, 1));
  return 1 + ((weekday - wd + 7) % 7);
}

// DST starts 02:00 NZST on the last Sunday in September.
int64_t dst_start(int y) {
  return days_from_civil(y, 9, last_weekday_of(y, 9, 0)) * DAY + 2 * 3600 - NZST;
}

// DST ends 03:00 NZDT on the first Sunday in April.
int64_t dst_end(int y) {
  return days_from_civil(y, 4, first_weekday_of(y, 4, 0)) * DAY + 3 * 3600 - NZDT;
}

}  // namespace

int64_t days_from_civil(int y, int m, int d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

CivilDate civil_from_days(int64_t z) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int64_t y = static_cast<int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  const unsigned d = doy - (153 * mp + 2) / 5 + 1;
  const unsigned m = mp + (mp < 10 ? 3 : -9);
  return {static_cast<int>(y + (m <= 2)), static_cast<int>(m), static_cast<int>(d)};
}

int nz_offset_s(int64_t epoch) {
  const CivilDate utc = civil_from_days(epoch >= 0 ? epoch / DAY : (epoch - DAY + 1) / DAY);
  return (epoch >= dst_start(utc.y) || epoch < dst_end(utc.y)) ? NZDT : NZST;
}

LocalTime nz_local(int64_t epoch) {
  const int offset = nz_offset_s(epoch);
  const int64_t local = epoch + offset;
  int64_t days = local / DAY;
  int64_t rem = local % DAY;
  if (rem < 0) {
    rem += DAY;
    days -= 1;
  }
  const CivilDate c = civil_from_days(days);
  LocalTime t{};
  t.y = c.y;
  t.m = c.m;
  t.d = c.d;
  t.hour = static_cast<int>(rem / 3600);
  t.min = static_cast<int>((rem % 3600) / 60);
  t.sec = static_cast<int>(rem % 60);
  t.offset_s = offset;
  return t;
}

int64_t nz_epoch(int y, int m, int d, int32_t secs_of_day) {
  const int64_t wall = days_from_civil(y, m, d) * DAY + secs_of_day;
  const int64_t as_dst = wall - NZDT;
  if (nz_offset_s(as_dst) == NZDT) return as_dst;  // also picks the first of an
                                                   // ambiguous autumn pair
  const int64_t as_std = wall - NZST;
  if (nz_offset_s(as_std) == NZST) return as_std;
  return as_std;  // spring gap: the wall time never happens
}

bool parse_gtfs_time(const char* s, int32_t* secs) {
  if (s == nullptr || strlen(s) != 8 || s[2] != ':' || s[5] != ':') return false;
  int v[3] = {0, 0, 0};
  for (int part = 0; part < 3; part++) {
    const char* p = s + part * 3;
    if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return false;
    v[part] = (p[0] - '0') * 10 + (p[1] - '0');
  }
  if (v[0] > 47 || v[1] > 59 || v[2] > 59) return false;
  *secs = v[0] * 3600 + v[1] * 60 + v[2];
  return true;
}

bool parse_iso_date(const char* s, CivilDate* out) {
  if (s == nullptr || strlen(s) != 10 || s[4] != '-' || s[7] != '-') return false;
  int y = 0;
  for (int i = 0; i < 4; i++) {
    if (s[i] < '0' || s[i] > '9') return false;
    y = y * 10 + (s[i] - '0');
  }
  int part[2] = {0, 0};
  for (int k = 0; k < 2; k++) {
    const char* p = s + 5 + k * 3;
    if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return false;
    part[k] = (p[0] - '0') * 10 + (p[1] - '0');
  }
  if (part[0] < 1 || part[0] > 12 || part[1] < 1 || part[1] > 31) return false;
  *out = {y, part[0], part[1]};
  return true;
}

int64_t gtfs_epoch(CivilDate service_date, int32_t secs) {
  const int64_t noon = nz_epoch(service_date.y, service_date.m, service_date.d, 12 * 3600);
  return noon - 12 * 3600 + secs;
}

void format_clock(int64_t epoch, char* out, size_t n) {
  if (epoch < CLOCK_SET_AFTER) {
    snprintf(out, n, "--:--");  // no NTP yet: never a confident wrong time
    return;
  }
  const LocalTime t = nz_local(epoch);
  snprintf(out, n, "%02d:%02d", t.hour, t.min);
}

void format_iso_date(CivilDate d, char* out, size_t n) {
  snprintf(out, n, "%04d-%02d-%02d", d.y, d.m, d.d);
}
