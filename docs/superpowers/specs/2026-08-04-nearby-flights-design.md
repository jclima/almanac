# Nearby Flights — Design Spec

Date: 2026-08-04
Status: Approved
Scope: Personal fork of CrossPoint Reader for the Xteink **X4**. Not intended for
upstream contribution — CrossPoint's [SCOPE.md](../../../SCOPE.md) explicitly
closes new external network connectors and rules out "active connectivity"
features on battery/CPU grounds. This feature is being added to a personal
fork specifically because of that, but the implementation still respects the
spirit of those constraints (no background polling, bounded memory) since
they're sound engineering concerns independent of upstream policy.

## Goal

Add a "Nearby Flights" screen that shows aircraft currently flying near a
configured home location, using free, keyless data from OpenSky Network.

## Decisions

| Question | Decision |
|---|---|
| Device | Xteink X4 only |
| Data source | [OpenSky Network](https://openskynetwork.github.io/opensky-api/rest.html) REST API, anonymous (no account) |
| Location | Fixed home location (lat/long), set once in Settings — the X4 has no GPS |
| Refresh | Manual only — one fetch on screen open, explicit on-screen "Refresh" action to re-fetch. No background polling |
| Units | Imperial (feet, mph, miles) |
| List content | Compact one-line rows (callsign, altitude, distance+bearing); tap a row for a detail view |
| Entry point | New "Nearby Flights" item on the home screen menu |
| Default radius | 30 miles, configurable in Settings |

## Why OpenSky over FlightAware AeroAPI

AeroAPI is the most complete source (routes, flight numbers) but is paid
per-query with no tier suited to casual "check a few times a day" personal
use. OpenSky's `/states/all` endpoint gives position, altitude, ground speed,
heading, vertical rate, and callsign for all ADS-B-tracked aircraft in a
lat/long bounding box, for free, anonymously, at ~10s data resolution — more
than sufficient for a manual-refresh e-reader screen. It does **not** include
route/origin/destination, which AeroAPI would provide — an accepted trade-off
for a free source in a personal fork.

## Architecture

The implementation reuses two patterns already established in the codebase
rather than introducing new ones:

1. **Bounded-memory streaming JSON parsing.** `lib/JsonParser/StreamingJsonParser`
   is a SAX-style parser already used by `ReleaseJsonParser` (OTA update
   checks) to consume large JSON bodies via callbacks, without ever holding
   the whole document in memory. `HttpDownloader::fetchUrl(url, DataCallback)`
   streams the HTTP response body in chunks, feeding a parser like this one
   directly. This is important because `platformio.ini` documents the device
   as heap-constrained (~50KB free during a reading session) and `states/all`
   responses over a busy airspace can be large.

2. **Wi-Fi-gated, single-Activity list screens.** `OpdsBookBrowserActivity`
   is the template: one `Activity` with an internal state enum
   (`CHECK_WIFI → WIFI_SELECTION → LOADING → BROWSING → ERROR`), driving the
   existing Wi-Fi-selection flow rather than reimplementing it, and rendering
   different content per state within a single `render()`.

### New files

```
lib/JsonParser/OpenSkyStatesParser.h
lib/JsonParser/OpenSkyStatesParser.cpp
src/network/OpenSkyClient.h
src/network/OpenSkyClient.cpp
src/activities/flights/NearbyFlightsActivity.h
src/activities/flights/NearbyFlightsActivity.cpp
src/activities/settings/FlightTrackerSettingsActivity.h
src/activities/settings/FlightTrackerSettingsActivity.cpp
```

### `OpenSkyStatesParser`

A `StreamingJsonParser` consumer, structured like `ReleaseJsonParser`
(static SAX callback trampolines + a small state machine over key names).

- Consumes the `states` array from `/api/states/all`. Each element is itself
  a JSON array (not object) of 18 positional fields per
  [the OpenSky REST spec](https://openskynetwork.github.io/opensky-api/rest.html);
  the parser tracks array index within each row rather than key names for
  that inner array.
- Decodes fields needed for display into one **reusable scratch struct**:
  `icao24, callsign, latitude, longitude, baroAltitude, geoAltitude, onGround,
  velocity, trueTrack, verticalRate, originCountry, lastContact`.
- On each row's array-end:
  - Skip if `onGround` is true (only airborne aircraft are relevant) or if
    lat/long are null (position not currently known).
  - Compute great-circle distance (haversine) and initial bearing from the
    configured home location.
  - Skip if distance exceeds the configured radius.
  - Insert into a **fixed-capacity array of the 20 closest matches**,
    keeping it sorted by distance (insertion sort on insert; evict the
    farthest entry if the array is full and the new one is closer).
- Memory footprint is O(1) in the number of aircraft in the response — one
  scratch struct plus a fixed 20-entry result array — regardless of how many
  rows the bounding box returns. Parsing itself still processes every row
  in the streamed response (no early termination), so response time scales
  with airspace density even though memory doesn't. Documented limitation:
  an extremely busy bounding box (e.g. over a major hub) will take longer
  to parse; the mitigation is reducing the configured radius, not further
  parser complexity.
- Exposes `count()` and `const FlightMatch& at(size_t i)` for the activity
  to render. A `responseTimestamp()` accessor for the top-level `time` field
  was scoped here but not implemented — see "Data age" under
  `NearbyFlightsActivity` below for what ships instead and why.

### `OpenSkyClient`

- `bool fetchNearby(double homeLat, double homeLon, double radiusMiles,
  OpenSkyStatesParser& outParser)`.
- Computes a lat/long bounding box from home location + radius (simple
  equirectangular approximation — fine at this radius scale).
- Builds `https://opensky-network.org/api/states/all?lamin=..&lomin=..&lamax=..&lomax=..`.
- Calls `HttpDownloader::fetchUrl(url, [&](data, len){ outParser.feed(...); return true; })`.
- Returns false on transport/HTTP error; caller reads `outParser.hasError()`
  for parse-level failure.

### `NearbyFlightsActivity`

States: `CHECK_WIFI, WIFI_SELECTION, LOADING, LIST, DETAIL, ERROR`, following
`OpdsBookBrowserActivity`'s shape (`ButtonNavigator` for list selection,
`onWifiSelectionComplete` callback pattern for the Wi-Fi flow).

- `onEnter()`: if home location isn't configured
  (`AlmanacSettings::flightTrackerHomeLat/Lon` unset), skip straight to a
  static message screen ("Set a home location in Settings first") instead of
  `CHECK_WIFI` — no point prompting for Wi-Fi with nowhere to search around.
  Otherwise proceeds through `CHECK_WIFI`/`WIFI_SELECTION` exactly like
  `OpdsBookBrowserActivity`, then `LOADING`, which calls `OpenSkyClient` and
  transitions to `LIST` or `ERROR`.
- `LIST`: one row per match from `OpenSkyStatesParser`, already sorted by
  distance. Row format: `"UAL1234 · 34,000ft · 8.2mi NE"` — callsign
  (trimmed/blank-if-none), altitude in feet (prefer `geoAltitude`, fall back
  to `baroAltitude`), distance in miles, 8-point compass bearing
  (N/NE/E/SE/S/SW/W/NW) derived from the computed bearing angle. Empty state:
  "No flights within {radius}mi" if the match list is empty.
  Following `OpdsBookBrowserActivity`'s pattern of assigning contextual
  meaning to buttons via `mappedInput.mapLabels(...)` and a button-hint
  footer (rather than a dedicated hardware key), `LIST` maps one free button
  slot to a "Refresh" action (label via `mapLabels`, same mechanism OPDS
  uses for its "Search" hint) that re-enters `LOADING`.
- `DETAIL` (entered on row select, back returns to `LIST`, no separate
  Activity/nav-stack entry): callsign, ICAO24, origin country, altitude,
  ground speed (mph), heading (true track, degrees + compass letter), climb/
  descend/level derived from vertical rate, distance + bearing, and data age
  ("as of Ns ago").
  - **Data age, as actually built:** rather than parsing OpenSky's top-level
    `time` field, the activity records `fetchCompletedMs = millis()` when a
    fetch completes and renders `(millis() - fetchCompletedMs) / 1000` as the
    age. This is a simplification, not the `responseTimestamp()` design
    above — it measures time since the *device's* fetch, not since OpenSky
    computed the snapshot, so it reads "As of 0s ago" immediately after a
    refresh even though OpenSky's own data has ~10s resolution. It was kept
    because it avoids parsing and threading an extra top-level field through
    the streaming parser for a display that's already approximate, and it
    sidesteps device-clock-vs-server-clock skew entirely. The tradeoff: the
    displayed age understates true data staleness by up to OpenSky's
    refresh interval.
- `ERROR`: shows the failure (no Wi-Fi, HTTP failure, parse failure) with a
  retry action back to `LOADING`.

### Settings

New fields on `AlmanacSettings` (`PersistableStore` pattern, JSON-backed
like existing fields):

- `double flightTrackerHomeLat = NAN;`
- `double flightTrackerHomeLon = NAN;`
- `uint16_t flightTrackerRadiusMiles = 30;`

`FlightTrackerSettingsActivity` (under `src/activities/settings/`) provides
text-entry fields for these three values, reusing the existing on-screen
keyboard/text-entry component already used for Wi-Fi SSID/password entry
(numeric input, not free text). Reachable from the main `SettingsActivity`
list.

### Home menu

One new entry in `HomeActivity`'s menu, "Nearby Flights", pushed via
`ActivityManager` to `NearbyFlightsActivity`, positioned near other
network-driven entries.

## Error handling

- No Wi-Fi configured / connection fails → existing `WIFI_SELECTION` flow
  handles this the same way OPDS/Calibre screens do.
- HTTP failure (timeout, non-200, TLS error) → `ERROR` state with retry.
- JSON parse error (`OpenSkyStatesParser::hasError()`) → `ERROR` state with
  retry; treated the same as an HTTP failure from the UI's perspective.
- Home location not configured → short-circuit before any network activity,
  point at Settings.
- Empty result set (valid response, zero matches in radius) → not an error;
  `LIST` state with an empty-state message.

## Testing

Host-side unit tests under `test/`, following the existing
`test/release_json_parser` / `test/streaming_json_parser` CMake pattern:

- `test/opensky_states_parser/`: feeds a canned `states/all` JSON fixture
  (including null fields, on-ground aircraft, aircraft outside radius, and
  more than 20 in-radius matches) through `OpenSkyStatesParser` and asserts
  the resulting sorted/bounded match list, skip rules, and O(1) memory
  behavior (array never exceeds 20 entries).
- Unit tests for the haversine distance/bearing math and the bounding-box
  computation in `OpenSkyClient`, independent of network I/O.
- No automated tests for the `Activity` state machine or on-device rendering
  — no existing precedent for this in the codebase; verified manually on
  hardware (or via the host build if a device is unavailable) as part of
  implementation.

## Out of scope for v1

- Auto-refresh / live polling.
- Wi-Fi-based or any automatic geolocation.
- Route/origin/destination (would require a paid data source).
- Aircraft type/airline enrichment from ICAO24 registration.
- OpenSky authenticated (OAuth2) access for higher rate limits.
