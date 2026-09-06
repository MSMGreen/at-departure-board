# Auckland Transport API — verified notes

Read off <https://dev-portal.at.govt.nz/> on 2026-09-06. Everything below is
copied from the portal's own operation pages, not inferred.

Auth: header `Ocp-Apim-Subscription-Key: <key>` on every request.
Free key: sign up at the dev portal, subscribe to both products below.

Two separate APIs are involved:

| API | Base | Purpose |
|---|---|---|
| General Transit Feed V3 (`gtfs-api`) | `https://api.at.govt.nz/gtfs/v3` | Static schedule. JSON:API, `application/vnd.api+json` |
| Realtime Compat (`gtfs-realtime-compat`) | `https://api.at.govt.nz/realtime/legacy` | Live delays. Updated at least every 30 s |

## The three calls this project needs

### 1. Resolve a route by its short name — survives the 13 Sep 2026 rename

```
GET /gtfs/v3/routes?filter[route_short_name]=20
GET /gtfs/v3/routes?filter[route_type]=2          # 2 = Rail
```

Returns `route_id`, `route_short_name`, `route_long_name`, `route_color`,
`route_text_color`. **`route_color` is worth using** — it gives us AT's real
line colours for the badges instead of ones we invent.

Note `route_id` embeds the short name as a prefix: `NX1-203`, `325-221`. Do not
rely on that — resolve properly through this endpoint, because the CRL rename on
**13 September 2026** changes both the ids and the short names (Western Line →
East-West Line, `E-W`).

### 2. Scheduled departures at a stop — one call, everything we need

```
GET /gtfs/v3/stops/{stop_id}/stoptrips
      ?filter[date]=2026-09-06        # required, YYYY-MM-DD, service date
      &filter[start_hour]=17          # required, integer 0-23
      &filter[hour_range]=2           # optional
```

Each element of `data[].attributes`:

```json
{
  "arrival_time": "05:35:00",
  "departure_time": "05:35:00",
  "direction_id": 1,
  "route_id": "NX1-203",
  "service_date": "2023-06-01",
  "stop_headsign": "ALBANY STN",
  "stop_id": "7036-f1ffa0be",
  "stop_sequence": 3,
  "trip_headsign": "Britomart (Lower Albert St) To Albany Station",
  "trip_id": "1395-27002-19800-2-a27d3190",
  "trip_start_time": "05:30:00"
}
```

This is better than expected: `departure_time` + `trip_id` + `route_id` +
`direction_id` + `trip_headsign` all arrive together, so no separate
trips/stop_times joins are needed.

Two consequences to handle:

- `filter[date]` is a **service date**. A window running past midnight needs a
  second call for the following date.
- `departure_time` can exceed `24:00:00` in GTFS (e.g. `25:10:00` = 01:10 next
  day). The parser must accept hours > 23.

### 3. Realtime delays, filtered to the trips we already care about

```
GET /realtime/legacy/?tripid=<comma-separated trip_ids>
```

`tripid` filtering is the critical part — the unfiltered feed is every active
trip in Auckland and would not fit in ESP32 heap.

```json
{
  "status": "OK",
  "response": {
    "header": { "gtfs_realtime_version": "1.0", "timestamp": 259982000 },
    "entity": [{
      "id": "259982000",
      "trip_update": {
        "trip": { "trip_id": "...", "route_id": "...", "direction_id": 0,
                  "start_time": "2022-04-07", "start_date": "20190528",
                  "schedule_relationship": 0 },
        "vehicle": { "id": "512000545", "label": "DALDY", "license_plate": "ZMZ7645" },
        "stop_time_update": [{
          "stop_sequence": 1,
          "stop_id": "6955-01-01",
          "arrival":   { "delay": -441, "time": 1559005659 },
          "departure": { "delay": -441, "time": 1559005659 }
        }],
        "timestamp": 1558997153,
        "delay": -67
      }
    }]
  },
  "error": {}
}
```

- Prefer the `stop_time_update[]` entry matching our `stop_sequence`; fall back
  to the trip-level `delay` when the feed doesn't include our stop.
- `delay` is **signed seconds** — negative means running early.
- `schedule_relationship: 3` on the trip means CANCELED. Show it, don't hide it.
- Protobuf is available via `Accept: application/x-protobuf`; we use JSON because
  ArduinoJson can stream-filter it without a protobuf dependency.

Other realtime operations exist (Trip Updates, Vehicle Positions, Service
Alerts, Ferry Positions) but the Combined Feed above covers our needs in one
request.

## The one real gap: stop code → stop_id

`stop_id` is **not** the stop code on the pole. It's `{stop_code}-{hash}`:

```
stop_code "100"  ->  stop_id "100-56c57897"   (Papatoetoe Train Station)
stop_code "7036" ->  stop_id "7036-f1ffa0be"
```

So stop 8213 is `8213-<hash>` and we must learn the hash. The problem:

```
GET /gtfs/v3/stops[?filter[date]]     # filter[date] is the ONLY filter
GET /gtfs/v3/stops/{id}               # needs the full hashed id already
```

There is no `filter[stop_code]`. Invalid filters return a clean 400
(`"Filter is invalid. Cannot find the specified filter"`), so this is cheap to
probe.

**Untested, in priority order — settle these with curl before writing the
client:**

1. Does `/gtfs/v3/stops/8213` resolve by bare code? (One call, best case.)
2. Does an undocumented `filter[stop_code]=8213` work? (400 = no.)
3. Fallback that definitely works: `GET /gtfs/v3/stops` once, stream-parse for
   the matching `stop_code`, cache the hashed `stop_id` in NVS forever. Slow and
   large, but it is a one-time setup-only operation and ArduinoJson's streaming
   filter keeps memory constant regardless of response size.

Design accordingly: `resolve_stop_id()` is one function with three strategies
tried in order, and the result is cached. Whichever wins, the rest of the
firmware is unaffected.

Re-resolve when a cached `stop_id` starts returning 404 — the hash changes when
AT publishes a new GTFS version (`GET /gtfs/v3/versions` reports the active one).

## Kingsland, and "to the city"

Kingsland has platforms; `location_type`, `parent_station` and `platform_code`
distinguish them. Rather than make the user work that out, the setup UI should
list what the API returns for the stop and let them pick the direction by its
real `trip_headsign`, storing the chosen `direction_id`.
