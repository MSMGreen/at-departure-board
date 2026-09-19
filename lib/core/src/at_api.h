#pragma once
#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#include "color.h"
#include "nztime.h"

// URLs, response filters and parsers for the two AT APIs. Pure: no networking
// here, so every shape below is covered by a fixture in test/fixtures/.
// docs/at-api-notes.md records why each of these looks the way it does.

constexpr int TRIP_ID_LEN = 48;
constexpr int STOP_ID_LEN = 24;
constexpr int ROUTE_ID_LEN = 16;
constexpr int SHORT_NAME_LEN = 12;
constexpr int STOP_CODE_LEN = 12;

struct StopInfo {
  char stop_id[STOP_ID_LEN];
  int location_type;  // 0 stop, 1 station
};

struct RouteInfo {
  char route_id[ROUTE_ID_LEN];
  char short_name[SHORT_NAME_LEN];
  int route_type;  // 2 rail, 3 bus
  bool has_color;
  Rgb color;
};

struct StopTripRow {
  char trip_id[TRIP_ID_LEN];
  char route_id[ROUTE_ID_LEN];
  char stop_id[STOP_ID_LEN];  // the platform for a station's rows
  int8_t direction_id;
  CivilDate service_date;
  int32_t departure_s;  // seconds from the service day's noon minus 12h
};

struct TripStop {
  char stop_id[STOP_ID_LEN];
  char stop_code[STOP_CODE_LEN];
  char parent_station[STOP_ID_LEN];  // "" for stops with no parent
};

struct RtEntity {
  char trip_id[TRIP_ID_LEN];
  bool has_delay;
  int32_t delay;  // trip level, signed; negative is early
  bool cancelled;
  bool has_stu_departure;
  char stu_stop_id[STOP_ID_LEN];
  int32_t stu_departure_delay;
};

// All builders return false rather than emit a truncated URL.
bool url_stop_by_code(char* out, size_t n, const char* stop_code);
bool url_routes_by_short_name(char* out, size_t n, const char* short_name);
bool url_rail_routes(char* out, size_t n);
// start_hour must be 1..23: the API answers 400 for 0 (verified 2026-09-19).
bool url_stoptrips(char* out, size_t n, const char* stop_id, CivilDate date,
                   int start_hour, int hour_range);
bool url_trip_stops(char* out, size_t n, const char* trip_id);
bool url_realtime(char* out, size_t n, const char* const trip_ids[], int count);

void stop_filter(JsonDocument& f);
void routes_filter(JsonDocument& f);
void stoptrips_filter(JsonDocument& f);
void trip_stops_filter(JsonDocument& f);
void realtime_filter(JsonDocument& f);

bool parse_stop(const JsonDocument& doc, StopInfo* out);  // false when data is empty
int parse_routes(const JsonDocument& doc, RouteInfo out[], int cap);
int parse_stoptrips(const JsonDocument& doc, StopTripRow out[], int cap);
int parse_trip_stops(const JsonDocument& doc, TripStop out[], int cap);
int parse_realtime(const JsonDocument& doc, RtEntity out[], int cap);
