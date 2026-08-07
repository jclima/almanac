# Zip Code Home Location — Design Spec

Date: 2026-08-07
Status: Approved
Builds on: [2026-08-04-nearby-flights-design.md](./2026-08-04-nearby-flights-design.md)
Scope: Personal fork of CrossPoint Reader for the Xteink **X4** (now
published as **Almanac**, `jclima/almanac`).

## Goal

Let the user set their Flight Tracker home location by typing a 5-digit US
zip code, instead of decimal latitude/longitude only. The zip is geocoded to
lat/lon once, on submit; nothing downstream of `AlmanacSettings` changes —
`NearbyFlightsActivity` reads `flightTrackerHomeLat`/`flightTrackerHomeLon`
exactly as it does today.

## Context: what already ships

`FlightTrackerSettingsActivity` is a 3-row settings screen (Home Latitude,
Home Longitude, Search Radius), each a `char[16]`/`uint8_t` field on
`AlmanacSettings`, edited via `KeyboardEntryActivity` and validated by a
local `parseCoordinate` (rejects non-numeric input, NaN, and out-of-range
values). It has no Wi-Fi handling — it's a pure local editor today.

Two established patterns from the flights feature transfer directly:

- **The `AdsbdbClient`/`AircraftInfoParser` pair**: a `StreamingJsonParser`
  consumer plus a thin network client returning a `Result{Ok, NotFound,
  Error}` tri-state, using `HttpDownloader::fetchUrl`'s `outStatus`
  out-parameter to distinguish "no record" (HTTP 404, not an error) from a
  genuine transport/parse failure.
- **The Wi-Fi-gated state machine**: `NearbyFlightsActivity`'s
  `CHECK_WIFI → WIFI_SELECTION → LOADING` shape, reusing
  `WifiSelectionActivity` rather than reimplementing connection UI, and
  tearing Wi-Fi down via disconnect-then-`silentRestart()` in `onExit()` so
  the heap doesn't fragment across the session.

Both apply here with no new mechanism invented.

## Decisions

| Question | Decision |
|---|---|
| UI placement | A 4th row, **"Home Zip Code"**, added above the existing Latitude/Longitude rows. Entering it geocodes immediately and overwrites Lat/Lon, which stay visible and remain independently editable (the fallback for non-US users or if the API is unreachable) |
| Persistence | The zip code itself is stored (new `char[6]` field), so the row shows your last entry rather than resetting to blank |
| Wi-Fi | The settings screen gains the same `CHECK_WIFI → WIFI_SELECTION → LOADING` flow `NearbyFlightsActivity` uses, triggered only when the zip row is committed — the other two rows are unaffected |
| Country scope | US only. 5-digit numeric validation; hardcoded to Zippopotam's `/us/` endpoint |
| Geocoding source | [Zippopotam.us](https://api.zippopotam.us) — free, keyless, HTTPS. Verified live (2026-08-07): `GET /us/90210` → HTTP 200 with `places[0].latitude`/`longitude`; `GET /us/00000` → HTTP 404 with an empty `{}` body |

### Why Zippopotam over other options

The US Census Geocoder was checked live and ruled out: its address endpoint
requires a street address, not zip-alone — a 400 with `zip=90210` and
nothing else. Zippopotam needs only the zip, returns a small clean JSON
body, and its 404-on-unknown-zip shape is exactly what
`AdsbdbClient::Result::NotFound` was already built to handle.

**Load-bearing detail, verified against the real response, not assumed:**
`latitude`/`longitude` in Zippopotam's JSON are **strings**
(`"latitude": "34.0901"`), not numbers — unlike OpenSky's `states/all`,
which uses real JSON numbers for the equivalent fields. A parser written by
analogy to `OpenSkyStatesParser` would read the wrong callback and silently
capture nothing. `ZipGeocodeParser` must consume these via the string
callback.

## Architecture

### `lib/JsonParser/ZipGeocodeParser`

A `StreamingJsonParser` consumer, same shape as `AircraftInfoParser`:
static SAX callbacks, a small position/key state machine, fixed-size output.

```cpp
struct ZipGeocodeResult {
  bool found = false;
  double latitude = 0;
  double longitude = 0;
  char placeName[32] = {0};   // e.g. "Beverly Hills" -- for the confirmation message
  char stateAbbrev[4] = {0};  // e.g. "CA"
};
```

Consumes the first entry of the top-level `places` array; `latitude`/
`longitude` are parsed from their string values with `strtod`. `found` is
set once a complete `places[0]` object has closed — mirroring
`AircraftInfoParser`'s `sawAircraftObject` flag, so a truncated body can
never present as complete. No allocation: two `double`s, two fixed
`char[]`s, one `bool`.

### `src/network/ZipGeocodeClient`

Mirrors `AdsbdbClient` exactly:

```cpp
class ZipGeocodeClient {
 public:
  enum class Result { Ok, NotFound, Error };
  // zip must be exactly 5 ASCII digits (validated by the caller). parser
  // must already be reset() before calling.
  static Result geocode(const char* zip, ZipGeocodeParser& parser);
};
```

Builds `https://api.zippopotam.us/us/<zip>`, calls
`HttpDownloader::fetchUrl` with the `outStatus` parameter, maps HTTP 404 to
`NotFound` (parser left in its reset state, matching `AdsbdbClient`'s
handling of adsbdb's 404) and anything else non-200 to `Error`.

### `AlmanacSettings`

One new field, next to the existing three:

```cpp
char flightTrackerHomeZip[6] = "";  // "" = unset; 5 digits + NUL
```

Registered category-less in `SettingsList.h`, same as
`flightTrackerHomeLat`/`Lon` — persisted through the generic
`SettingInfo::String` mechanism, not shown in the generic Settings list,
edited only by `FlightTrackerSettingsActivity`.

### `FlightTrackerSettingsActivity`

`ITEM_COUNT` becomes 4. New row order: **Home Zip Code, Home Latitude, Home
Longitude, Search Radius**.

New internal state, added to (not replacing) the existing row-list
rendering:

```cpp
enum class ZipLookupState { IDLE, CHECK_WIFI, WIFI_SELECTION, LOADING };
```

Committing the zip row:

1. **Format check first, no network call on failure.** Reject anything that
   isn't exactly 5 ASCII digits, same fail-fast style as `parseCoordinate`.
2. Transition through `CHECK_WIFI` → (if needed) `WIFI_SELECTION` via
   `WifiSelectionActivity`, exactly as `NearbyFlightsActivity` does →
   `LOADING`, showing "Looking up 90210…" in the existing header-subtitle
   slot already used for the invalid-coordinate message.
3. On `Result::Ok`: format `latitude`/`longitude` into the existing
   `flightTrackerHomeLat`/`Lon` `char[16]` fields via `snprintf("%.4f", …)`
   (four decimal places is sub-100m precision, comfortably inside the
   16-byte buffer even at the longest possible value), persist the zip
   itself, `saveToFile()` once for both, return to `IDLE`.
4. On `Result::NotFound`: show "Zip code not found", log at `LOG_ERR`
   (matching `rejectCoordinate`'s existing logging), return to `IDLE`. Not
   a transport failure — logged and shown as a normal miss, same posture as
   `AircraftInfoParser`'s not-found case.
5. On `Result::Error`: show a network-failure message; log the transport
   and parse-failure cases **separately** (matching
   `NearbyFlightsActivity::ensureAircraftInfo`'s existing convention of
   distinguishing the two on the serial console).

`onExit()` gains the same disconnect-then-`silentRestart()` teardown
`NearbyFlightsActivity`/`OpdsBookBrowserActivity` use, since this activity
now touches Wi-Fi. Only fires if `WiFi.getMode() != WIFI_MODE_NULL` — a
session that only ever edits lat/lon/radius by hand never triggers it.

Lat/Lon rows are otherwise untouched: still independently editable, still
validated by the existing `parseCoordinate`, so a zip-code miss or an
offline session never blocks setting a home location manually.

## Error handling

- Invalid zip format → rejected locally, no network activity, no state
  transition beyond the existing transient-error display.
- `NotFound` and `Error` are both shown as transient header messages and
  both return to `IDLE` — the row keeps whatever zip/lat/lon was last
  successfully saved; a failed attempt never blanks existing settings.
- Wi-Fi connection failure surfaces via the same `WIFI_SELECTION` →
  `onWifiSelectionComplete(false)` path `NearbyFlightsActivity` already
  uses.
- If the device already has an unrelated stale Wi-Fi session (e.g. the user
  backed out of Nearby Flights mid-connect), `CHECK_WIFI`'s "already
  connected" check picks that up and skips straight to `LOADING`, exactly
  as it does today.

## Testing

- **`test/zip_geocode_parser`** (new): the real captured Zippopotam success
  body (string-typed lat/lon, `places[0]` extraction), the empty-`{}`
  not-found body, a truncated body (must not present as `found`), and a
  chunked-feed case — same structure as `test/aircraft_info_parser`.
- No host test for `ZipGeocodeClient` or the activity changes — matches the
  established convention (`HttpDownloader.h` pulls Arduino/FreeRTOS headers
  unavailable on the host; no `Activity` in this codebase has a host test).
  Verified by the firmware build and on-device checks.
- On-device: a known-good zip (e.g. `90210`), a well-formed but nonexistent
  zip (e.g. `00000`), malformed input (`abc12`, `9021`), and the Wi-Fi
  prompt appearing only when not already connected.

## Out of scope

- Non-US postal codes.
- Reverse geocoding (showing a place name for manually-entered lat/lon).
- Auto-detecting location from Wi-Fi AP data or any other source — this
  device has no GPS and this spec doesn't change that; zip code is another
  manual entry method, not automatic detection.
- Editing/clearing the zip independently of a fresh successful lookup —
  there is no dedicated "clear" action; the existing keyboard-entry flow
  covers re-typing a new zip, and the underlying lat/lon fields remain
  directly editable as the manual fallback.
