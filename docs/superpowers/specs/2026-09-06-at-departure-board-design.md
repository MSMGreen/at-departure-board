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

| Watch | Stop | Route | Direction |
|---|---|---|---|
| Bus | 8213 | `20` | to the city |
| Train | Kingsland | Western / `E-W` after 13 Sep 2026 | to the city |

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
  "direction_id": 0,
  "enabled": true
}
```

`kind` is **not** configured — it is derived from the route's GTFS `route_type`
(2 = Rail → train sprite, 3 = Bus → bus sprite). One less thing to get wrong.

Direction is not typed either. The setup UI calls `stoptrips` for the stop,
shows the distinct `trip_headsign` values it actually returns, and the user
picks the one that reads like their destination; the stored value is the
`direction_id` behind it. Nobody should have to know what `direction_id: 0`
means.

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

**Route resolution — once per watch, cached until the GTFS version changes.**
`GET /gtfs/v3/routes?filter[route_short_name]=20` → `route_id`, `route_type`,
`route_color`, `route_text_color`. Resolving by short name rather than storing a
`route_id` is what carries the board through the 13 Sep rename. AT's own
`route_color` drives the badge, so the board matches the signage.

**Schedule — every 15 minutes, plus on date rollover.**
`GET /gtfs/v3/stops/{stop_id}/stoptrips?filter[date]=…&filter[start_hour]=…&filter[hour_range]=2`
Filter client-side to the watch's `route_id` + `direction_id`. One request per
watch; yields `departure_time` and `trip_id` per candidate departure.

**Realtime — every 30 s while anything is within 30 min, else every 2 min.**
`GET /realtime/legacy/?tripid=<all cached trip_ids, comma separated>` — **one
request covering every watch**. Unfiltered this feed is all of Auckland and will
not fit in heap; the `tripid` filter is load-bearing, not an optimisation.

Per trip, prefer the `stop_time_update[]` entry whose `stop_sequence` matches
ours and read `departure.delay`; fall back to the trip-level `delay`.

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

## 6. Resolving stop code → stop_id

The number on the pole is not the API's id: `stop_id` is `{stop_code}-{hash}`
(`100` → `100-56c57897`). `/gtfs/v3/stops` accepts **only** `filter[date]`;
there is no `filter[stop_code]`.

`resolve_stop_id(code)` tries three strategies in order and caches the result in
NVS:

1. `GET /gtfs/v3/stops/{code}` — does the bare code resolve? One call.
2. `GET /gtfs/v3/stops?filter[stop_code]={code}` — undocumented; a clean 400
   means no.
3. `GET /gtfs/v3/stops`, stream-parse for the matching `stop_code`. Multi-
   megabyte, but ArduinoJson's streaming filter holds memory constant and this
   runs once, during setup, behind a progress bar.

Strategy 3 always works, so nothing is blocked on the answer. **Settling 1 and 2
with curl is the first task of implementation** — the difference is one request
versus a multi-megabyte stream, and it is five minutes of work to find out.

Cached ids are re-resolved on a 404, which is how a new published GTFS version
surfaces.

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
| No departures in window | Parked vehicle, next service in text. |
| No config | Boot into `PORTAL`. |

Brightness follows use rather than a light sensor: full while anything is within
30 minutes, dimmed otherwise, backlight off entirely during configurable quiet
hours. Polling drops to the slow cadence while dimmed and stops while asleep;
the board wakes on the timer, already populated, so it is never caught fetching
when someone walks up to it.

## 9. Testing

Native unit tests (PlatformIO `native` env) over `schedule` and `clock`, driven
by fixtures captured with curl during the first task and committed to
`test/fixtures/`. Real responses, not invented ones.

Cases: `25:10:00` rollover; midnight-spanning window needing two date queries;
the 27 Sep DST transition; negative delay (running early); cancelled trip;
missing `stop_time_update` for our stop, falling back to trip-level delay; empty
timetable; malformed JSON; a `stop_id` that 404s.

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

## 12. First task

Before any firmware: get a key, curl the endpoints, settle §6 strategies 1 and
2, and commit the responses as `test/fixtures/`. Every parser is then written
against real bytes rather than a documentation sample.
