#include "at_api.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace {

constexpr const char* GTFS = "https://api.at.govt.nz/gtfs/v3";
constexpr const char* REALTIME = "https://api.at.govt.nz/realtime/legacy";

bool put(char* out, size_t n, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

bool put(char* out, size_t n, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  const int written = vsnprintf(out, n, fmt, args);
  va_end(args);
  return written > 0 && static_cast<size_t>(written) < n;
}

void copy(char* dst, size_t size, const char* src) {
  if (src == nullptr) src = "";
  strncpy(dst, src, size - 1);
  dst[size - 1] = '\0';
}

}  // namespace

bool url_stop_by_code(char* out, size_t n, const char* stop_code) {
  return put(out, n, "%s/stops?filter%%5Bstop_code%%5D=%s", GTFS, stop_code);
}

bool url_routes_by_short_name(char* out, size_t n, const char* short_name) {
  return put(out, n, "%s/routes?filter%%5Broute_short_name%%5D=%s", GTFS, short_name);
}

bool url_rail_routes(char* out, size_t n) {
  return put(out, n, "%s/routes?filter%%5Broute_type%%5D=2", GTFS);
}

bool url_stoptrips(char* out, size_t n, const char* stop_id, CivilDate date,
                   int start_hour, int hour_range) {
  if (start_hour < 1 || start_hour > 23 || hour_range < 1) return false;
  char day[16];
  format_iso_date(date, day, sizeof day);
  return put(out, n,
             "%s/stops/%s/stoptrips?filter%%5Bdate%%5D=%s"
             "&filter%%5Bstart_hour%%5D=%d&filter%%5Bhour_range%%5D=%d",
             GTFS, stop_id, day, start_hour, hour_range);
}

bool url_trip_stops(char* out, size_t n, const char* trip_id) {
  return put(out, n, "%s/trips/%s/stops", GTFS, trip_id);
}

bool url_realtime(char* out, size_t n, const char* const trip_ids[], int count) {
  if (count <= 0) return false;
  int written = snprintf(out, n, "%s/tripupdates?tripid=", REALTIME);
  if (written <= 0 || static_cast<size_t>(written) >= n) return false;
  size_t used = static_cast<size_t>(written);
  for (int i = 0; i < count; i++) {
    const int more = snprintf(out + used, n - used, "%s%s", i == 0 ? "" : ",", trip_ids[i]);
    if (more <= 0 || used + static_cast<size_t>(more) >= n) return false;
    used += static_cast<size_t>(more);
  }
  return true;
}

void stop_filter(JsonDocument& f) {
  JsonObject a = f["data"][0]["attributes"].to<JsonObject>();
  a["stop_id"] = true;
  a["location_type"] = true;
}

void routes_filter(JsonDocument& f) {
  JsonObject a = f["data"][0]["attributes"].to<JsonObject>();
  a["route_id"] = true;
  a["route_short_name"] = true;
  a["route_type"] = true;
  a["route_color"] = true;
}

void stoptrips_filter(JsonDocument& f) {
  JsonObject a = f["data"][0]["attributes"].to<JsonObject>();
  a["trip_id"] = true;
  a["route_id"] = true;
  a["stop_id"] = true;
  a["direction_id"] = true;
  a["departure_time"] = true;
  a["service_date"] = true;
}

void trip_stops_filter(JsonDocument& f) {
  JsonObject a = f["data"][0]["attributes"].to<JsonObject>();
  a["stop_id"] = true;
  a["stop_code"] = true;
  a["parent_station"] = true;
}

void realtime_filter(JsonDocument& f) {
  JsonObject e = f["response"]["entity"][0].to<JsonObject>();
  JsonObject tu = e["trip_update"].to<JsonObject>();
  tu["trip"]["trip_id"] = true;
  tu["trip"]["schedule_relationship"] = true;
  tu["delay"] = true;
  // Kept whole: the API sends a single object where the docs promise an array,
  // so a shaped filter would drop one of the two forms.
  tu["stop_time_update"] = true;
}

bool parse_stop(const JsonDocument& doc, StopInfo* out) {
  JsonArrayConst rows = doc["data"].as<JsonArrayConst>();
  if (rows.isNull() || rows.size() == 0) return false;
  JsonObjectConst a = rows[0]["attributes"];
  copy(out->stop_id, sizeof out->stop_id, a["stop_id"] | "");
  out->location_type = a["location_type"] | 0;
  return out->stop_id[0] != '\0';
}

int parse_routes(const JsonDocument& doc, RouteInfo out[], int cap) {
  int n = 0;
  for (JsonObjectConst row : doc["data"].as<JsonArrayConst>()) {
    if (n >= cap) break;
    JsonObjectConst a = row["attributes"];
    RouteInfo& r = out[n];
    copy(r.route_id, sizeof r.route_id, a["route_id"] | "");
    copy(r.short_name, sizeof r.short_name, a["route_short_name"] | "");
    r.route_type = a["route_type"] | 0;
    r.has_color = parse_hex(a["route_color"] | static_cast<const char*>(nullptr), &r.color);
    if (r.route_id[0] != '\0') n++;
  }
  return n;
}

int parse_stoptrips(const JsonDocument& doc, StopTripRow out[], int cap) {
  int n = 0;
  for (JsonObjectConst row : doc["data"].as<JsonArrayConst>()) {
    if (n >= cap) break;
    JsonObjectConst a = row["attributes"];
    StopTripRow& r = out[n];
    copy(r.trip_id, sizeof r.trip_id, a["trip_id"] | "");
    copy(r.route_id, sizeof r.route_id, a["route_id"] | "");
    copy(r.stop_id, sizeof r.stop_id, a["stop_id"] | "");
    r.direction_id = static_cast<int8_t>(a["direction_id"] | -1);
    if (!parse_gtfs_time(a["departure_time"] | "", &r.departure_s)) continue;
    if (!parse_iso_date(a["service_date"] | "", &r.service_date)) continue;
    if (r.trip_id[0] != '\0') n++;
  }
  return n;
}

int parse_trip_stops(const JsonDocument& doc, TripStop out[], int cap) {
  int n = 0;
  // Order is positional - these rows carry no stop_sequence, so never sort them.
  for (JsonObjectConst row : doc["data"].as<JsonArrayConst>()) {
    if (n >= cap) break;
    JsonObjectConst a = row["attributes"];
    TripStop& s = out[n];
    copy(s.stop_id, sizeof s.stop_id, a["stop_id"] | "");
    copy(s.stop_code, sizeof s.stop_code, a["stop_code"] | "");
    copy(s.parent_station, sizeof s.parent_station, a["parent_station"] | "");
    if (s.stop_id[0] != '\0') n++;
  }
  return n;
}

int parse_realtime(const JsonDocument& doc, RtEntity out[], int cap) {
  int n = 0;
  for (JsonObjectConst ent : doc["response"]["entity"].as<JsonArrayConst>()) {
    if (n >= cap) break;
    JsonObjectConst tu = ent["trip_update"];
    if (tu.isNull()) continue;
    RtEntity& e = out[n];
    memset(&e, 0, sizeof e);
    copy(e.trip_id, sizeof e.trip_id, tu["trip"]["trip_id"] | "");
    e.has_delay = !tu["delay"].isNull();
    e.delay = tu["delay"] | 0;
    e.cancelled = (tu["trip"]["schedule_relationship"] | 0) == 3;

    // The docs promise an array; the API sends one object. Accept both, taking
    // the first element of an array.
    JsonVariantConst stu = tu["stop_time_update"];
    if (stu.is<JsonArrayConst>()) stu = stu[0];
    if (!stu.isNull()) {
      copy(e.stu_stop_id, sizeof e.stu_stop_id, stu["stop_id"] | "");
      JsonVariantConst dep = stu["departure"];
      if (!dep.isNull() && !dep["delay"].isNull()) {
        e.has_stu_departure = true;
        e.stu_departure_delay = dep["delay"] | 0;
      }
    }
    if (e.trip_id[0] != '\0') n++;
  }
  return n;
}
