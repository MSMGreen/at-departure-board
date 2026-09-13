# AT Departure Board — design

An ESP32 + 2.8" TFT that shows when the next bus and train actually leave,
using Auckland Transport's realtime feed. Designed to be built by someone else
from the repo.

Status: approved design, ready for an implementation plan.
Date: 2026-09-06.

## 1. What it does

A always-on display showing up to four "watches". A watch is one answer to
"when does my next one leave?" — a stop, a route, and a direction. The two that
motivate the project:

| Watch | Stop | Route | Toward |
|---|---|---|---|
| Bus | 8213 | `20` | Wynyard Quarter |
| Train | Kingsland | *(any line)* | Waitematā |

Each watch gets a lane. The vehicle's **position along its lane encodes time to
arrival** — it enters at the 20-minute mark and pulls into the stop as the
countdown reaches zero. The animation is the reading, not decoration: the board
is legible from across a room without resolving any digits.

### Goals

- Correct at 1am, on a DST boundary, and on 13 September 2026 when the rail
  lines are renamed.
- Buildable by a stranger: no toolchain needed to point it at a different stop.
- Honest when it doesn't know. Stale data must never look live.

### Non-goals for v1

No touch input (the panel is not a touch panel), no OTA updates, no enclosure
(printed separately — see §11), no vehicle-position map, no ferries.

## 2. Hardware

- ESP32 dev board (WROOM-32), USB powered, always on.
- 2.8" 320x240 ILI9341 SPI panel, no touch.

| Display | ESP32 | Note |
|---|---|---|
| VCC | 3V3 | |
| GND | GND | |
| CS | GPIO15 | |
| RESET | GPIO4 | |
| DC / RS | GPIO2 | |
| SDI / MOSI | GPIO23 | |
| SCK | GPIO18 | |
| LED | GPIO32 | PWM via LEDC — this is what makes dimming possible |
| SDO / MISO | GPIO19 | |

Driving the backlight from a GPIO rather than 3V3 is not optional; §8 depends
on it.

## 3. Configuration

No secrets in the repo and no recompile to change a stop. First boot with empty
config: the board starts an AP (`at-board-setup`), serves a captive portal, and
the user enters WiFi credentials and their AT API key, then adds watches. After
that the same UI stays reachable on the LAN at the board's mDNS name.

Stored in NVS. A watch is:

```json
{
  "label": "Mine",
  "stop_code": "8213",
  "route_short_name": "20",
  "toward_stop_code": "1060",
  "enabled": true
}
```

**`direction_id` is deliberately not stored.** See §3a — this changed on
13 September 2026 and the original design would have shipped a silent, confident
wrong answer.

`kind` is **not** configured — it is derived from the GTFS `route_type` of the
departures returned (2 = Rail → train sprite, 3 = Bus → bus sprite). One less
thing to get wrong.

Direction is not typed either. The setup UI calls `stoptrips` for the stop,
shows the distinct `trip_headsign` values it actually returns, and the user
picks the one that reads like their destination. What gets stored is the
**stop they are travelling toward**, not the direction integer behind it.
Nobody should have to know what `direction_id: 0` means — and as it turns out,
nor should the firmware.

**`route_short_name` is optional. Empty means "any route".** This matters more
than it looks. At a train station, every city-bound departure is one you'd take,
whatever the line is called — so leaving it blank is both what the user wants
and the thing that carries the board through the 13 September rename with no
reconfiguration, as `WEST-201` trips become `E-W-201` trips. Pin a route only
when a stop is served by routes you would not board, as bus stop 8213 is.

The two configured watches, from live data (see `docs/at-api-notes.md`):

| | stop_code | route | toward |
|---|---|---|---|
| Bus | 8213 | `20` | `1060` Wynyard Quarter |
| Train | 122 | *(none)* | `133` Waitematā |

## 3a. Why direction is derived, not stored

The first version of this spec stored `direction_id`, chosen once at setup. CRL
opened on 13 September 2026 and the meaning inverted at Kingsland:

| | pre-CRL | post-CRL |
|---|---|---|
| `direction_id: 0` | `Swanson To Brit 2` — to the city | `Manukau To Swanson` — away |
| `direction_id: 1` | away | `Swanson To Manukau` — to the city |

A board built to that spec would have shown trains to Swanson: no error, no
stale marker, complete confidence. That is the worst failure this project can
have, and it would have happened one week after the design was written.

Text matching does not rescue it. Every pre-CRL headsign changed, and because
CRL made the line a through-route, *both* directions now read `Via Waitemata` —
only the destination distinguishes them.

So direction is **derived at every schedule refresh** from a stored destination:

1. Take one candidate trip per `direction_id` from `stoptrips`.
2. `GET /gtfs/v3/trips/{trip_id}/stops` — the stop list, in sequence order.
3. Find our stop; check whether `toward_stop_code` appears after it, matching
   either `stop_code` or `parent_station` (trips list platforms, not stations).
4. Cache the winning `direction_id` until the next refresh.

Direction is a property of `(route_id, direction_id)`, not of a trip, so this
costs **one extra request per watch per refresh** — about 8 per hour.

If neither direction serves the target, the board says **"check config"** rather
than guessing. A watch that cannot prove which way it is pointing must not
display a time.

Stations are configured by their own code (Kingsland = 122, `location_type: 1`);
`stoptrips` on a station returns every platform's departures, so platforms never
surface in the UI at all.

Maximum four watches: at five the lane height drops below what stays readable
across a room.

## 4. Architecture

| Module | Responsibility | Depends on |
|---|---|---|
| `config` | NVS-backed settings, watch list, validation | — |
| `portal` | AP captive portal + LAN config UI | `config`, `at_client` |
| `at_client` | All HTTPS to AT; streaming JSON → typed structs | `config` |
| `schedule` | Merge schedule with delays, sort, pick next 2 per watch | — (pure) |
| `clock` | NTP, `Pacific/Auckland`, DST, GTFS time arithmetic | — (pure) |
| `ui` | Layout, sprites, animation, dimming | `schedule` |
| `app` | State machine, cadence, error surfaces | all |

`schedule` and `clock` include no Arduino headers. They compile and test on a
laptop under PlatformIO's `native` environment, which is where the logic that
is hard to debug on-device gets proven.

State machine: `BOOT → (no config?) PORTAL → SYNC → RUN ⇄ DIM → SLEEP → RUN`.

## 5. Data flow

Endpoint specifics are in `docs/at-api-notes.md`, verified against the portal.
Three loops at deliberately different speeds:

**Route resolution — only for watches that pin a route.**
`GET /gtfs/v3/routes?filter[route_short_name]=20` → `route_id`, `route_type`.
Resolving by short name rather than storing a `route_id` is what carries a
pinned watch through the 13 Sep rename.

Colour comes from `route_color` **when the API supplies it, which for buses it
does not** — AT omits null fields and only colours rail. So the badge palette is
ours, keyed on `route_type`, with `route_color` overriding when present. Rail
colours are real and worth honouring (`E-W` `#97C93D`). `#000000` (HUIA) is
treated as absent rather than painted black on a dark ground.

**Schedule — every 15 minutes, plus on date rollover.**
`GET /gtfs/v3/stops/{stop_id}/stoptrips?filter[date]=…&filter[start_hour]=…&filter[hour_range]=2`
Filter client-side to the **derived** direction (§3a), and to its `route_id` if
one is pinned. One request per watch, plus one `trips/{id}/stops` call to
re-derive the direction.

`hour_range` is **clamped to the service day** — it does not roll past midnight.
A window crossing midnight needs a second request with the next `filter[date]`.

**Realtime — every 30 s while anything is within 30 min, else every 2 min.**
`GET /realtime/legacy/tripupdates?tripid=<all cached trip_ids>` — **one request
covering every watch**. Two things make this endpoint the right one: unfiltered
it would be all of Auckland and would not fit in heap, and the combined feed
(`/realtime/legacy/`) interleaves vehicle-position entities we never use, ~45%
wasted bytes.

Delay comes from the **trip-level `trip_update.delay`** (signed seconds,
negative = early). The documented per-stop route does not work in practice:
`stop_time_update` arrives as a single object rather than the documented array,
and it describes the vehicle's *next* stop, which is rarely ours. We prefer it
opportunistically when its `stop_id` matches, and fall back to `delay` — which
is the normal path. This is not a marginal correction: observed delays reached
−427 s, buses running seven minutes *early*.

The `tripid` filter is inexact — 6 requested ids returned 7 trips — so the
client re-filters returned `trip_id`s against its own set.

**Render — 15 fps.** ETA is computed locally as
`scheduled + delay − now`, so the countdown ticks every second and the vehicle
creeps visibly along its lane between polls. The network only ever corrects the
delay. This keeps the animation smooth regardless of latency and holds the API
budget near 130 requests/hour.

### Time handling

Two GTFS traps, both silent failures at exactly the hour this board matters:

- `departure_time` may exceed 24 hours (`25:10:00` = 01:10 the next day). The
  parser accepts hours > 23 and resolves against `service_date`.
- `filter[date]` is a **service** date, not a wall-clock date. A window crossing
  midnight requires a second request for the following date.

NTP with full `Pacific/Auckland` rules, not a fixed offset. NZDT begins
27 September 2026 — three weeks out, and a board an hour wrong is worse than a
board that is blank.

## 6. Resolving stop code → stop_id — settled

The number on the pole is not the API's id: `stop_id` is `{stop_code}-{hash}`.
Probed against the live API:

```
GET /gtfs/v3/stops/8213                   -> 404
GET /gtfs/v3/stops?filter[stop_code]=8213 -> 200, 223 bytes, one stop
```

`filter[stop_code]` works and is what we use, despite the portal documenting
only `filter[date]`. The design that fell out of the earlier uncertainty — a
streaming parse of every stop in Auckland — is **not needed and is dropped**.

Because the filter is undocumented it could be withdrawn. If it ever returns
400, the board says so plainly rather than silently degrading; that is a
one-line failure path, not a fallback implementation.

Resolved ids are cached in NVS: `8213` → `8213-7e021a72`,
`122` → `122-34ecc043` (Kingsland Train Station).

### 404 does not mean the id is stale

`stoptrips` returns **404 for "no services in this window"** — the same status as
an unknown stop. Kingsland returns 404 for every `hour_range` at hour 23 on a
Sunday, simply because the trains have stopped.

So a 404 from `stoptrips` renders the empty state and nothing else. Only a 404
from `GET /stops/{id}` invalidates a cached id. Conflating the two would make
the board re-resolve every stop every night, which is exactly what the first
draft of this spec said to do.

## 7. Screen

320x240, dark ground. One lane per enabled watch, `(240 − 18) / n` high.

- **Status bar**, 18 px: location label, live/stale indicator, clock.
- **Per lane**: route badge in AT's own `route_color`; headsign; the next
  departure in large digits with `then <n>` beneath; and the approach lane.
- **Approach lane**: track (road hatching for bus, rail for train), a stop
  marker at the right, and the vehicle at
  `x = 1 − min(eta, 20 min) / 20 min` across it.

Animation, at 15 fps:

- Idle bob, and road dashes / sleepers scrolling, so a vehicle 19 minutes out
  still reads as moving rather than broken.
- Final 60 seconds: the vehicle pulls in and settles at the marker.
- At departure it leaves right, the row promotes the following service, and the
  next vehicle enters from the left.
- No services left today: the vehicle parks at the left with the lane dimmed and
  the next service's day and time in text.

Full-screen redraws flicker and cost too much RAM (320x240x2 = 153 KB). Each
lane renders into a single reused `TFT_eSprite` of 320x56x2 = 35 KB, pushed per
lane. Peak heap: ~35 KB sprite + ~40 KB TLS + ~8 KB parser ≈ 85 KB of ~300 KB.

Sprites are authored in `tools/mockup.py` and exported from it as C arrays, so
the Python sandbox and the firmware cannot drift apart.

## 8. Degradation

The board is in a hallway and will be believed. It must not lie.

| Condition | Behaviour |
|---|---|
| WiFi or API unreachable | Keep last good data, show `stale 4m`, dim the lanes, keep animating. Never blank, never silently frozen. |
| Trip cancelled (`schedule_relationship: 3`) | Greyed vehicle, struck-through time, promote the next. Surfaced, never dropped. |
| 401 | "Check API key" plus the config URL. |
| 429 / 5xx | Exponential backoff to a 5-minute cap; last good data stays up. |
| `stoptrips` 404 or empty | Normal empty state: parked vehicle, next service in text. **Not** an error, and never a reason to re-resolve the stop. |
| `/stops/{id}` 404 | Cached id is stale (new GTFS version) — re-resolve from `stop_code`. |
| `filter[stop_code]` starts 400ing | Say so explicitly; the undocumented filter has been withdrawn and needs a code change. |
| No config | Boot into `PORTAL`. |

Brightness follows use rather than a light sensor: full while anything is within
30 minutes, dimmed otherwise, backlight off entirely during configurable quiet
hours. Polling drops to the slow cadence while dimmed and stops while asleep;
the board wakes on the timer, already populated, so it is never caught fetching
when someone walks up to it.

## 9. Testing

Native unit tests (PlatformIO `native` env) over `schedule` and `clock`, driven
by the fixtures already captured in `test/fixtures/`. Real responses, not
invented ones.

Cases, each traceable to something actually observed unless marked:

- `stop_time_update` as a single object, not an array
- `stop_time_update` describing a stop that is not ours → trip-level `delay`
- `stop_time_update` that *does* match ours → prefer `departure.delay`
- negative delay (−427 s observed: seven minutes early)
- realtime returning a trip we did not request → discarded
- `header.timestamp` as a float, not an integer
- `stoptrips` 404 → empty state, cache untouched
- direction cannot be derived: neither direction serves `toward_stop_code`
  → "check config", no times shown (post-CRL regression)
- direction flips between refreshes → the new one is used, silently and
  correctly
- window clamped at the service day → second date query issued
- a rail watch with no `route_short_name` accepting whatever line is running
- a bus route with no `route_color` → fallback palette; `#000000` → fallback
- `25:10:00` after-midnight time (*unobserved in samples; parse tolerantly*)
- 27 Sep DST transition (*synthetic*)
- cancelled trip, `schedule_relationship: 3` (*synthetic*)
- malformed JSON, truncated response

`tools/mockup.py` covers layout, and a `DEMO_MODE` build flag feeds synthetic
departures so the UI can be exercised — including the 1am and cancelled states —
without waiting for reality or holding an API key.

## 10. Repository

MIT. PlatformIO for pinned dependencies and the native test environment;
Arduino IDE documented as a fallback.

```
src/{config,portal,at_client,schedule,clock,ui,app}.{h,cpp}
test/                native tests + fixtures/
tools/mockup.py      layout sandbox + sprite exporter
docs/                at-api-notes.md, wiring, BOM, enclosure dimensions
```

README carries the BOM with prices, the wiring table from §2, how to get a free
AT key, and the flashing steps.

## 11. Enclosure

Printed separately by the owner, so the repo owes it dimensions rather than a
model: ESP32 and ILI9341 board outlines, mounting hole positions and pitch,
display active-area offset within its PCB, and USB connector position. In
`docs/enclosure.md`, measured during the hardware bring-up task.

## 12. API verification — done

Completed 2026-09-06 before any firmware was written. Fixtures are committed to
`test/fixtures/`; findings are in `docs/at-api-notes.md`. Five things the live
API does that its documentation does not describe, each of which would have been
a defect:

1. `filter[stop_code]` exists and works — the streaming-parse fallback is dropped.
2. `stoptrips` returns 404 for an empty window, not just an unknown stop.
3. `stop_time_update` is a single object, not an array, and usually describes a
   stop that isn't ours — so trip-level `delay` is the real source.
4. The `tripid` filter is inexact and needs client-side re-filtering.
5. Bus routes carry no `route_color` at all.

### Re-verified after CRL — 2026-09-13

Fixtures re-captured in `test/fixtures/post-crl/`. The result was one vindication
and one defect:

- **Unpinned rail routes worked exactly as designed.** `WEST-201` stopped
  running at Kingsland, `E-W-201` took over, and nothing needed reconfiguring.
  Stop id hashes survived the GTFS version change; the bus was untouched.
- **Stored `direction_id` inverted** and would have shown trains going the wrong
  way, confidently and silently. §3a replaces it with a derived direction.

The lesson generalises: anything stored at setup that encodes a *relationship*
rather than an *identity* will rot. Stop codes and route short names are
identities and survived. `direction_id` was a relationship and did not.
