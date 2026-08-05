# Radar View & Aircraft Type — Design Spec

Date: 2026-08-05
Status: Approved
Builds on: [2026-08-04-nearby-flights-design.md](./2026-08-04-nearby-flights-design.md)
Scope: Personal fork of CrossPoint Reader for the Xteink **X4**. Same
divergence from upstream `SCOPE.md` as the base feature, and the same
self-imposed constraints: user-initiated fetches only, bounded memory.

## Goal

Two additions to the shipped Nearby Flights feature:

1. **Radar view** — a polar plot of nearby aircraft around your home location,
   toggled from the existing list.
2. **Aircraft type** — show manufacturer/type/registration on the detail
   screen, looked up on demand.

## Context: what already ships

The base feature is merged, built, flashed, and verified on real hardware
(4 fetches, 20 aircraft each, zero errors, ~36 KB peak transient heap with
~68 KB still free at the tightest moment). `OpenSkyStatesParser` already
computes `distanceMiles` and `bearingDeg` per match — polar coordinates —
so the radar needs no new data, only new rendering.

## Decisions

| Question | Decision |
|---|---|
| Radar placement | Toggle inside `NearbyFlightsActivity`, sharing fetched data. No new activity, fetch, or Wi-Fi flow |
| Aircraft marks | Filled triangle rotated to true track |
| Range rings | Fixed at ⅓, ⅔, and full configured radius; labelled in miles |
| Selection | Cycle nearest-first; selected mark drawn filled, larger, with a solid ring; Confirm opens detail |
| Entry view | Still the list — opening the screen behaves exactly as it does today |
| Type source | [adsbdb.com](https://api.adsbdb.com) — free, keyless, HTTPS |
| Type lookup timing | On entering the **detail** screen only, for that one aircraft |
| Type fields shown | `manufacturer` + `icao_type` (e.g. "Boeing B739"), plus `registration` |

## Part 1: Radar View

### Buttons

Four physical slots, and the list already used all four. New allocation:

```
LIST      « Back | Select | Radar | Down
RADAR     « Back | Detail | List  | Next
```

`Refresh` moves to a **long-press of Confirm** on both views, following the
`LONG_PRESS_MS` / `confirmHeld` / `confirmLongHandled` pattern already used by
`KeyboardEntryActivity` — not a new input mechanism.

**Known limitation, accepted:** there is still no "Up". Navigation cycles
down and wraps, as it does today. With at most 20 matches that is ≤20
presses. Fixing it properly needs the OPDS-style contextual button
(`OpdsBookBrowserActivity` swaps its third slot between "Search" and "Up"
based on selection position) and is deliberately out of scope here.

### Rendering

`RADAR` is added to the existing `FlightsState` enum (joining `NO_LOCATION`,
`CHECK_WIFI`, `WIFI_SELECTION`, `LOADING`, `LIST`, `DETAIL`, `ERROR`), with a
`renderRadar()` path. Both the `loop()` and `render()` switches are
exhaustive with no `default:`, so the compiler flags the new value until both
handle it. It reads the same `OpenSkyStatesParser` instance the list reads —
toggling copies nothing and fetches nothing.

Layout, all derived from `renderer.getScreenWidth()`/`getScreenHeight()` and
`UITheme` metrics (no hardcoded 480/800):

- **Centre** = your home location, drawn as a filled dot inside a ring.
- **Range rings** at ⅓, ⅔, and 1× `SETTINGS.flightTrackerRadiusMiles`,
  labelled (e.g. `10` / `20` / `30mi` at the default radius). Drawn with
  `GfxRenderer::drawArc`, which renders a quarter arc per call selected by
  its `xDir`/`yDir` params — a full circle is four calls.
- **Crosshair** N–S and E–W through the centre, with `N`/`E`/`S`/`W` letters
  outside the outer ring.
- **Aircraft** as filled triangles via `GfxRenderer::fillPolygon`, positioned
  by (bearing, distance) and rotated to `headingDeg`. Aircraft with no
  heading (`hasHeading == false`) draw as an unrotated triangle pointing up;
  they are not hidden.
- **Selected aircraft** drawn as the same filled triangle, larger, with a
  solid selection ring around it. (Shipped as filled/solid rather than the
  hollow-glyph-plus-dashed-ring-plus-adjacent-label originally planned here —
  see "Implementation note" below.)
- **Detail strip** below the plot: the selected aircraft's callsign,
  distance + compass bearing, and altitude — three lines, not the full set
  originally planned (see "Implementation note" below).

**Implementation note (added after shipping):** this section originally
called for the selected mark drawn *hollow* with a *dashed* ring and its
*callsign beside it*, and a six-value detail strip (callsign, distance,
altitude, speed, heading, climb/descent). What shipped instead is a filled,
larger triangle with a solid ring and no adjacent label, and a three-line
strip (callsign, distance, altitude). The drift was never reconciled back
into this spec at the time; this note corrects that after the fact, with the
best available reasoning for each:

- *Mark style* (filled vs. hollow, solid vs. dashed ring, no adjacent
  label): a hollow glyph is barely legible at the ~15px mark size on a 1-bit
  e-ink panel — no anti-aliasing and no gray fill to read as "outlined", so a
  thin outline at that size tends to disappear or alias into noise. An
  adjacent callsign label also clutters fast with up to `MAX_MATCHES` (20)
  marks on screen, several of which can sit close together near the plot
  centre at short range, each needing its own label placement to avoid
  overlapping neighbours or other marks.
- *Three-line strip instead of six*: the three dropped fields (speed,
  heading, climb/descent) are redundant with the existing detail screen,
  one Confirm press away, and a taller reserved strip would tighten the
  plot's vertical budget further on already-tight layouts — the detail
  screen's own rendering (`NearbyFlightsActivity::renderDetail`) already
  has to truncate lower-priority fields on the tightest theme+orientation
  combination it supports.

Radius in pixels is `min(usableWidth, usableHeight) / 2 - margin`, so
landscape orientations (800×480) shrink the circle to fit height rather than
clipping it.

### New geometry helpers

Two pure functions added to `lib/Geo/GeoMath`, which keeps the visual
geometry **host-testable** rather than device-eyeball-only:

```cpp
struct ScreenPoint { int x; int y; };

// Maps a (distance, bearing) polar pair to a screen point, with bearing 0
// pointing up (north) and increasing clockwise. Distances at or beyond
// maxRangeMiles clamp to the outer ring rather than escaping the plot.
ScreenPoint polarToScreen(double distanceMiles, double bearingDeg,
                          double maxRangeMiles, int cx, int cy, int radiusPx);

// Fills xs[4]/ys[4] with the vertices of an arrow-like triangle centred at
// (cx,cy), rotated so its nose points along headingDeg (0 = up/north).
// Output feeds GfxRenderer::fillPolygon directly.
void headingTriangle(int cx, int cy, double headingDeg, int size,
                     int xs[4], int ys[4]);
```

Both get host unit tests alongside the existing `GeoMath` suite: cardinal
bearings land at the expected screen positions, distance scales linearly to
radius, over-range distances clamp, and the triangle's nose vertex points in
the right direction for each cardinal heading.

## Part 2: Aircraft Type

### Why adsbdb, and what was ruled out

Verified against the live APIs on 2026-08-05:

- **OpenSky `states/all`** carries no type field. Adding `extended=1` yields
  a `category` field, but it is a *weight class*, not a type, and it was
  empty (`0` = "No info") for 13 of 14 aircraft sampled over the test
  location. Not usable.
- **OpenSky's aircraft metadata endpoint** (`/metadata/aircraft/icao/...`)
  returns **HTTP 410 Gone** — retired.
- **hexdb.io** did not respond.
- **adsbdb.com** works: free, no API key, HTTPS, ~0.54 s and ~480 bytes per
  lookup.

### Response shape

Success:

```json
{"response":{"aircraft":{
  "type":"737NG 990ER/W","icao_type":"B739","manufacturer":"Boeing",
  "registration":"N251AK","registered_owner":"Alaska Airlines", ...}}}
```

Not found:

```json
{"response":"unknown aircraft"}
```

**The not-found case changes `response` from an object to a string.** A
parser that assumes an object will misparse it. This is not hypothetical —
one of the first four aircraft sampled (KLM615) hit it. The parser must
handle both shapes.

`icao_type` is used rather than `type`: the verbose field returns
inconsistent, wide strings like `"737NG 990ER/W"` that do not fit a 480 px
screen, whereas `icao_type` is the clean 4-character ICAO designator.
Displayed as `manufacturer + " " + icao_type` → **"Boeing B739"**.

### Why detail-screen-only

Looking up all 20 matches on list entry would mean 20 sequential TLS
handshakes — roughly 40 seconds and heavy heap churn (each handshake peaks
around 36 KB transient, measured on device) to populate rows the user mostly
scrolls past. A single lookup when a specific aircraft is opened costs ~0.5 s
and reuses the live connection. Manual-refresh discipline, applied to a
second endpoint.

### Components

**`lib/JsonParser/AircraftInfoParser`** — a `StreamingJsonParser` consumer in
the same shape as `OpenSkyStatesParser` and `ReleaseJsonParser`. Extracts
`manufacturer`, `icao_type`, and `registration` into a fixed struct; detects
the string-valued `response` and reports "not found" distinctly from a parse
error.

```cpp
struct AircraftInfo {
  char icao24[7] = {0};
  char manufacturer[24] = {0};
  char icaoType[8] = {0};
  char registration[12] = {0};
  bool found = false;
};
```

~50 bytes, one instance held by the activity. No allocation.

**`src/network/AdsbdbClient`** — mirrors `OpenSkyClient`: builds
`https://api.adsbdb.com/v0/aircraft/<icao24>` and streams the response into
the parser via `HttpDownloader::fetchUrl`'s `DataCallback` overload.

**`NearbyFlightsActivity`** — on entering `DETAIL`, if the selected
aircraft's `icao24` differs from the cached `AircraftInfo`, fetch it. A
**single-slot cache** keyed on `icao24` means going detail → back → same
detail does not re-fetch. Deliberately not an N-entry LRU: one slot covers
the common back-and-forth, and the extra bookkeeping is not earned.

The detail screen gains one line, rendered per outcome:

| Outcome | Shown |
|---|---|
| Found | `Boeing B739 · N251AK` |
| `"unknown aircraft"` | `Type: unknown` |
| Transport or parse failure | `Type: unavailable` |

Failures are logged distinguishably (`LOG_ERR` with the cause), matching the
logging added to the base feature during review.

## Error handling

- Wi-Fi already up on entering `DETAIL` (it was needed for the list fetch);
  if it dropped, the lookup fails and shows `Type: unavailable` — the detail
  screen still renders everything else and stays usable.
- adsbdb "unknown aircraft" is a normal outcome, not an error.
- Radar with zero matches renders rings, crosshair, and centre with the same
  "No flights within {radius}mi" message the list uses.
- Radar and list **share the existing `selectedIndex` member** rather than
  keeping separate cursors, so toggling between views preserves which
  aircraft is selected. It already resets to 0 on every successful fetch, so
  a refresh returning fewer aircraft cannot leave the selection out of range
  — an invariant that matters because `OpenSkyStatesParser::matchAt` is
  unchecked raw array access, as established in the base feature's review.

## Testing

Host unit tests, following the existing `test/geo_math` and
`test/opensky_states_parser` CMake pattern:

- **`test/geo_math`** (extend): `polarToScreen` for cardinal and
  intercardinal bearings, linear distance scaling, over-range clamping,
  zero-distance centre case; `headingTriangle` nose direction for each
  cardinal heading and vertex count/ordering.
- **`test/aircraft_info_parser`** (new): the real success payload captured
  from adsbdb, the `{"response":"unknown aircraft"}` string case, a truncated
  body, and a payload with fields missing — asserting `found` is accurate and
  no buffer overruns.

No automated tests for the render path or the activity state machine — no
precedent for that in this codebase. Verified on hardware.

## Out of scope

- Aircraft photos (`url_photo` is in the adsbdb response; no image budget or
  benefit on 1-bit e-ink).
- Owner/operator name (available, but the screen is narrow; type and
  registration carry the interest).
- Prefetching types for the whole list, for the TLS cost reason above.
- Auto-zoom or a zoomable radar scale; rings are fixed to the configured
  radius.
- Decluttering overlapping marks. In busy airspace 20 triangles can overlap;
  only the selected one is labelled. The test location consistently returns
  67 aircraft in a 60-mile box against a 20-match cap, so overlap is expected
  and accepted.
- Restoring an "Up" button, per the buttons section above.
