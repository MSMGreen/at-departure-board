# Config portal: a web UI for stops and theme

Date: 2026-09-19
Status: designed, not built
Relates to: spec §3 (Configuration), §4 (Architecture)

## 1. What this covers

A LAN-reachable web page that changes the board's **location, watches and
theme** at runtime, so neither a stop change nor a theme change needs a
recompile and a reflash.

It is a staged first increment of spec §3, not all of it.

| In | Out (deferred, see §11) |
|---|---|
| Location, watches, theme in NVS | WiFi credentials in NVS |
| LAN config page over plain HTTP | AT API key in NVS |
| mDNS name + address on the panel | `at-board-setup` AP |
| Stop-code validation against AT | Captive portal, first-boot flow |

## 2. Why staged

WiFi credentials stay in `src/secrets.h`, which buys one property worth more
than the convenience it costs: **the board always rejoins the network, so the
config page is always reachable.** A bad saved config can make the departures
wrong; it cannot make the board unreachable. Every mistake is recoverable from
the browser that made it.

Moving credentials into NVS forfeits that. It needs an AP fallback, a first-boot
state machine and a recovery story for stale credentials — the parts most likely
to strand a board that is mounted on a wall. That work deserves its own design
pass rather than riding along with this one.

## 3. The `config` module

Split in two, because the interesting half must be testable on a laptop:

| Where | What | Arduino headers |
|---|---|---|
| `lib/core/src/config_schema.{h,cpp}` | Parse, validate, compact, serialize. Pure. | None — builds under `native` |
| `src/config.{h,cpp}` | NVS load/store, static storage, the accessors | Yes |

`config_schema` never touches NVS; it turns a JSON string into a validated
`Config` (or an error) and back again. That is where every rule in §9 is
tested. `src/config.cpp` is the thin wrapper that reads the blob out of NVS,
hands it to the schema layer, and owns the storage.

The `Config` **type** is declared in `config_schema.h`, since the pure layer
builds and returns one and the native tests need it. `src/config.cpp` owns the
single file-scope **instance**. Its char buffers are static, so every
`const char*` handed out is valid for the life of the program: `WatchConfig`
(`lib/core/src/live.h:10`) holds raw pointers, and today they point at string
literals — nothing downstream may learn that this changed.

```c
struct Config {
  char        location[24];
  uint8_t     theme;
  uint8_t     n_watches;                   // enabled only, compacted
  WatchConfig watches[MAX_WATCHES];        // point into buf below
  char        buf[MAX_WATCHES][4][32];     // label, stop, route, toward
};
```

Interface:

| Function | Purpose |
|---|---|
| `config_begin()` | Load from NVS; on missing or invalid, seed from `watch_config.h` |
| `config_watches()`, `config_n_watches()` | Replace `WATCHES` / `N_WATCHES` |
| `config_location()` | Replaces `LOCATION` |
| `config_theme()`, `config_set_theme(uint8_t)` | The live-applied `uint8_t`. The setter both updates the in-RAM value and persists it; it is the one runtime write permitted by §6 |
| `config_save_json(const char*)` | Validate via `config_schema`, then persist to NVS. Does **not** update the in-RAM `Config` — see §6 |
| `config_to_json(char*, size_t)` | Serve `GET /api/config` |

`src/watch_config.h` is **kept**, demoted from live configuration to the
compiled defaults. An unconfigured board therefore behaves exactly as it does
today, and a factory reset is an NVS erase.

### Call sites to convert

| File | Lines |
|---|---|
| `src/fetcher.cpp` | 30, 290, 355, 575, 705, 707 |
| `src/main.cpp` | 60 (also passes `config_theme()` in place of the literal `0`) |

The `static_assert` at `fetcher.cpp:30` becomes a runtime clamp inside the
validator, since the count is no longer known at compile time.

### `enabled` does not reach the core

Spec §3's watch object carries `"enabled"`; `WatchConfig` has no such field.
`config` publishes only enabled watches, compacted into a contiguous array, so
`live.cpp` and `fetcher.cpp` never learn the concept. `MAX_WATCHES = 4`
(`lib/core/src/model.h:11`) already matches the spec's cap of four.

## 4. Storage format

A single JSON blob under one NVS key, namespace `board`, key `cfg`.

```json
{ "v": 1, "location": "Kingsland", "theme": 0,
  "watches": [
    { "label": "to Wynyard Quarter", "stop_code": "8213",
      "route_short_name": "20", "toward_stop_code": "1060", "enabled": true }
  ] }
```

Chosen over discrete NVS keys because ArduinoJson is already linked, the browser
wants JSON anyway so `GET /api/config` serves the stored bytes nearly as-is, and
a future migration is one `"v"` field rather than a key-renaming exercise.
Discrete keys would mean 20+ index-encoded names to enumerate and hand-built
JSON on the way out.

NVS is log-structured: an interrupted write leaves the previous value intact
rather than a half-record. A schema check on load covers the rest, falling back
to the compiled defaults.

## 5. The `portal` module

New `src/portal.{h,cpp}`.

### It runs on its own task

`xTaskCreatePinnedToCore(portal_task, "portal", 8192, nullptr, 1, nullptr, 0)`
— core 0, alongside the fetcher (`fetcher.cpp:718`), leaving core 1 for
drawing.

`handleClient()` must not be called from the draw loop. Ghibli frames cost 58–59
ms of a 66 ms budget at 15 fps, so roughly 8 ms of slack; accepting a socket and
rendering a page inside that would drop frames visibly. Equal priority to the
fetcher is fine — it blocks on network IO and yields.

The 8 KB stack is a starting figure, to be confirmed with
`uxTaskGetStackHighWaterMark`, which the fetcher already reports at
`fetcher.cpp:626`.

`DNSServer` and `WiFi.softAP` are **not** used; they are captive-portal only.

### Endpoints

| Route | Behaviour |
|---|---|
| `GET /` | The page: one embedded `PROGMEM` string |
| `GET /api/config` | Current config as JSON |
| `GET /api/themes` | `theme_count()` and each `theme(i).name` |
| `POST /api/theme` | Set and persist theme; applies live; no reboot |
| `POST /api/stop` | Validate one stop code against AT; return its name |
| `POST /api/config` | Validate, persist, respond, then reboot |

### Assets are embedded, not on SPIFFS

A single `PROGMEM` string. SPIFFS would add a `pio run -t uploadfs` step, and
flashing this board already requires the manual BOOT/EN sequence every time
(`docs/hardware-notes.md`); doubling that is a worse cost than the flash bytes.
Gzipping with `Content-Encoding: gzip` is held in reserve if the page grows.

## 6. Concurrency rules

**The rule: nothing but `theme` is written to live config at runtime.**

The fetcher task reads `config_watches()` on core 0 with no lock. So
`POST /api/config` writes NVS *only* — it must not touch the in-RAM `Config`.
The sequence is: validate, persist, flush the HTTP response, `delay(250)`,
`ESP.restart()`. The new config is picked up by `config_begin()` on the way back
up, the one moment nothing else is reading it.

`theme` is the single exception, and only because a `uint8_t` store is atomic on
this target. It is read once per frame by `ui.cpp:210` via `b.theme`, which
`build_board` (`lib/core/src/live.cpp:277`) already takes as a parameter. No
render-path change is needed for runtime theme switching.

## 7. Discovery

`MDNS.begin("at-board")` once WiFi has associated, giving `at-board.local`
(spec §3 calls for an mDNS name).

The pre-first-fetch screen additionally shows the URL and the raw dotted-quad
address. mDNS fails often enough on Windows without Bonjour that the IP is a
necessary fallback, and that screen is currently dead time waiting on the first
fetch, so it costs no departure real estate.

## 8. Security

**There is none, deliberately.** The page is LAN-only, over plain HTTP, with no
authentication. Under this staged scope it exposes no secrets: the AT API key
stays in `secrets.h` and never reaches the browser. The worst a LAN peer can do
is change which stops are displayed.

This is only defensible while credentials stay compiled in. **When the API key
and WiFi password move to NVS, this decision must be revisited** — at that point
the page both holds and can disclose secrets.

## 9. Testing

### Native, written first

These exercise `lib/core/src/config_schema.{h,cpp}` (§3), which has no Arduino
headers and runs under `pio test -e native` beside `schedule` and `clock`. They
test parse, validate, compact and serialize against strings in memory; NVS is
not involved and is covered on-device instead. TDD: these are written before the
implementation.

- Rejects more than `MAX_WATCHES` watches
- Rejects an empty `stop_code`
- Accepts an empty `route_short_name` — it means "any route" (spec §3) and
  carries a rail watch through a line rename
- Clamps `theme` to `theme_count()`
- Rejects strings that would overflow the fixed buffers
- `enabled` compaction: three watches, middle one disabled, yields two contiguous
- Round-trips: serialize a `Config`, parse it back, get the same `Config`
- Garbage, truncated and empty payloads are each rejected with an error the
  caller can distinguish from a valid parse, so `config_begin()` knows to fall
  back to the compiled defaults

### On-device, run by hand

Every flash needs the manual BOOT/EN sequence first; these are checked one at a
time on the panel.

1. Unconfigured board behaves exactly as today
2. Panel shows `at-board.local` and the IP at boot
3. Page loads over the LAN
4. Theme change applies live, without a reboot, and survives a power cycle
5. Watch edit reboots and the new stop shows departures
6. A bad stop code gives a validation error, not a silently empty lane
7. 15 fps holds with the page open and with a request in flight
8. Portal task stack high-water mark and largest free heap block are healthy
9. Config survives a power cycle

## 10. Risks

| # | Risk | Mitigation |
|---|---|---|
| 1 | **Frame budget.** 58–59 ms of 66 ms is already spent; a socket-serving task is new load | Core split (portal on 0, draw on 1); measure with a request in flight rather than assume |
| 2 | **Heap fragmentation.** `WebServer` allocates a `String` per request; this codebase already tracks `heap_caps_get_largest_free_block` because fragmentation has bitten before | Watch largest-free-block, not just free heap |
| 3 | **Flash.** Measured +31 KB against 311 KB free | Re-measure at the end; gzip assets if needed |
| 4 | **Concurrency.** Handled by discipline, not by a lock | §6 states the rule explicitly; only `theme` is live-written |

## 11. Deferred

The remainder of spec §3, to be designed separately:

- WiFi credentials and the AT API key in NVS
- The `at-board-setup` AP and captive portal
- First-boot flow and the `BOOT → PORTAL` state-machine edge (spec §4)
- Authentication on the config page (§8), which becomes mandatory once the page
  can disclose secrets

## Appendix: measurements

Taken 2026-09-19 on the `esp32` environment, default partition table
(1,310,720 B app partition).

| Build | Flash | RAM |
|---|---|---|
| Live build as committed | 992,053 B (75.7%) | 86,128 B (26.3%) |
| Probe: `WebServer` + `DNSServer` + `Preferences` linked with live handlers | 1,024,141 B (78.1%) | 87,000 B (26.6%) |
| Delta | **+32,088 B** | +872 B |

The probe forced linkage through global constructors, which survive
`--gc-sections`, and was deleted after measuring. The figure is conservative for
this design: `DNSServer` is not used here.

No partition change is required. `docs/hardware-notes.md` predicts the portal
"will likely need `huge_app.csv`"; that was written before the portal was
costed, and the measurement says otherwise. The default table also reserves a
1.375 MB SPIFFS partition that nothing currently references.
