# Auckland Transport API — verified notes

Endpoints read off <https://dev-portal.at.govt.nz/> and then **exercised against
the live API** on 2026-09-06 (Sunday, ~17:40 NZST). Everything below is observed
behaviour, not documentation. Where the docs and reality disagree, reality is
recorded and the disagreement called out.

Auth: header `Ocp-Apim-Subscription-Key: <key>` on every request.
Free key from the dev portal; subscribe to both products.

| API | Base |
|---|---|
| General Transit Feed V3 (`gtfs-api`) | `https://api.at.govt.nz/gtfs/v3` |
| Realtime Compat (`gtfs-realtime-compat`) | `https://api.at.govt.nz/realtime/legacy` |

Captured responses live in `test/fixtures/`.

## Resolved: stop code → stop_id

`stop_id` is `{stop_code}-{hash}`. Two ways to get the hash, one works:

```
GET /gtfs/v3/stops/8213                  -> 404, "Resource Not Found"
GET /gtfs/v3/stops?filter[stop_code]=8213 -> 200, 223 bytes    <-- use this
```

`filter[stop_code]` is **undocumented** — the portal lists only `filter[date]` —
but it works and returns exactly one stop. `filter[stop_name]` also works, on
exact full match only (`Kingsland` returns empty; `Kingsland Train Station`
returns the station).

Being undocumented, it could be withdrawn without notice. The client should
surface a clear error if it ever starts 400ing rather than silently degrading.

Resolved for this project:

| Name | stop_code | stop_id | location_type |
|---|---|---|---|
| Kingsland Avenue (bus) | 8213 | `8213-7e021a72` | 0 (stop) |
| Kingsland Train Station | 122 | `122-34ecc043` | 1 (station) |

## Stations resolve to both platforms — don't ask users to pick one

`stoptrips` on the **parent station** (`location_type: 1`) returns departures
from every child platform, tagged with the platform's own `stop_id`:

```
122-34ecc043 -> stop_ids {9305-ef07ca76: 6, 9304-dcb2ed75: 6}
```

So a train watch stores the station code and filters on `direction_id`. Nobody
needs to know which platform is which.

## Scheduled departures

```
GET /gtfs/v3/stops/{stop_id}/stoptrips
      ?filter[date]=2026-09-06 &filter[start_hour]=17 &filter[hour_range]=2
```

Returns `departure_time`, `arrival_time`, `trip_id`, `route_id`, `direction_id`,
`trip_headsign`, `stop_headsign`, `stop_sequence`, `service_date` together. No
secondary joins needed.

### 404 means "no services in this window"

The single most important finding, and a trap:

```
122-34ecc043, hour 23, range 1|2|3|4|6  -> 404 for every range
122-34ecc043, hour 17, range 2          -> 200, 12 rows
```

Kingsland simply has no 23:00 trains on a Sunday. The API expresses "empty
result" as **404**, the same status as an unknown stop id.

Therefore: **never treat a 404 from `stoptrips` as a stale/invalid stop_id.**
Only a 404 from `GET /stops/{id}` means the id has gone stale. Conflating them
makes the board re-resolve every stop every night.

### The window does not cross midnight

```
8213, hour 23, range 1 -> 4 rows
8213, hour 23, range 6 -> 4 rows   (identical; clamped at end of service day)
```

`hour_range` is clamped to the service day. A window spanning midnight needs a
second request with `filter[date]` set to the following day. `hour_range` itself
accepts at least 6 and scales linearly (hour 17: range 1→8 rows, 6→44 rows).

`departure_time` > `24:00:00` is legal GTFS for after-midnight services. **Not
observed** in these samples — parse tolerantly anyway, don't rely on it.

## Realtime

Use the dedicated trip-updates path, not the combined feed:

```
GET /realtime/legacy/tripupdates?tripid=<comma separated>   3202 b, 6 entities
GET /realtime/legacy/?tripid=<same>                         5836 b, 12 entities
```

The combined feed interleaves `vehicle` position entities we have no use for —
~45% wasted bytes and heap. `/tripupdates` returns `trip_update` entities only.
(`/trip-updates`, with a hyphen, is a 404.)

### `stop_time_update` is a single object, not an array

The docs declare `StopTimeUpdate[]`. The API returns one object:

```json
"stop_time_update": {
  "stop_sequence": 20,
  "stop_id": "1060-00b64ee7",
  "arrival":   { "delay": -430, "time": 1788671870, "uncertainty": 0 },
  "departure": { "delay": -427, "time": 1788671873, "uncertainty": 17 },
  "schedule_relationship": 0
}
```

Worse, it describes the vehicle's **current/next** stop, which is almost never
our stop — across 6 trips, only 1 happened to carry ours.

**So the per-stop delay strategy does not work.** Use the trip-level
`trip_update.delay` (signed seconds; negative = running early). Observed values
ranged −427 s to −20 s, i.e. buses genuinely running up to 7 minutes early — a
board that ignored delay would be materially wrong, not marginally.

Opportunistically prefer `stop_time_update.departure.delay` when its `stop_id`
does match ours; otherwise fall back to `delay`. In practice the fallback is the
normal path.

### The tripid filter is not exact — re-filter client-side

Requesting 6 trip ids returned 7 distinct trips; the extra one
(`20-02006-63000-2-191733d1`) was a different direction on the same route. The
client must match returned `trip_id`s against its own set and discard the rest.

### Other realtime details

- `response.header.timestamp` is a **float** (`1788673055.67`), not an integer.
- Entity shape is `{id, trip_update, is_deleted}`.
- `trip.schedule_relationship: 3` = CANCELED.
- Protobuf available via `Accept: application/x-protobuf`; JSON is used so
  ArduinoJson can stream-filter without a protobuf dependency.

## Routes

```
GET /gtfs/v3/routes?filter[route_short_name]=20 -> route_id 20-202, route_type 3
GET /gtfs/v3/routes?filter[route_type]=2        -> all rail
```

### Bus routes carry no colour

Route 20 returns **only** `agency_id, route_id, route_long_name,
route_short_name, route_type`. No `route_color` — the API omits null/empty
fields, and AT doesn't colour bus routes. Rail routes do carry colour:

| route_id | short | colour |
|---|---|---|
| `WEST-201` | WEST | `#97C93D` |
| `E-W-201` | E-W | `#97C93D` |
| `O-W-201` | O-W | `#00AEEF` |
| `ONE-201` | ONE | `#00AEEF` |
| `STH-201` | STH | `#D52923` |
| `S-C-201` | S-C | `#D52923` |
| `EAST-201` | EAST | `#FDB913` |
| `HUIA-404` | HUIA | `#000000` |

So the UI needs its own fallback palette keyed on `route_type` for buses, and
should only use `route_color` when present. `#000000` (HUIA) must be treated as
"unusable on a dark background" rather than taken literally.

### The CRL rename is already in the feed

Both `WEST-201` and `E-W-201` exist **now**, sharing a colour, as do `ONE`/`O-W`
and `STH`/`S-C`. Only `WEST-201` currently has trips at Kingsland. On
13 September 2026 the trips move to `E-W-201`.

This is why a watch should **not** pin a route short name for rail. See below.

## What this means for the two configured watches

Observed at 17:37 on Sunday 2026-09-06:

**Bus — stop 8213**, 16 departures in 2 hours across `20-202`, `22R-202`,
`22N-202`. Every one is `direction_id: 0`. Route 20 reads
`St Lukes To Wynyard Quarter Via Kingsland` — city-bound.

**Train — station 122**, 12 departures, all `WEST-201`, split evenly:

| direction_id | headsign | meaning |
|---|---|---|
| 0 | `Swanson To Brit 2 Via Newmarket 2` | **toward the city** |
| 1 | `Brit 2 To Swanson 1 Via Newmarket 1` | away from the city |

So: bus = stop 8213 + route `20` + direction 0; train = station 122 + direction
0 + **no route filter at all**. Leaving the rail route unpinned means the board
keeps working on 14 September without anyone touching it, because whatever line
is running city-bound through Kingsland is by definition the one you want.
