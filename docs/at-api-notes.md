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

---

# Post-CRL re-verification — 2026-09-13

The City Rail Link opened today. Every endpoint was re-probed against the live
API at 15:30 NZST. Fixtures are in `test/fixtures/post-crl/`.

## What held

- **Stop ids survived the GTFS version change.** `8213-7e021a72` and
  `122-34ecc043` both still return 200. The hash is not per-version.
- **`filter[stop_code]` still works** — still undocumented, still the only way
  to resolve a stop code.
- **Leaving the rail route unpinned worked exactly as designed.** `WEST-201`
  stopped running at Kingsland and `E-W-201` took over, with no configuration
  change and nothing to reflash. Both route ids still exist in `/routes`; only
  the trips moved.
- **The bus is untouched.** Route 20 at stop 8213 is still `20-202`,
  `direction_id: 0`, headsign `St Lukes To Wynyard Quarter Via Kingsland`.

## What broke: `direction_id` flipped

This is the important one, and it would have shipped as a silent, confident
wrong answer.

| | pre-CRL (2026-09-06) | post-CRL (2026-09-13) |
|---|---|---|
| `direction_id: 0` | `Swanson To Brit 2` — **to the city** | `Manukau To Swanson` — away |
| `direction_id: 1` | `Brit 2 To Swanson` — away | `Swanson To Manukau` — **to the city** |

Confirmed against the trips' own stop lists rather than by reading headsigns:

```
dir 0 after Kingsland: Morningside, Baldwin Ave, Mt Albert, Avondale
dir 1 after Kingsland: Maungawhau, Karanga-a-Hape, Te Waihorotiu, Waitemata
```

A board storing `direction_id: 0` for "to the city", as the design said to,
would now show trains to Swanson — with no error, no stale flag, and complete
confidence.

### Why the obvious fixes don't work

- **Matching the headsign text fails.** Every pre-CRL headsign changed
  (`Brit 2` no longer exists; `Britomart` is now `Waitemata`).
- **Matching on "Via Waitemata" fails.** CRL made the line a through-route, so
  *both* directions now read `Via Waitemata`. Only the destination
  distinguishes them: `To Manukau` passes through the city, `To Swanson` does
  not.

### The fix: store a destination, not a direction

Store the **stop_code the user wants to travel toward** (Waitematā for "to the
city"). At each schedule refresh, resolve which `direction_id` currently serves
it downstream:

```
GET /gtfs/v3/trips/{trip_id}/stops
```

Returns the trip's stops **in sequence order**. Attributes are
`stop_id, stop_code, stop_name, stop_lat, stop_lon, location_type,
parent_station, platform_code, wheelchair_boarding` — note there is **no
`stop_sequence` field**; order is positional, so do not sort the array.

Direction is a property of `(route_id, direction_id)`, not of an individual
trip, so this costs **one extra request per watch per schedule refresh** (every
~15 min), cached in between. Take any candidate trip, fetch its stops, find our
stop, and check whether the target stop_code appears after it.

That encoding is durable against exactly what just happened: line renames,
route id changes, headsign rewrites, and direction flips. It is also what the
user actually means — "trains that will take me to Waitematā" — rather than an
internal integer that happened to point the right way in September.

### Match the parent station, not the platform

A trip's stop list contains **platform-level** stops, so a naive stop_code
comparison against a station code never matches:

```
Waitemata Train Station 1 -> stop_code 9001, parent_station 133-08da14b5
Maungawhau Train Station 1 -> stop_code 9291, parent_station 136-2d5b76e2
```

Station 133 never appears in the list; platform 9001 does. Each entry carries
`parent_station`, so the check is: does any downstream stop have
`stop_code == target` **or** `parent_station == target_stop_id`. Buses have no
parent, so the first half covers them on its own.

Verified targets for this project:

| | stop_code | route | toward | resolves to |
|---|---|---|---|---|
| Bus | 8213 | `20` | `1060` Wynyard Quarter | last stop of the trip |
| Train | 122 | *(none)* | `133` Waitematā | parent of platform `9001` |

Keep the resolved `direction_id` as a cache, never as the source of truth, and
re-derive it whenever the schedule is refetched.

---

# One station, two lines: direction is per route

Discovered while building the firmware data-path plan, against the live API on
2026-09-19. Kingsland is served by **two** rail lines, not one:

| route_id | via | reaches Waitematā? |
|---|---|---|
| `E-W-201` (Swanson↔Manukau) | Waitematā | yes |
| `O-W-201` (Henderson↔Onehunga) | Newmarket: Kingsland → Maungawhau → Grafton → Newmarket → … → Onehunga | no |

Only `E-W-201` reaches Waitematā. Taking **one candidate trip per
`direction_id`** (as §3a originally read) can pick an `O-W-201` trip for both
directions, and on 2026-09-19 it did — checking only those two trips against
`toward_stop_code=133` found nothing and wrongly concluded that no direction at
Kingsland serves Waitematā.

The spec's own §3a sentence already said the right thing: "Direction is a
property of `(route_id, direction_id)`, not of an individual trip." The bug was
in not deriving it per route. Every `(route_id, direction_id)` pair actually
running at the stop must be checked, not just one trip per `direction_id`.

On hardware the board now logs, for stop 122:

```
dirs 122: O-W-201/0 no  O-W-201/1 no  E-W-201/0 no  E-W-201/1 yes -> ok
```

## Realtime only reports trips already in progress

Asking `/realtime/legacy/tripupdates?tripid=...` about trips that haven't
started yet returns **fewer entities than ids requested** — the trip simply
isn't in the feed until it's under way. That's normal, not an error; the client
should not treat a short entity list as a fetch failure.

## Post-CRL fixtures (2026-09-19)

Re-verified against the live API on 2026-09-19, with new fixtures in
`test/fixtures/post-crl/`:

- `filter[start_hour]=0` is rejected: 400 `Invalid Request`, detail
  `"Key: 'StopTripRequest.StartHour' Error:Field validation for 'StartHour'
  failed on the 'required' tag"` (full body in
  `test/fixtures/post-crl/stoptrips_start_hour_0.json`). The validator reports
  hour 0 as *missing* (`required`), not out of range — this looks like a
  zero-value check on AT's side (Go's `required` tag rejects the zero value),
  not a range check, which is why hour 1 is accepted and hour 0 is not. The
  valid range is **1..23** — a 00:00–00:59 departure cannot be fetched with
  `start_hour=0`.
- An unknown stop code returns **200** `{"data":[]}`, not 404.
- A bad subscription key returns **401** with a `statusCode`/`message` body.
- `stops?filter[stop_code]` still resolves 8213/122/133/1060 to the same ids as
  on 13 September — the hashes are unchanged.

New fixtures, captured 2026-09-19: `test/fixtures/post-crl/stops_8213.json`,
`stops_122.json`, `stops_133.json`, `stops_1060.json`, `stops_unknown.json`,
`stoptrips_start_hour_0.json`.
