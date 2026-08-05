# Nearby Flights Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a "Nearby Flights" screen to this personal X4 fork that fetches aircraft near a configured home location from OpenSky Network's free, anonymous API and displays them, with a per-flight detail view.

**Architecture:** A pure-math `GeoMath` lib (distance/bearing/bounding-box), a `StreamingJsonParser`-based `OpenSkyStatesParser` that keeps a fixed 20-entry sorted match list in O(1) memory, a thin `OpenSkyClient` network wrapper, three new `CrossPointSettings` fields (home lat/lon as text, radius as uint8_t) persisted through the existing generic settings loop, a `FlightTrackerSettingsActivity` for editing them, a `NearbyFlightsActivity` state machine modeled directly on `OpdsBookBrowserActivity`, and a Home menu entry.

**Tech Stack:** C++20 (Arduino/ESP-IDF via pioarduino), ArduinoJson is NOT used here (hand-rolled `StreamingJsonParser` instead, matching the codebase's existing OTA-release-parsing pattern), GoogleTest for host-side unit tests via CMake/CTest.

## Global Constraints

- Target device: Xteink X4 only (ESP32-C3, ~380KB RAM, no PSRAM, one 48KB framebuffer). See `.claude/skills/heap-discipline`.
- No heap allocation for the parsed flight list: fixed-size arrays/structs only, sized at compile time (`OpenSkyStatesParser::MAX_MATCHES = 20`). No bare `new`; this feature doesn't need `makeUniqueNoThrow` either since nothing here needs dynamic sizing.
- Every user-facing string goes through `tr(STR_*)`; the key must be added to `lib/I18n/translations/english.yaml` in the same change as its first use (gen_i18n.py hard-fails the build otherwise). See `.claude/skills/hal-and-abstractions`.
- Follow existing patterns exactly rather than inventing new ones: `Activity` state-machine shape from `OpdsBookBrowserActivity`, SAX JSON parsing shape from `ReleaseJsonParser`/`StreamingJsonParser`, settings persistence via the existing generic `SettingInfo`/`getSettingsList()` loop (category-less entries persist but don't appear in the generic Settings UI, same as `opdsDownloadFolder`).
- Enum-driven state machines use exhaustive `switch` with no `default`. See `.claude/skills/control-flow-clarity`.
- Host tests live under `test/<name>/`, registered via an explicit `add_subdirectory(<name>)` line in `test/CMakeLists.txt` (no globbing). Run via `pio run -t unit-tests`, or directly:
  ```bash
  cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release
  cmake --build build/test
  ctest --test-dir build/test --output-on-failure -j
  ```
- Firmware build/verification: `pio run` (full ESP32-C3 build). This may require the pioarduino toolchain to be installed/downloaded on first run — flag to the human tester if unavailable in the execution environment.
- This feature intentionally diverges from `SCOPE.md`'s "no new external network connectors" / "no active connectivity" rule. That's deliberate and already agreed with the user: this is a personal fork, not intended for upstream contribution (see `docs/superpowers/specs/2026-08-04-nearby-flights-design.md`). The engineering discipline in `SCOPE.md` (manual-refresh only, no background polling, bounded memory) is still followed.
- No new `UIIcon` glyph: reuse the existing `Wifi` icon for the Home menu entry rather than touching every theme's icon-draw table.

---

## Task 1: GeoMath library (distance, bearing, bounding box) + tests

**Files:**
- Create: `lib/Geo/GeoMath.h`
- Create: `lib/Geo/GeoMath.cpp`
- Create: `test/geo_math/CMakeLists.txt`
- Create: `test/geo_math/GeoMathTest.cpp`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Produces: `namespace GeoMath { double distanceMiles(double lat1Deg, double lon1Deg, double lat2Deg, double lon2Deg); double initialBearingDegrees(double lat1Deg, double lon1Deg, double lat2Deg, double lon2Deg); const char* compassPoint(double bearingDeg); struct BoundingBox { double latMin, lonMin, latMax, lonMax; }; BoundingBox computeBoundingBox(double lat, double lon, double radiusMiles); }` — used by `OpenSkyStatesParser` (Task 2) and `OpenSkyClient` (Task 3).

- [ ] **Step 1: Write the failing tests**

Create `test/geo_math/GeoMathTest.cpp`:

```cpp
#include <gtest/gtest.h>

#include "lib/Geo/GeoMath.h"

TEST(GeoMath, DistanceZeroForSamePoint) {
  EXPECT_NEAR(GeoMath::distanceMiles(37.6213, -122.3790, 37.6213, -122.3790), 0.0, 1e-6);
}

TEST(GeoMath, DistanceOneDegreeLatitudeConstantAtEquator) {
  EXPECT_NEAR(GeoMath::distanceMiles(0.0, 0.0, 1.0, 0.0), 69.09, 0.1);
}

TEST(GeoMath, DistanceOneDegreeLatitudeConstantAtMidLatitude) {
  // A degree of latitude is ~constant everywhere (meridians are great circles),
  // unlike a degree of longitude, which shrinks away from the equator.
  EXPECT_NEAR(GeoMath::distanceMiles(37.0, -122.0, 38.0, -122.0), 69.09, 0.15);
}

TEST(GeoMath, DistanceOneDegreeLongitudeAtEquator) {
  EXPECT_NEAR(GeoMath::distanceMiles(0.0, 0.0, 0.0, 1.0), 69.09, 0.1);
}

TEST(GeoMath, DistanceIsSymmetric) {
  const double ab = GeoMath::distanceMiles(37.6213, -122.3790, 40.6413, -73.7781);
  const double ba = GeoMath::distanceMiles(40.6413, -73.7781, 37.6213, -122.3790);
  EXPECT_NEAR(ab, ba, 1e-6);
}

TEST(GeoMath, BearingDueNorth) {
  EXPECT_NEAR(GeoMath::initialBearingDegrees(0.0, 0.0, 1.0, 0.0), 0.0, 0.5);
}

TEST(GeoMath, BearingDueEast) {
  EXPECT_NEAR(GeoMath::initialBearingDegrees(0.0, 0.0, 0.0, 1.0), 90.0, 0.5);
}

TEST(GeoMath, BearingDueSouth) {
  EXPECT_NEAR(GeoMath::initialBearingDegrees(0.0, 0.0, -1.0, 0.0), 180.0, 0.5);
}

TEST(GeoMath, BearingDueWest) {
  EXPECT_NEAR(GeoMath::initialBearingDegrees(0.0, 0.0, 0.0, -1.0), 270.0, 0.5);
}

TEST(GeoMath, CompassPointCardinals) {
  EXPECT_STREQ(GeoMath::compassPoint(0.0), "N");
  EXPECT_STREQ(GeoMath::compassPoint(90.0), "E");
  EXPECT_STREQ(GeoMath::compassPoint(180.0), "S");
  EXPECT_STREQ(GeoMath::compassPoint(270.0), "W");
}

TEST(GeoMath, CompassPointIntercardinals) {
  EXPECT_STREQ(GeoMath::compassPoint(45.0), "NE");
  EXPECT_STREQ(GeoMath::compassPoint(135.0), "SE");
  EXPECT_STREQ(GeoMath::compassPoint(225.0), "SW");
  EXPECT_STREQ(GeoMath::compassPoint(315.0), "NW");
}

TEST(GeoMath, CompassPointWrapsNearZero) {
  EXPECT_STREQ(GeoMath::compassPoint(350.0), "N");
  EXPECT_STREQ(GeoMath::compassPoint(-10.0), "N");
}

TEST(GeoMath, BoundingBoxCoversHomeLocation) {
  const auto box = GeoMath::computeBoundingBox(37.6213, -122.3790, 30.0);
  EXPECT_LT(box.latMin, 37.6213);
  EXPECT_GT(box.latMax, 37.6213);
  EXPECT_LT(box.lonMin, -122.3790);
  EXPECT_GT(box.lonMax, -122.3790);
}

TEST(GeoMath, BoundingBoxLatitudeSpanMatchesRadius) {
  const auto box = GeoMath::computeBoundingBox(0.0, 0.0, 69.09);
  EXPECT_NEAR(box.latMax - box.latMin, 2.0, 0.05);  // ~1 degree each direction
}

TEST(GeoMath, BoundingBoxWidensLongitudeSpanAwayFromEquator) {
  const auto equatorBox = GeoMath::computeBoundingBox(0.0, 0.0, 50.0);
  const auto highLatBox = GeoMath::computeBoundingBox(60.0, 0.0, 50.0);
  const double equatorLonSpan = equatorBox.lonMax - equatorBox.lonMin;
  const double highLatLonSpan = highLatBox.lonMax - highLatBox.lonMin;
  EXPECT_GT(highLatLonSpan, equatorLonSpan);
}
```

Create `test/geo_math/CMakeLists.txt`:

```cmake
add_executable(GeoMathTest
  GeoMathTest.cpp
  ${REPO_ROOT}/lib/Geo/GeoMath.cpp
)

target_include_directories(GeoMathTest PRIVATE
  ${REPO_ROOT}/lib/Geo
)

target_link_libraries(GeoMathTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GeoMathTest)
```

Add to `test/CMakeLists.txt`, in the `add_subdirectory(...)` block (near the other entries):

```cmake
add_subdirectory(geo_math)
```

- [ ] **Step 2: Create the header so the test fails to link (not fails to compile)**

Create `lib/Geo/GeoMath.h`:

```cpp
#pragma once

// Great-circle distance/bearing/bounding-box helpers for the Nearby Flights
// feature. Pure math, no device dependencies -- safe to unit test on the host.
namespace GeoMath {

constexpr double EARTH_RADIUS_MILES = 3958.7613;

// Great-circle distance between two WGS84 points, in miles.
double distanceMiles(double lat1Deg, double lon1Deg, double lat2Deg, double lon2Deg);

// Initial bearing from (lat1,lon1) to (lat2,lon2), degrees clockwise from
// true north, normalized to [0, 360).
double initialBearingDegrees(double lat1Deg, double lon1Deg, double lat2Deg, double lon2Deg);

// 8-point compass label ("N", "NE", "E", "SE", "S", "SW", "W", "NW") for a
// bearing in degrees. Input is normalized internally, so any finite value works.
const char* compassPoint(double bearingDeg);

struct BoundingBox {
  double latMin;
  double lonMin;
  double latMax;
  double lonMax;
};

// Bounding box covering a circle of radiusMiles around (lat,lon), using an
// equirectangular approximation -- fine at these radii, not valid near the poles.
BoundingBox computeBoundingBox(double lat, double lon, double radiusMiles);

}  // namespace GeoMath
```

- [ ] **Step 3: Run the tests to verify they fail to build (no .cpp yet)**

Run: `cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release && cmake --build build/test`
Expected: FAIL — linker error, undefined references to `GeoMath::distanceMiles` etc.

- [ ] **Step 4: Write the implementation**

Create `lib/Geo/GeoMath.cpp`:

```cpp
#include "GeoMath.h"

#include <cmath>

namespace GeoMath {

namespace {
constexpr double DEG_TO_RAD = M_PI / 180.0;
constexpr double MILES_PER_DEGREE_LAT = 69.0;

double normalizeDegrees(double deg) {
  double d = std::fmod(deg, 360.0);
  if (d < 0) d += 360.0;
  return d;
}
}  // namespace

double distanceMiles(const double lat1Deg, const double lon1Deg, const double lat2Deg, const double lon2Deg) {
  const double lat1 = lat1Deg * DEG_TO_RAD;
  const double lat2 = lat2Deg * DEG_TO_RAD;
  const double dLat = (lat2Deg - lat1Deg) * DEG_TO_RAD;
  const double dLon = (lon2Deg - lon1Deg) * DEG_TO_RAD;

  const double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
                   std::cos(lat1) * std::cos(lat2) * std::sin(dLon / 2) * std::sin(dLon / 2);
  const double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
  return EARTH_RADIUS_MILES * c;
}

double initialBearingDegrees(const double lat1Deg, const double lon1Deg, const double lat2Deg,
                             const double lon2Deg) {
  const double lat1 = lat1Deg * DEG_TO_RAD;
  const double lat2 = lat2Deg * DEG_TO_RAD;
  const double dLon = (lon2Deg - lon1Deg) * DEG_TO_RAD;

  const double y = std::sin(dLon) * std::cos(lat2);
  const double x = std::cos(lat1) * std::sin(lat2) - std::sin(lat1) * std::cos(lat2) * std::cos(dLon);
  const double bearing = std::atan2(y, x) / DEG_TO_RAD;
  return normalizeDegrees(bearing);
}

const char* compassPoint(const double bearingDeg) {
  static constexpr const char* POINTS[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  const double normalized = normalizeDegrees(bearingDeg);
  const int index = static_cast<int>((normalized + 22.5) / 45.0) % 8;
  return POINTS[index];
}

BoundingBox computeBoundingBox(const double lat, const double lon, const double radiusMiles) {
  const double latSpan = radiusMiles / MILES_PER_DEGREE_LAT;

  double milesPerDegreeLon = MILES_PER_DEGREE_LAT * std::cos(lat * DEG_TO_RAD);
  if (milesPerDegreeLon < 1.0) milesPerDegreeLon = 1.0;  // guard near the poles
  const double lonSpan = radiusMiles / milesPerDegreeLon;

  return BoundingBox{lat - latSpan, lon - lonSpan, lat + latSpan, lon + lonSpan};
}

}  // namespace GeoMath
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build/test && ctest --test-dir build/test --output-on-failure -R GeoMath`
Expected: PASS, all `GeoMath.*` tests green.

- [ ] **Step 6: Commit**

```bash
git add lib/Geo/GeoMath.h lib/Geo/GeoMath.cpp test/geo_math test/CMakeLists.txt
git commit -m "feat: add GeoMath library for distance/bearing/bounding-box math"
```

---

## Task 2: OpenSkyStatesParser (streaming JSON consumer) + tests

**Files:**
- Create: `lib/JsonParser/OpenSkyStatesParser.h`
- Create: `lib/JsonParser/OpenSkyStatesParser.cpp`
- Create: `test/opensky_states_parser/CMakeLists.txt`
- Create: `test/opensky_states_parser/OpenSkyStatesParserTest.cpp`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `GeoMath::distanceMiles`, `GeoMath::initialBearingDegrees` (Task 1); `StreamingJsonParser`/`JsonCallbacks` (existing, `lib/JsonParser/StreamingJsonParser.h`).
- Produces: `struct FlightMatch { char icao24[7]; char callsign[9]; char originCountry[32]; double latitude; double longitude; bool hasAltitudeFeet; int32_t altitudeFeet; bool hasSpeedMph; int32_t speedMph; bool hasHeading; int32_t headingDeg; bool hasVerticalRate; float verticalRateMs; double distanceMiles; double bearingDeg; };` and `class OpenSkyStatesParser { public: static constexpr size_t MAX_MATCHES = 20; void reset(double homeLatitude, double homeLongitude, double radiusMiles); void feed(const char* data, size_t len); bool hasError() const; size_t matchCount() const; const FlightMatch& matchAt(size_t index) const; };` — consumed by `OpenSkyClient` (Task 3) and `NearbyFlightsActivity` (Task 6).

- [ ] **Step 1: Write the failing tests**

Create `test/opensky_states_parser/OpenSkyStatesParserTest.cpp`:

```cpp
#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "lib/JsonParser/OpenSkyStatesParser.h"

namespace {
// Home location: SFO. Radius covers the nearby match but excludes JFK.
constexpr double HOME_LAT = 37.6213;
constexpr double HOME_LON = -122.3790;
constexpr double RADIUS_MILES = 50.0;

void feedAll(OpenSkyStatesParser& parser, const std::string& json) {
  parser.feed(json.c_str(), json.size());
}
}  // namespace

TEST(OpenSkyStatesParser, DecodesAMatchingAirborneAircraft) {
  const std::string json = R"({
    "time": 1700000000,
    "states": [
      ["a835af","UAL123  ","United States",1699999999,1699999999,-122.2211,37.7213,1000.0,false,90.0,45.0,2.5,null,1050.0,"1200",false,0,0]
    ]
  })";

  OpenSkyStatesParser parser;
  parser.reset(HOME_LAT, HOME_LON, RADIUS_MILES);
  feedAll(parser, json);

  ASSERT_FALSE(parser.hasError());
  ASSERT_EQ(parser.matchCount(), 1u);

  const auto& m = parser.matchAt(0);
  EXPECT_STREQ(m.icao24, "a835af");
  EXPECT_STREQ(m.callsign, "UAL123");  // trailing spaces trimmed
  EXPECT_STREQ(m.originCountry, "United States");
  ASSERT_TRUE(m.hasAltitudeFeet);
  EXPECT_NEAR(m.altitudeFeet, 1050.0 * 3.28084, 1.0);  // prefers geo_altitude
  ASSERT_TRUE(m.hasSpeedMph);
  EXPECT_NEAR(m.speedMph, 90.0 * 2.23694, 1.0);
  ASSERT_TRUE(m.hasHeading);
  EXPECT_EQ(m.headingDeg, 45);
  ASSERT_TRUE(m.hasVerticalRate);
  EXPECT_NEAR(m.verticalRateMs, 2.5, 0.01);
  EXPECT_GT(m.distanceMiles, 0.0);
  EXPECT_LT(m.distanceMiles, RADIUS_MILES);
}

TEST(OpenSkyStatesParser, SkipsOnGroundAndNullCallsignDoesNotDesyncFields) {
  const std::string json = R"({
    "time": 1700000000,
    "states": [
      ["b123cd",null,"United States",1699999999,1699999999,-122.3890,37.6150,50.0,true,0.0,0.0,0.0,null,60.0,"1201",false,0,0]
    ]
  })";

  OpenSkyStatesParser parser;
  parser.reset(HOME_LAT, HOME_LON, RADIUS_MILES);
  feedAll(parser, json);

  ASSERT_FALSE(parser.hasError());
  EXPECT_EQ(parser.matchCount(), 0u);  // on_ground=true -> excluded
}

TEST(OpenSkyStatesParser, SkipsAircraftWithNullPosition) {
  const std::string json = R"({
    "time": 1700000000,
    "states": [
      ["c456ef","NOPOS   ","United States",null,1699999999,null,null,null,false,120.0,180.0,0.0,null,null,null,false,0,0]
    ]
  })";

  OpenSkyStatesParser parser;
  parser.reset(HOME_LAT, HOME_LON, RADIUS_MILES);
  feedAll(parser, json);

  ASSERT_FALSE(parser.hasError());
  EXPECT_EQ(parser.matchCount(), 0u);  // no lat/lon -> excluded
}

TEST(OpenSkyStatesParser, SkipsAircraftOutsideRadius) {
  const std::string json = R"({
    "time": 1700000000,
    "states": [
      ["d789gh","JBU456  ","United States",1699999999,1699999999,-73.7781,40.6413,3000.0,false,200.0,270.0,-1.0,null,3100.0,"1202",false,0,0]
    ]
  })";

  OpenSkyStatesParser parser;
  parser.reset(HOME_LAT, HOME_LON, RADIUS_MILES);  // JFK is ~2570mi from SFO
  feedAll(parser, json);

  ASSERT_FALSE(parser.hasError());
  EXPECT_EQ(parser.matchCount(), 0u);
}

TEST(OpenSkyStatesParser, MixedRowsOnlyKeepsTheValidMatch) {
  const std::string json = R"({
    "time": 1700000000,
    "states": [
      ["a835af","UAL123  ","United States",1699999999,1699999999,-122.2211,37.7213,1000.0,false,90.0,45.0,2.5,null,1050.0,"1200",false,0,0],
      ["b123cd",null,"United States",1699999999,1699999999,-122.3890,37.6150,50.0,true,0.0,0.0,0.0,null,60.0,"1201",false,0,0],
      ["c456ef","NOPOS   ","United States",null,1699999999,null,null,null,false,120.0,180.0,0.0,null,null,null,false,0,0],
      ["d789gh","JBU456  ","United States",1699999999,1699999999,-73.7781,40.6413,3000.0,false,200.0,270.0,-1.0,null,3100.0,"1202",false,0,0]
    ]
  })";

  OpenSkyStatesParser parser;
  parser.reset(HOME_LAT, HOME_LON, RADIUS_MILES);
  feedAll(parser, json);

  ASSERT_FALSE(parser.hasError());
  ASSERT_EQ(parser.matchCount(), 1u);
  EXPECT_STREQ(parser.matchAt(0).icao24, "a835af");
}

TEST(OpenSkyStatesParser, FeedInMultipleChunksProducesSameResult) {
  const std::string json = R"({
    "time": 1700000000,
    "states": [
      ["a835af","UAL123  ","United States",1699999999,1699999999,-122.2211,37.7213,1000.0,false,90.0,45.0,2.5,null,1050.0,"1200",false,0,0]
    ]
  })";

  OpenSkyStatesParser parser;
  parser.reset(HOME_LAT, HOME_LON, RADIUS_MILES);
  // Split mid-token to make sure chunk boundaries don't lose data.
  const size_t mid = json.size() / 2;
  parser.feed(json.c_str(), mid);
  parser.feed(json.c_str() + mid, json.size() - mid);

  ASSERT_FALSE(parser.hasError());
  ASSERT_EQ(parser.matchCount(), 1u);
  EXPECT_STREQ(parser.matchAt(0).icao24, "a835af");
}

TEST(OpenSkyStatesParser, KeepsOnlyClosestMaxMatchesSorted) {
  // Build MAX_MATCHES + 5 aircraft along the same meridian, spaced 1 degree
  // (~69mi) apart, well within a huge radius -- all should be candidates,
  // but only the MAX_MATCHES closest survive, sorted ascending by distance.
  std::string json = R"({"time": 1700000000, "states": [)";
  const int totalAircraft = static_cast<int>(OpenSkyStatesParser::MAX_MATCHES) + 5;
  for (int i = 0; i < totalAircraft; ++i) {
    char row[256];
    const double lat = HOME_LAT + 0.01 * (i + 1);  // each ~0.69mi further than the last
    snprintf(row, sizeof(row),
             "[\"%06x\",\"FLT%03d  \",\"United States\",1699999999,1699999999,%.4f,%.4f,1000.0,false,90.0,0.0,0.0,"
             "null,1050.0,\"1200\",false,0,0]",
             i, i, HOME_LON, lat);
    json += row;
    if (i + 1 < totalAircraft) json += ",";
  }
  json += "]}";

  OpenSkyStatesParser parser;
  parser.reset(HOME_LAT, HOME_LON, 1000.0);  // huge radius: every aircraft is a candidate
  feedAll(parser, json);

  ASSERT_FALSE(parser.hasError());
  EXPECT_EQ(parser.matchCount(), OpenSkyStatesParser::MAX_MATCHES);

  for (size_t i = 1; i < parser.matchCount(); ++i) {
    EXPECT_LE(parser.matchAt(i - 1).distanceMiles, parser.matchAt(i).distanceMiles);
  }
  // The closest aircraft (index 0, lat offset 0.01) must be kept.
  EXPECT_STREQ(parser.matchAt(0).icao24, "000000");
}
```

Create `test/opensky_states_parser/CMakeLists.txt`:

```cmake
add_executable(OpenSkyStatesParserTest
  OpenSkyStatesParserTest.cpp
  ${REPO_ROOT}/lib/JsonParser/OpenSkyStatesParser.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
  ${REPO_ROOT}/lib/Geo/GeoMath.cpp
)

target_include_directories(OpenSkyStatesParserTest PRIVATE
  ${REPO_ROOT}/lib/JsonParser
  ${REPO_ROOT}/lib/Geo
)

target_link_libraries(OpenSkyStatesParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(OpenSkyStatesParserTest)
```

Add to `test/CMakeLists.txt`:

```cmake
add_subdirectory(opensky_states_parser)
```

- [ ] **Step 2: Write the header**

Create `lib/JsonParser/OpenSkyStatesParser.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

#include "StreamingJsonParser.h"

// One matched aircraft: within the configured radius, airborne, position known.
struct FlightMatch {
  char icao24[7] = {0};
  char callsign[9] = {0};  // trimmed; may be empty if OpenSky has no callsign
  char originCountry[32] = {0};
  double latitude = 0;
  double longitude = 0;
  bool hasAltitudeFeet = false;
  int32_t altitudeFeet = 0;  // prefers geo_altitude, falls back to baro_altitude
  bool hasSpeedMph = false;
  int32_t speedMph = 0;
  bool hasHeading = false;
  int32_t headingDeg = 0;  // 0-359, true track
  bool hasVerticalRate = false;
  float verticalRateMs = 0;  // raw m/s; sign gives climb/level/descend
  double distanceMiles = 0;
  double bearingDeg = 0;  // initial bearing from home location, 0-359
};

// Streams OpenSky's /api/states/all response and keeps the MAX_MATCHES closest
// airborne aircraft within a radius of a configured home location, sorted by
// distance ascending. Memory use is fixed regardless of response size: one
// scratch row plus a fixed MAX_MATCHES-entry result array -- important on a
// device with ~380KB RAM and no PSRAM, where a busy airspace could otherwise
// mean an unbounded aircraft list.
class OpenSkyStatesParser {
 public:
  static constexpr size_t MAX_MATCHES = 20;

  OpenSkyStatesParser();

  OpenSkyStatesParser(const OpenSkyStatesParser&) = delete;
  OpenSkyStatesParser& operator=(const OpenSkyStatesParser&) = delete;

  // Resets all state and configures the search. Call before each fetch.
  void reset(double homeLatitude, double homeLongitude, double radiusMiles);

  // Feeds a chunk of the HTTP response body. Safe to call repeatedly as data streams in.
  void feed(const char* data, size_t len);

  bool hasError() const { return parser.hasError(); }

  size_t matchCount() const { return matchCount_; }
  const FlightMatch& matchAt(size_t index) const { return matches[index]; }

 private:
  struct RowScratch {
    char icao24[7] = {0};
    char callsign[9] = {0};
    char originCountry[32] = {0};
    bool hasLat = false;
    bool hasLon = false;
    double lat = 0;
    double lon = 0;
    bool onGround = false;
    bool hasBaroAlt = false;
    float baroAltM = 0;
    bool hasGeoAlt = false;
    float geoAltM = 0;
    bool hasVelocity = false;
    float velocityMs = 0;
    bool hasTrueTrack = false;
    float trueTrackDeg = 0;
    bool hasVerticalRate = false;
    float verticalRateMs = 0;
  };

  enum class Position : uint8_t { AWAITING_STATES, IN_STATES_ARRAY, IN_ROW };

  static void sOnKey(void* ctx, const char* key, size_t len);
  static void sOnString(void* ctx, const char* value, size_t len);
  static void sOnNumber(void* ctx, const char* value, size_t len);
  static void sOnBool(void* ctx, bool value);
  static void sOnNull(void* ctx);
  static void sOnArrayStart(void* ctx);
  static void sOnArrayEnd(void* ctx);

  void commitRow();
  void insertSorted(const FlightMatch& match);

  StreamingJsonParser parser;

  Position position = Position::AWAITING_STATES;
  bool expectStatesArray = false;
  uint8_t fieldIndex = 0;
  uint8_t nestedArrayDepth = 0;
  RowScratch scratch;

  double homeLat = 0;
  double homeLon = 0;
  double radiusMiles = 0;

  FlightMatch matches[MAX_MATCHES];
  size_t matchCount_ = 0;
};
```

- [ ] **Step 3: Run tests to verify they fail (no .cpp yet)**

Run: `cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release && cmake --build build/test`
Expected: FAIL — linker error, undefined references to `OpenSkyStatesParser` methods.

- [ ] **Step 4: Write the implementation**

Create `lib/JsonParser/OpenSkyStatesParser.cpp`:

```cpp
#include "OpenSkyStatesParser.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "GeoMath.h"

namespace {
constexpr double MPS_TO_MPH = 2.23694;
constexpr double METERS_TO_FEET = 3.28084;

void safeCopy(char* dst, size_t dstSize, const char* src, size_t srcLen) {
  const size_t n = srcLen < dstSize - 1 ? srcLen : dstSize - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

void trimTrailingSpaces(char* s) {
  size_t len = strlen(s);
  while (len > 0 && s[len - 1] == ' ') s[--len] = '\0';
}

double parseDouble(const char* value, size_t len) {
  char buf[32];
  const size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
  memcpy(buf, value, n);
  buf[n] = '\0';
  return strtod(buf, nullptr);
}
}  // namespace

OpenSkyStatesParser::OpenSkyStatesParser()
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, nullptr, nullptr, sOnArrayStart,
                           sOnArrayEnd}) {}

void OpenSkyStatesParser::reset(const double homeLatitude, const double homeLongitude, const double radius) {
  parser.reset();
  position = Position::AWAITING_STATES;
  expectStatesArray = false;
  fieldIndex = 0;
  nestedArrayDepth = 0;
  scratch = RowScratch{};
  homeLat = homeLatitude;
  homeLon = homeLongitude;
  radiusMiles = radius;
  matchCount_ = 0;
}

void OpenSkyStatesParser::feed(const char* data, size_t len) { parser.feed(data, len); }

void OpenSkyStatesParser::insertSorted(const FlightMatch& match) {
  if (matchCount_ < MAX_MATCHES) {
    size_t i = matchCount_;
    while (i > 0 && matches[i - 1].distanceMiles > match.distanceMiles) {
      matches[i] = matches[i - 1];
      --i;
    }
    matches[i] = match;
    ++matchCount_;
  } else if (match.distanceMiles < matches[MAX_MATCHES - 1].distanceMiles) {
    size_t i = MAX_MATCHES - 1;
    while (i > 0 && matches[i - 1].distanceMiles > match.distanceMiles) {
      matches[i] = matches[i - 1];
      --i;
    }
    matches[i] = match;
  }
}

void OpenSkyStatesParser::commitRow() {
  if (scratch.hasLat && scratch.hasLon && !scratch.onGround) {
    const double distance = GeoMath::distanceMiles(homeLat, homeLon, scratch.lat, scratch.lon);
    if (distance <= radiusMiles) {
      FlightMatch m{};
      safeCopy(m.icao24, sizeof(m.icao24), scratch.icao24, strlen(scratch.icao24));
      safeCopy(m.callsign, sizeof(m.callsign), scratch.callsign, strlen(scratch.callsign));
      trimTrailingSpaces(m.callsign);
      safeCopy(m.originCountry, sizeof(m.originCountry), scratch.originCountry, strlen(scratch.originCountry));
      m.latitude = scratch.lat;
      m.longitude = scratch.lon;

      m.hasAltitudeFeet = scratch.hasGeoAlt || scratch.hasBaroAlt;
      const float altMeters = scratch.hasGeoAlt ? scratch.geoAltM : scratch.baroAltM;
      m.altitudeFeet = m.hasAltitudeFeet ? static_cast<int32_t>(lroundf(altMeters * METERS_TO_FEET)) : 0;

      m.hasSpeedMph = scratch.hasVelocity;
      m.speedMph = m.hasSpeedMph ? static_cast<int32_t>(lroundf(scratch.velocityMs * MPS_TO_MPH)) : 0;

      m.hasHeading = scratch.hasTrueTrack;
      m.headingDeg =
          m.hasHeading ? ((static_cast<int32_t>(lroundf(scratch.trueTrackDeg)) % 360 + 360) % 360) : 0;

      m.hasVerticalRate = scratch.hasVerticalRate;
      m.verticalRateMs = scratch.verticalRateMs;

      m.distanceMiles = distance;
      m.bearingDeg = GeoMath::initialBearingDegrees(homeLat, homeLon, scratch.lat, scratch.lon);

      insertSorted(m);
    }
  }
  scratch = RowScratch{};
}

void OpenSkyStatesParser::sOnKey(void* ctx, const char* key, size_t len) {
  auto* self = static_cast<OpenSkyStatesParser*>(ctx);
  if (self->position == Position::AWAITING_STATES) {
    self->expectStatesArray = (len == 6 && memcmp(key, "states", 6) == 0);
  }
}

void OpenSkyStatesParser::sOnArrayStart(void* ctx) {
  auto* self = static_cast<OpenSkyStatesParser*>(ctx);
  switch (self->position) {
    case Position::AWAITING_STATES:
      if (self->expectStatesArray) {
        self->position = Position::IN_STATES_ARRAY;
        self->expectStatesArray = false;
      }
      break;
    case Position::IN_STATES_ARRAY:
      self->position = Position::IN_ROW;
      self->fieldIndex = 0;
      self->nestedArrayDepth = 0;
      self->scratch = RowScratch{};
      break;
    case Position::IN_ROW:
      // Nested array within a row (OpenSky's "sensors" field, index 12).
      // Reserve its field-index slot once, then ignore its contents.
      if (self->nestedArrayDepth == 0) self->fieldIndex++;
      self->nestedArrayDepth++;
      break;
  }
}

void OpenSkyStatesParser::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<OpenSkyStatesParser*>(ctx);
  switch (self->position) {
    case Position::AWAITING_STATES:
      break;  // stray top-level array; not part of this schema
    case Position::IN_STATES_ARRAY:
      self->position = Position::AWAITING_STATES;
      break;
    case Position::IN_ROW:
      if (self->nestedArrayDepth > 0) {
        self->nestedArrayDepth--;
      } else {
        self->commitRow();
        self->position = Position::IN_STATES_ARRAY;
      }
      break;
  }
}

void OpenSkyStatesParser::sOnString(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<OpenSkyStatesParser*>(ctx);
  if (self->position != Position::IN_ROW || self->nestedArrayDepth > 0) return;
  switch (self->fieldIndex) {
    case 0:  // icao24
      safeCopy(self->scratch.icao24, sizeof(self->scratch.icao24), value, len);
      break;
    case 1:  // callsign
      safeCopy(self->scratch.callsign, sizeof(self->scratch.callsign), value, len);
      break;
    case 2:  // origin_country
      safeCopy(self->scratch.originCountry, sizeof(self->scratch.originCountry), value, len);
      break;
    default:
      break;
  }
  self->fieldIndex++;
}

void OpenSkyStatesParser::sOnNumber(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<OpenSkyStatesParser*>(ctx);
  if (self->position != Position::IN_ROW || self->nestedArrayDepth > 0) return;
  switch (self->fieldIndex) {
    case 5:  // longitude
      self->scratch.lon = parseDouble(value, len);
      self->scratch.hasLon = true;
      break;
    case 6:  // latitude
      self->scratch.lat = parseDouble(value, len);
      self->scratch.hasLat = true;
      break;
    case 7:  // baro_altitude (meters)
      self->scratch.baroAltM = static_cast<float>(parseDouble(value, len));
      self->scratch.hasBaroAlt = true;
      break;
    case 9:  // velocity (m/s)
      self->scratch.velocityMs = static_cast<float>(parseDouble(value, len));
      self->scratch.hasVelocity = true;
      break;
    case 10:  // true_track (degrees)
      self->scratch.trueTrackDeg = static_cast<float>(parseDouble(value, len));
      self->scratch.hasTrueTrack = true;
      break;
    case 11:  // vertical_rate (m/s)
      self->scratch.verticalRateMs = static_cast<float>(parseDouble(value, len));
      self->scratch.hasVerticalRate = true;
      break;
    case 13:  // geo_altitude (meters)
      self->scratch.geoAltM = static_cast<float>(parseDouble(value, len));
      self->scratch.hasGeoAlt = true;
      break;
    default:
      break;
  }
  self->fieldIndex++;
}

void OpenSkyStatesParser::sOnBool(void* ctx, bool value) {
  auto* self = static_cast<OpenSkyStatesParser*>(ctx);
  if (self->position != Position::IN_ROW || self->nestedArrayDepth > 0) return;
  if (self->fieldIndex == 8) self->scratch.onGround = value;  // on_ground
  self->fieldIndex++;
}

void OpenSkyStatesParser::sOnNull(void* ctx) {
  auto* self = static_cast<OpenSkyStatesParser*>(ctx);
  if (self->position != Position::IN_ROW || self->nestedArrayDepth > 0) return;
  // A null just means "field absent" -- the corresponding hasX flag in
  // scratch was already false from the row reset, so there's nothing to set.
  self->fieldIndex++;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build build/test && ctest --test-dir build/test --output-on-failure -R OpenSkyStatesParser`
Expected: PASS, all `OpenSkyStatesParser.*` tests green.

- [ ] **Step 6: Commit**

```bash
git add lib/JsonParser/OpenSkyStatesParser.h lib/JsonParser/OpenSkyStatesParser.cpp test/opensky_states_parser test/CMakeLists.txt
git commit -m "feat: add OpenSkyStatesParser for bounded-memory states/all parsing"
```

---

## Task 3: OpenSkyClient (network wrapper)

**Files:**
- Create: `src/network/OpenSkyClient.h`
- Create: `src/network/OpenSkyClient.cpp`

**Interfaces:**
- Consumes: `GeoMath::computeBoundingBox` (Task 1), `OpenSkyStatesParser` (Task 2), `HttpDownloader::fetchUrl(const std::string&, const DataCallback&, ...)` (existing, `src/network/HttpDownloader.h`).
- Produces: `class OpenSkyClient { public: static bool fetchNearby(double homeLat, double homeLon, double radiusMiles, OpenSkyStatesParser& parser); };` — consumed by `NearbyFlightsActivity` (Task 6).

No host unit test for this task: it depends on `HttpDownloader.h`, which pulls in ESP-IDF/Arduino headers (`HalStorage.h`) not available in the host toolchain — matching the existing codebase convention that `HttpDownloader` itself has no host test. The pure bounding-box math it depends on is already tested in Task 1. Verified via firmware build + on-device manual test (Task 6/7).

- [ ] **Step 1: Write the implementation**

Create `src/network/OpenSkyClient.h`:

```cpp
#pragma once

#include "OpenSkyStatesParser.h"

// Fetches aircraft near a home location from OpenSky Network's free,
// anonymous /api/states/all endpoint and streams them into a parser.
class OpenSkyClient {
 public:
  // parser must already be reset() with the same home location/radius before
  // calling. Returns false on HTTP/transport failure; check parser.hasError()
  // separately for JSON parse failures (a false return does not distinguish
  // the two -- see HttpDownloader::fetchUrl's DataCallback overload).
  static bool fetchNearby(double homeLat, double homeLon, double radiusMiles, OpenSkyStatesParser& parser);
};
```

Create `src/network/OpenSkyClient.cpp`:

```cpp
#include "OpenSkyClient.h"

#include <cstdio>
#include <string>

#include "GeoMath.h"
#include "network/HttpDownloader.h"

bool OpenSkyClient::fetchNearby(const double homeLat, const double homeLon, const double radiusMiles,
                                OpenSkyStatesParser& parser) {
  const auto box = GeoMath::computeBoundingBox(homeLat, homeLon, radiusMiles);

  char url[192];
  snprintf(url, sizeof(url),
           "https://opensky-network.org/api/states/all?lamin=%.4f&lomin=%.4f&lamax=%.4f&lomax=%.4f", box.latMin,
           box.lonMin, box.latMax, box.lonMax);

  return HttpDownloader::fetchUrl(std::string(url), [&parser](const uint8_t* data, size_t len) {
    parser.feed(reinterpret_cast<const char*>(data), len);
    return !parser.hasError();
  });
}
```

- [ ] **Step 2: Verify it compiles as part of the firmware**

Run: `pio run`
Expected: builds cleanly (this file isn't referenced by any activity yet, so it just needs to compile standalone — PlatformIO compiles every `.cpp` under `src/`).

- [ ] **Step 3: Commit**

```bash
git add src/network/OpenSkyClient.h src/network/OpenSkyClient.cpp
git commit -m "feat: add OpenSkyClient to fetch nearby aircraft from OpenSky Network"
```

---

## Task 4: CrossPointSettings fields for home location and radius

**Files:**
- Modify: `src/CrossPointSettings.h`
- Modify: `src/SettingsList.h`
- Modify: `lib/I18n/translations/english.yaml`

**Interfaces:**
- Produces: `char CrossPointSettings::flightTrackerHomeLat[16]`, `char CrossPointSettings::flightTrackerHomeLon[16]` (both `""` = unset, decimal-degree text), `uint8_t CrossPointSettings::flightTrackerRadiusMiles` (default 30), plus `CrossPointSettings::FLIGHT_TRACKER_RADIUS_MIN/MAX/STEP` — consumed by `FlightTrackerSettingsActivity` (Task 5) and `NearbyFlightsActivity` (Task 6).

No dedicated host test: `CrossPointSettings` itself has no test suite in this codebase (verified via `test/` directory listing — persistence for existing fields is exercised only by the firmware build + manual on-device check, and this task follows the exact same generic `SettingInfo` mechanism those fields already use). Verified via firmware build.

- [ ] **Step 1: Add the fields to CrossPointSettings.h**

Open `src/CrossPointSettings.h`, find the `char opdsDownloadFolder[64] = "";` field (with its preceding comment), and add immediately after it:

```cpp
  // Flight tracker home location, decimal degrees as text ("" = unset). Manually
  // edited via FlightTrackerSettingsActivity; kept out of the generic Settings UI
  // (category-less), same pattern as opdsDownloadFolder.
  char flightTrackerHomeLat[16] = "";
  char flightTrackerHomeLon[16] = "";
  static constexpr uint8_t FLIGHT_TRACKER_RADIUS_MIN = 5;
  static constexpr uint8_t FLIGHT_TRACKER_RADIUS_MAX = 200;
  static constexpr uint8_t FLIGHT_TRACKER_RADIUS_STEP = 5;
  uint8_t flightTrackerRadiusMiles = 30;
```

- [ ] **Step 2: Add the i18n keys these settings will reference**

Open `lib/I18n/translations/english.yaml` and add (near other settings-label keys, alphabetical grouping isn't enforced elsewhere in the file, so append near related entries is fine):

```yaml
STR_FLIGHT_TRACKER_HOME_LAT: "Home Latitude"
STR_FLIGHT_TRACKER_HOME_LON: "Home Longitude"
STR_FLIGHT_TRACKER_RADIUS: "Search Radius (mi)"
```

- [ ] **Step 3: Register the settings for persistence**

Open `src/SettingsList.h`, find the existing entry:

```cpp
        SettingInfo::String(StrId::STR_OPDS_DOWNLOAD_FOLDER, &SETTINGS.opdsDownloadFolder[0],
                            sizeof(SETTINGS.opdsDownloadFolder), "opdsDownloadFolder"),
```

and add immediately after it (still category-less, so it persists via the generic `toJson`/`fromJson` loop but doesn't appear in the generic on-device Settings screen — it's edited via `FlightTrackerSettingsActivity`, Task 5):

```cpp
        SettingInfo::String(StrId::STR_FLIGHT_TRACKER_HOME_LAT, &SETTINGS.flightTrackerHomeLat[0],
                            sizeof(SETTINGS.flightTrackerHomeLat), "flightTrackerHomeLat"),
        SettingInfo::String(StrId::STR_FLIGHT_TRACKER_HOME_LON, &SETTINGS.flightTrackerHomeLon[0],
                            sizeof(SETTINGS.flightTrackerHomeLon), "flightTrackerHomeLon"),
        SettingInfo::Value(StrId::STR_FLIGHT_TRACKER_RADIUS, &CrossPointSettings::flightTrackerRadiusMiles,
                           {CrossPointSettings::FLIGHT_TRACKER_RADIUS_MIN, CrossPointSettings::FLIGHT_TRACKER_RADIUS_MAX,
                            CrossPointSettings::FLIGHT_TRACKER_RADIUS_STEP},
                           "flightTrackerRadiusMiles"),
```

- [ ] **Step 4: Verify the firmware builds**

Run: `pio run`
Expected: builds cleanly. `gen_i18n.py` runs as part of this build and will hard-fail if any `STR_FLIGHT_TRACKER_*` key were referenced without a YAML entry — this step also validates Step 2 was done correctly.

- [ ] **Step 5: Commit**

```bash
git add src/CrossPointSettings.h src/SettingsList.h lib/I18n/translations/english.yaml
git commit -m "feat: add flight tracker home location and radius settings"
```

---

## Task 5: FlightTrackerSettingsActivity + Settings menu wiring

**Files:**
- Create: `src/activities/settings/FlightTrackerSettingsActivity.h`
- Create: `src/activities/settings/FlightTrackerSettingsActivity.cpp`
- Modify: `src/activities/settings/SettingsActivity.h`
- Modify: `src/activities/settings/SettingsActivity.cpp`
- Modify: `lib/I18n/translations/english.yaml`

**Interfaces:**
- Consumes: `CrossPointSettings::flightTrackerHomeLat/Lon/RadiusMiles` (Task 4), `KeyboardEntryActivity` (existing, `src/activities/util/KeyboardEntryActivity.h`), `ButtonNavigator` (existing, `src/util/ButtonNavigator.h`).
- Produces: `class FlightTrackerSettingsActivity final : public Activity { explicit FlightTrackerSettingsActivity(GfxRenderer&, MappedInputManager&); };` reachable from Settings > System > "Flight Tracker".

No host test: `Activity` subclasses aren't unit tested anywhere in this codebase (confirmed — no `test/` directory covers any `Activity`). Verified via firmware build + manual on-device check (flagged below).

- [ ] **Step 1: Add the "Flight Tracker" i18n key**

Add to `lib/I18n/translations/english.yaml`:

```yaml
STR_FLIGHT_TRACKER: "Flight Tracker"
```

- [ ] **Step 2: Write FlightTrackerSettingsActivity.h**

Create `src/activities/settings/FlightTrackerSettingsActivity.h`:

```cpp
#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Three-row settings screen: home latitude, home longitude (both free-text
// decimal degrees via the keyboard), and search radius (tap-to-cycle).
class FlightTrackerSettingsActivity final : public Activity {
 public:
  explicit FlightTrackerSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FlightTrackerSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr int ITEM_COUNT = 3;

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;

  void handleSelection();
};
```

- [ ] **Step 3: Write FlightTrackerSettingsActivity.cpp**

Create `src/activities/settings/FlightTrackerSettingsActivity.cpp`:

```cpp
#include "FlightTrackerSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
bool parseCoordinate(const std::string& text, double minValue, double maxValue, double& outValue) {
  if (text.empty()) return false;
  char* end = nullptr;
  const double value = strtod(text.c_str(), &end);
  if (end == text.c_str() || *end != '\0') return false;
  if (value < minValue || value > maxValue) return false;
  outValue = value;
  return true;
}
}  // namespace

void FlightTrackerSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  requestUpdate();
}

void FlightTrackerSettingsActivity::onExit() { Activity::onExit(); }

void FlightTrackerSettingsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  switch (handleListTouch(selectedIndex, ITEM_COUNT, contentTop, contentHeight, false)) {
    case ListTouchResult::Activated:
      handleSelection();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
  }

  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, ITEM_COUNT);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, ITEM_COUNT);
    requestUpdate();
  });
}

void FlightTrackerSettingsActivity::handleSelection() {
  if (selectedIndex == 0) {
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        double parsed = 0;
        if (parseCoordinate(kb.text, -90.0, 90.0, parsed)) {
          strncpy(SETTINGS.flightTrackerHomeLat, kb.text.c_str(), sizeof(SETTINGS.flightTrackerHomeLat) - 1);
          SETTINGS.flightTrackerHomeLat[sizeof(SETTINGS.flightTrackerHomeLat) - 1] = '\0';
          SETTINGS.saveToFile();
        }
      }
      requestUpdate();
    };
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_FLIGHT_TRACKER_HOME_LAT),
                                                std::string(SETTINGS.flightTrackerHomeLat), 15, InputType::Text),
        handler);
    return;
  }

  if (selectedIndex == 1) {
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        double parsed = 0;
        if (parseCoordinate(kb.text, -180.0, 180.0, parsed)) {
          strncpy(SETTINGS.flightTrackerHomeLon, kb.text.c_str(), sizeof(SETTINGS.flightTrackerHomeLon) - 1);
          SETTINGS.flightTrackerHomeLon[sizeof(SETTINGS.flightTrackerHomeLon) - 1] = '\0';
          SETTINGS.saveToFile();
        }
      }
      requestUpdate();
    };
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_FLIGHT_TRACKER_HOME_LON),
                                                std::string(SETTINGS.flightTrackerHomeLon), 15, InputType::Text),
        handler);
    return;
  }

  // Search radius: tap cycles through the allowed range.
  const uint8_t next = SETTINGS.flightTrackerRadiusMiles + CrossPointSettings::FLIGHT_TRACKER_RADIUS_STEP;
  SETTINGS.flightTrackerRadiusMiles =
      next > CrossPointSettings::FLIGHT_TRACKER_RADIUS_MAX ? CrossPointSettings::FLIGHT_TRACKER_RADIUS_MIN : next;
  SETTINGS.saveToFile();
  requestUpdate();
}

void FlightTrackerSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_FLIGHT_TRACKER));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, ITEM_COUNT, selectedIndex,
      [](int index) -> std::string {
        switch (index) {
          case 0:
            return I18n::getInstance().get(StrId::STR_FLIGHT_TRACKER_HOME_LAT);
          case 1:
            return I18n::getInstance().get(StrId::STR_FLIGHT_TRACKER_HOME_LON);
          default:
            return I18n::getInstance().get(StrId::STR_FLIGHT_TRACKER_RADIUS);
        }
      },
      nullptr, nullptr,
      [](int index) -> std::string {
        switch (index) {
          case 0:
            return SETTINGS.flightTrackerHomeLat[0] ? std::string(SETTINGS.flightTrackerHomeLat)
                                                     : std::string(I18n::getInstance().get(StrId::STR_NOT_SET));
          case 1:
            return SETTINGS.flightTrackerHomeLon[0] ? std::string(SETTINGS.flightTrackerHomeLon)
                                                     : std::string(I18n::getInstance().get(StrId::STR_NOT_SET));
          default: {
            char buf[8];
            snprintf(buf, sizeof(buf), "%u", SETTINGS.flightTrackerRadiusMiles);
            return std::string(buf);
          }
        }
      });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
```

- [ ] **Step 4: Wire into SettingsActivity**

Open `src/activities/settings/SettingsActivity.h`, find the `SettingAction` enum:

```cpp
enum class SettingAction { None, RemapFrontButtons, CustomiseStatusBar, KOReaderSync, OPDSBrowser, Network,
                           ClearCache, CheckForUpdates, SdFirmwareUpdate, Language, DownloadFonts, TextSettings };
```

and add `FlightTracker` to it:

```cpp
enum class SettingAction { None, RemapFrontButtons, CustomiseStatusBar, KOReaderSync, OPDSBrowser, Network,
                           ClearCache, CheckForUpdates, SdFirmwareUpdate, Language, DownloadFonts, TextSettings,
                           FlightTracker };
```

Open `src/activities/settings/SettingsActivity.cpp`, add the include near the other activity includes:

```cpp
#include "FlightTrackerSettingsActivity.h"
```

Find:

```cpp
  systemSettings.push_back(SettingInfo::Action(StrId::STR_WIFI_NETWORKS, SettingAction::Network));
```

and add immediately after it:

```cpp
  systemSettings.push_back(SettingInfo::Action(StrId::STR_FLIGHT_TRACKER, SettingAction::FlightTracker));
```

Find the dispatch switch's `case SettingAction::Network:` block:

```cpp
    case SettingAction::Network:
      startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, false), resultHandler);
      break;
```

and add immediately after it:

```cpp
    case SettingAction::FlightTracker:
      startActivityForResult(std::make_unique<FlightTrackerSettingsActivity>(renderer, mappedInput), resultHandler);
      break;
```

- [ ] **Step 5: Verify the firmware builds**

Run: `pio run`
Expected: builds cleanly.

- [ ] **Step 6: Manual on-device verification (flag for human tester)**

🔲 **Device**: Flash to the X4, go to Settings > System > "Flight Tracker", set a home latitude/longitude (e.g. `37.6213` / `-122.3790`) and cycle the radius, back out, re-enter Settings and confirm the values persisted. Confirm invalid input (e.g. letters, or `999`) is rejected and the previous value is kept.

- [ ] **Step 7: Commit**

```bash
git add src/activities/settings/FlightTrackerSettingsActivity.h src/activities/settings/FlightTrackerSettingsActivity.cpp src/activities/settings/SettingsActivity.h src/activities/settings/SettingsActivity.cpp lib/I18n/translations/english.yaml
git commit -m "feat: add Flight Tracker settings screen for home location and radius"
```

---

## Task 6: NearbyFlightsActivity

**Files:**
- Create: `src/activities/flights/NearbyFlightsActivity.h`
- Create: `src/activities/flights/NearbyFlightsActivity.cpp`
- Modify: `lib/I18n/translations/english.yaml`

**Interfaces:**
- Consumes: `OpenSkyStatesParser`, `FlightMatch` (Task 2), `OpenSkyClient::fetchNearby` (Task 3), `CrossPointSettings::flightTrackerHomeLat/Lon/RadiusMiles` (Task 4), `GeoMath::compassPoint` (Task 1), `WifiSelectionActivity`/`KeyboardResult`-style `ActivityResult` flow, `SilentRestart.h::silentRestart()` (all existing).
- Produces: `class NearbyFlightsActivity final : public Activity { explicit NearbyFlightsActivity(GfxRenderer&, MappedInputManager&); };` — consumed by Home menu wiring (Task 7). Requires `HomeMenuItem::NEARBY_FLIGHTS` to exist (added in Task 7) for its `onGoHome(...)` calls — **Task 7's `HomeMenuItem` enum change must land before this task compiles**, so implement Task 7's enum addition first if doing these out of order, or treat Tasks 6 and 7 as one combined commit if strict incremental compilation per task matters less than usual here.

No host test: `Activity` subclasses aren't unit tested anywhere in this codebase. Verified via firmware build + manual on-device check (flagged below).

- [ ] **Step 1: Add the i18n keys this activity needs**

Add to `lib/I18n/translations/english.yaml`:

```yaml
STR_NEARBY_FLIGHTS: "Nearby Flights"
STR_SET_HOME_LOCATION_FIRST: "Set a home location in Settings to see nearby flights"
STR_NO_FLIGHTS_FORMAT: "No flights within %d mi"
STR_UNKNOWN_CALLSIGN: "Unknown"
STR_FLIGHT_DISTANCE_FORMAT: "%.1f mi %s"
STR_REFRESH: "Refresh"
STR_FETCH_FLIGHTS_FAILED: "Failed to fetch flight data"
STR_FLIGHT_ALTITUDE_FORMAT: "Altitude: %ld ft"
STR_FLIGHT_SPEED_FORMAT: "Speed: %ld mph"
STR_FLIGHT_HEADING_FORMAT: "Heading: %ld° %s"
STR_FLIGHT_CLIMBING: "Climbing"
STR_FLIGHT_DESCENDING: "Descending"
STR_FLIGHT_LEVEL: "Level flight"
STR_FLIGHT_ORIGIN_FORMAT: "Origin: %s"
STR_FLIGHT_ICAO24_FORMAT: "ICAO24: %s"
STR_FLIGHT_DATA_AGE_FORMAT: "As of %lus ago"
```

- [ ] **Step 2: Write NearbyFlightsActivity.h**

Create `src/activities/flights/NearbyFlightsActivity.h`:

```cpp
#pragma once

#include "OpenSkyStatesParser.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class NearbyFlightsActivity final : public Activity {
 public:
  enum class FlightsState { NO_LOCATION, CHECK_WIFI, WIFI_SELECTION, LOADING, LIST, DETAIL, ERROR };

  explicit NearbyFlightsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("NearbyFlights", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  bool preventAutoSleep() override { return true; }

  ButtonNavigator buttonNavigator;
  FlightsState state = FlightsState::NO_LOCATION;
  OpenSkyStatesParser parser;
  int selectedIndex = 0;
  std::string errorMessage;
  unsigned long fetchCompletedMs = 0;

  bool parseHomeLocation(double& lat, double& lon) const;
  void checkAndConnectWifi();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void fetchFlights();

  void renderNoLocation() const;
  void renderCheckWifiOrLoading() const;
  void renderError() const;
  void renderList();
  void renderDetail() const;
};
```

- [ ] **Step 3: Write NearbyFlightsActivity.cpp**

Create `src/activities/flights/NearbyFlightsActivity.cpp`:

```cpp
#include "NearbyFlightsActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include <cstdio>
#include <cstdlib>

#include "CrossPointSettings.h"
#include "GeoMath.h"
#include "MappedInputManager.h"
#include "OpenSkyClient.h"
#include "SilentRestart.h"
#include "activities/ActivityManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

bool NearbyFlightsActivity::parseHomeLocation(double& lat, double& lon) const {
  if (SETTINGS.flightTrackerHomeLat[0] == '\0' || SETTINGS.flightTrackerHomeLon[0] == '\0') return false;
  char* latEnd = nullptr;
  char* lonEnd = nullptr;
  const double parsedLat = strtod(SETTINGS.flightTrackerHomeLat, &latEnd);
  const double parsedLon = strtod(SETTINGS.flightTrackerHomeLon, &lonEnd);
  if (latEnd == SETTINGS.flightTrackerHomeLat || *latEnd != '\0') return false;
  if (lonEnd == SETTINGS.flightTrackerHomeLon || *lonEnd != '\0') return false;
  if (parsedLat < -90.0 || parsedLat > 90.0 || parsedLon < -180.0 || parsedLon > 180.0) return false;
  lat = parsedLat;
  lon = parsedLon;
  return true;
}

void NearbyFlightsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  errorMessage.clear();

  double lat = 0;
  double lon = 0;
  if (!parseHomeLocation(lat, lon)) {
    state = FlightsState::NO_LOCATION;
    requestUpdate();
    return;
  }

  state = FlightsState::CHECK_WIFI;
  requestUpdate();
  checkAndConnectWifi();
}

void NearbyFlightsActivity::onExit() {
  Activity::onExit();
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    // Leave WiFi up; the silent reboot below tears it down without
    // fragmenting the heap -- same pattern OpdsBookBrowserActivity uses.
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void NearbyFlightsActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    fetchFlights();
    return;
  }
  launchWifiSelection();
}

void NearbyFlightsActivity::launchWifiSelection() {
  state = FlightsState::WIFI_SELECTION;
  requestUpdate();
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void NearbyFlightsActivity::onWifiSelectionComplete(const bool connected) {
  if (connected) {
    fetchFlights();
  } else {
    state = FlightsState::ERROR;
    errorMessage = tr(STR_WIFI_CONN_FAILED);
    requestUpdate();
  }
}

void NearbyFlightsActivity::fetchFlights() {
  state = FlightsState::LOADING;
  requestUpdate(true);

  double lat = 0;
  double lon = 0;
  if (!parseHomeLocation(lat, lon)) {
    state = FlightsState::NO_LOCATION;
    requestUpdate();
    return;
  }

  const double radius = SETTINGS.flightTrackerRadiusMiles;
  parser.reset(lat, lon, radius);

  if (!OpenSkyClient::fetchNearby(lat, lon, radius, parser) || parser.hasError()) {
    state = FlightsState::ERROR;
    errorMessage = tr(STR_FETCH_FLIGHTS_FAILED);
    requestUpdate();
    return;
  }

  fetchCompletedMs = millis();
  selectedIndex = 0;
  state = FlightsState::LIST;
  requestUpdate();
}

void NearbyFlightsActivity::loop() {
  if (state == FlightsState::WIFI_SELECTION) return;

  if (state == FlightsState::NO_LOCATION) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      onGoHome(HomeMenuItem::NEARBY_FLIGHTS);
    }
    return;
  }

  if (state == FlightsState::CHECK_WIFI || state == FlightsState::LOADING) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome(HomeMenuItem::NEARBY_FLIGHTS);
    }
    return;
  }

  if (state == FlightsState::ERROR) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      checkAndConnectWifi();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome(HomeMenuItem::NEARBY_FLIGHTS);
    }
    return;
  }

  if (state == FlightsState::DETAIL) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = FlightsState::LIST;
      requestUpdate();
    }
    return;
  }

  // LIST
  const auto matchCount = static_cast<int>(parser.matchCount());

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome(HomeMenuItem::NEARBY_FLIGHTS);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (matchCount > 0) {
      state = FlightsState::DETAIL;
      requestUpdate();
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    checkAndConnectWifi();  // "Refresh" -- re-enters LOADING
    return;
  }

  if (matchCount > 0) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight =
        renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
    switch (handleListTouch(selectedIndex, matchCount, contentTop, contentHeight, true)) {
      case ListTouchResult::Activated:
        state = FlightsState::DETAIL;
        requestUpdate();
        return;
      case ListTouchResult::Consumed:
        return;
      case ListTouchResult::None:
        break;
    }

    buttonNavigator.onNextRelease([this, matchCount] {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, matchCount);
      requestUpdate();
    });
    buttonNavigator.onPreviousRelease([this, matchCount] {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, matchCount);
      requestUpdate();
    });
  }
}

void NearbyFlightsActivity::render(RenderLock&&) {
  switch (state) {
    case FlightsState::NO_LOCATION:
      renderNoLocation();
      return;
    case FlightsState::CHECK_WIFI:
    case FlightsState::LOADING:
      renderCheckWifiOrLoading();
      return;
    case FlightsState::WIFI_SELECTION:
      return;  // WifiSelectionActivity owns the screen while pushed
    case FlightsState::ERROR:
      renderError();
      return;
    case FlightsState::LIST:
      renderList();
      return;
    case FlightsState::DETAIL:
      renderDetail();
      return;
  }
}

void NearbyFlightsActivity::renderNoLocation() const {
  renderer.clearScreen();
  const auto pageHeight = renderer.getScreenHeight();
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_SET_HOME_LOCATION_FIRST));
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void NearbyFlightsActivity::renderCheckWifiOrLoading() const {
  renderer.clearScreen();
  const auto pageHeight = renderer.getScreenHeight();
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_LOADING));
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void NearbyFlightsActivity::renderError() const {
  renderer.clearScreen();
  const auto pageHeight = renderer.getScreenHeight();
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_ERROR_MSG));
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, errorMessage.c_str());
  if (mappedInput.hasTouch()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 40, tr(STR_TAP_TO_RETRY));
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void NearbyFlightsActivity::renderList() {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_NEARBY_FLIGHTS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  const auto matchCount = static_cast<int>(parser.matchCount());

  if (matchCount == 0) {
    char message[48];
    snprintf(message, sizeof(message), tr(STR_NO_FLIGHTS_FORMAT), static_cast<int>(SETTINGS.flightTrackerRadiusMiles));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, message);
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, matchCount, selectedIndex,
        [this](int index) -> std::string {
          const auto& m = parser.matchAt(static_cast<size_t>(index));
          return m.callsign[0] ? std::string(m.callsign) : std::string(tr(STR_UNKNOWN_CALLSIGN));
        },
        [this](int index) -> std::string {
          const auto& m = parser.matchAt(static_cast<size_t>(index));
          char buf[32];
          snprintf(buf, sizeof(buf), tr(STR_FLIGHT_DISTANCE_FORMAT), m.distanceMiles,
                   GeoMath::compassPoint(m.bearingDeg));
          return std::string(buf);
        },
        nullptr,
        [this](int index) -> std::string {
          const auto& m = parser.matchAt(static_cast<size_t>(index));
          if (!m.hasAltitudeFeet) return std::string();
          char buf[16];
          snprintf(buf, sizeof(buf), "%ld ft", static_cast<long>(m.altitudeFeet));
          return std::string(buf);
        });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_REFRESH), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void NearbyFlightsActivity::renderDetail() const {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto& m = parser.matchAt(static_cast<size_t>(selectedIndex));

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 m.callsign[0] ? m.callsign : tr(STR_UNKNOWN_CALLSIGN));

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing + 30;
  constexpr int LINE_HEIGHT = 28;
  char line[64];

  if (m.hasAltitudeFeet) {
    snprintf(line, sizeof(line), tr(STR_FLIGHT_ALTITUDE_FORMAT), static_cast<long>(m.altitudeFeet));
    renderer.drawText(UI_10_FONT_ID, 20, y, line, true);
    y += LINE_HEIGHT;
  }
  if (m.hasSpeedMph) {
    snprintf(line, sizeof(line), tr(STR_FLIGHT_SPEED_FORMAT), static_cast<long>(m.speedMph));
    renderer.drawText(UI_10_FONT_ID, 20, y, line, true);
    y += LINE_HEIGHT;
  }
  if (m.hasHeading) {
    snprintf(line, sizeof(line), tr(STR_FLIGHT_HEADING_FORMAT), static_cast<long>(m.headingDeg),
             GeoMath::compassPoint(m.headingDeg));
    renderer.drawText(UI_10_FONT_ID, 20, y, line, true);
    y += LINE_HEIGHT;
  }
  if (m.hasVerticalRate) {
    const char* rateLabel = m.verticalRateMs > 0.5f    ? tr(STR_FLIGHT_CLIMBING)
                            : m.verticalRateMs < -0.5f ? tr(STR_FLIGHT_DESCENDING)
                                                        : tr(STR_FLIGHT_LEVEL);
    renderer.drawText(UI_10_FONT_ID, 20, y, rateLabel, true);
    y += LINE_HEIGHT;
  }

  snprintf(line, sizeof(line), tr(STR_FLIGHT_DISTANCE_FORMAT), m.distanceMiles, GeoMath::compassPoint(m.bearingDeg));
  renderer.drawText(UI_10_FONT_ID, 20, y, line, true);
  y += LINE_HEIGHT;

  if (m.originCountry[0]) {
    snprintf(line, sizeof(line), tr(STR_FLIGHT_ORIGIN_FORMAT), m.originCountry);
    renderer.drawText(UI_10_FONT_ID, 20, y, line, true);
    y += LINE_HEIGHT;
  }

  snprintf(line, sizeof(line), tr(STR_FLIGHT_ICAO24_FORMAT), m.icao24);
  renderer.drawText(UI_10_FONT_ID, 20, y, line, true);
  y += LINE_HEIGHT;

  const unsigned long ageSeconds = (millis() - fetchCompletedMs) / 1000;
  snprintf(line, sizeof(line), tr(STR_FLIGHT_DATA_AGE_FORMAT), ageSeconds);
  renderer.drawText(UI_10_FONT_ID, 20, y, line, true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
```

- [ ] **Step 4: Verify the firmware builds**

This will NOT yet compile cleanly: `HomeMenuItem::NEARBY_FLIGHTS` doesn't exist until Task 7. Do Task 7's Step 1 (the `HomeMenuItem` enum addition) first, then return here.

Run: `pio run`
Expected: builds cleanly once Task 7 Step 1 has landed.

- [ ] **Step 5: Manual on-device verification (flag for human tester)**

🔲 **Device**: With a home location and radius already set (Task 5), open the "Nearby Flights" entry once Task 7 wires it into the Home menu. Confirm: Wi-Fi connect flow appears if not connected, loading indicator shows, list appears (or "No flights within Xmi" if none), selecting a row shows the detail screen with plausible values, "Refresh" re-fetches, Back from the list returns to Home (device may briefly reboot due to the WiFi-teardown silent restart — this is expected, matching existing OPDS/Calibre behavior).
🔲 **Device**: Test with an unset home location (fresh install or cleared settings): confirm the "Set a home location..." message appears instead of attempting a fetch.

- [ ] **Step 6: Commit**

```bash
git add src/activities/flights/NearbyFlightsActivity.h src/activities/flights/NearbyFlightsActivity.cpp lib/I18n/translations/english.yaml
git commit -m "feat: add NearbyFlightsActivity to show nearby aircraft"
```

---

## Task 7: Home menu wiring

**Files:**
- Modify: `src/activities/ActivityManager.h`
- Modify: `src/activities/ActivityManager.cpp`
- Modify: `src/activities/home/HomeActivity.h`
- Modify: `src/activities/home/HomeActivity.cpp`

**Interfaces:**
- Consumes: `NearbyFlightsActivity` (Task 6).
- Produces: `HomeMenuItem::NEARBY_FLIGHTS`, `ActivityManager::goToNearbyFlights()` — makes the feature reachable from the Home screen.

No host test: `HomeActivity`/`ActivityManager` aren't unit tested anywhere in this codebase. Verified via firmware build + manual on-device check.

**Do Step 1 of this task before Task 6's Step 4 (firmware build check), since `NearbyFlightsActivity.cpp` references `HomeMenuItem::NEARBY_FLIGHTS`.**

- [ ] **Step 1: Add the HomeMenuItem enumerator**

Open `src/activities/ActivityManager.h`, find:

```cpp
enum class HomeMenuItem { NONE, FILE_BROWSER, RECENTS, OPDS_BROWSER, FILE_TRANSFER, SETTINGS_MENU };
```

and change it to:

```cpp
enum class HomeMenuItem { NONE, FILE_BROWSER, RECENTS, OPDS_BROWSER, FILE_TRANSFER, NEARBY_FLIGHTS, SETTINGS_MENU };
```

Also in `src/activities/ActivityManager.h`, find the `goToX` declarations (near `goToFileTransfer(); goToSettings();`) and add:

```cpp
  void goToNearbyFlights();
```

- [ ] **Step 2: Implement goToNearbyFlights and extend goHome's name detection**

Open `src/activities/ActivityManager.cpp`, add the include near the other activity includes:

```cpp
#include "activities/flights/NearbyFlightsActivity.h"
```

Find:

```cpp
void ActivityManager::goToSettings() { replaceActivity(std::make_unique<SettingsActivity>(renderer, mappedInput)); }
```

and add immediately after it:

```cpp
void ActivityManager::goToNearbyFlights() {
  replaceActivity(std::make_unique<NearbyFlightsActivity>(renderer, mappedInput));
}
```

Find the `goHome()` name-detection block:

```cpp
    } else if (activityName == "CrossPointWebServer") {
      initialMenuItem = HomeMenuItem::FILE_TRANSFER;
    } else if (activityName == "Settings") {
```

and insert a new branch between them:

```cpp
    } else if (activityName == "CrossPointWebServer") {
      initialMenuItem = HomeMenuItem::FILE_TRANSFER;
    } else if (activityName == "NearbyFlights") {
      initialMenuItem = HomeMenuItem::NEARBY_FLIGHTS;
    } else if (activityName == "Settings") {
```

- [ ] **Step 3: Verify the firmware builds (this unblocks Task 6 Step 4 too)**

Run: `pio run`
Expected: builds cleanly (assuming Task 6's files already exist; if doing tasks strictly in order, this step will fail until Task 6's files exist — that's fine, come back to it after Task 6 Step 3).

- [ ] **Step 4: Update HomeActivity's menu index helpers**

Open `src/activities/home/HomeActivity.h`, find:

```cpp
static int menuItemToIndex(HomeMenuItem item, bool hasOpdsUrl) {
  int i = 0;
  if (item == HomeMenuItem::FILE_BROWSER) return i;
  ++i;
  if (item == HomeMenuItem::RECENTS) return i;
  ++i;
  if (item == HomeMenuItem::OPDS_BROWSER) return hasOpdsUrl ? i : 0;
  if (hasOpdsUrl) ++i;
  if (item == HomeMenuItem::FILE_TRANSFER) return i;
  ++i;
  if (item == HomeMenuItem::SETTINGS_MENU) return i;
  return 0;
}
static HomeMenuItem indexToMenuItem(int idx, bool hasOpdsUrl) {
  int i = 0;
  if (idx == i++) return HomeMenuItem::FILE_BROWSER;
  if (idx == i++) return HomeMenuItem::RECENTS;
  if (hasOpdsUrl && idx == i++) return HomeMenuItem::OPDS_BROWSER;
  if (idx == i++) return HomeMenuItem::FILE_TRANSFER;
  if (idx == i) return HomeMenuItem::SETTINGS_MENU;
  return HomeMenuItem::NONE;
}
```

and replace both with:

```cpp
static int menuItemToIndex(HomeMenuItem item, bool hasOpdsUrl) {
  int i = 0;
  if (item == HomeMenuItem::FILE_BROWSER) return i;
  ++i;
  if (item == HomeMenuItem::RECENTS) return i;
  ++i;
  if (item == HomeMenuItem::OPDS_BROWSER) return hasOpdsUrl ? i : 0;
  if (hasOpdsUrl) ++i;
  if (item == HomeMenuItem::FILE_TRANSFER) return i;
  ++i;
  if (item == HomeMenuItem::NEARBY_FLIGHTS) return i;
  ++i;
  if (item == HomeMenuItem::SETTINGS_MENU) return i;
  return 0;
}
static HomeMenuItem indexToMenuItem(int idx, bool hasOpdsUrl) {
  int i = 0;
  if (idx == i++) return HomeMenuItem::FILE_BROWSER;
  if (idx == i++) return HomeMenuItem::RECENTS;
  if (hasOpdsUrl && idx == i++) return HomeMenuItem::OPDS_BROWSER;
  if (idx == i++) return HomeMenuItem::FILE_TRANSFER;
  if (idx == i++) return HomeMenuItem::NEARBY_FLIGHTS;
  if (idx == i) return HomeMenuItem::SETTINGS_MENU;
  return HomeMenuItem::NONE;
}
```

Add the dispatch handler declaration near the other `onXOpen()` declarations in the same header (e.g. near `onFileTransferOpen()`):

```cpp
  void onNearbyFlightsOpen();
```

- [ ] **Step 5: Update getMenuItemCount, the dispatch switch, the render() menu lists, and add onNearbyFlightsOpen**

Open `src/activities/home/HomeActivity.cpp`, find:

```cpp
int HomeActivity::getMenuItemCount() const {
  int count = 4;  // File Browser, Recents, File transfer, Settings
  if (!recentBooks.empty()) count += recentBooks.size();
  if (hasOpdsServers) count++;
  return count;
}
```

and change to:

```cpp
int HomeActivity::getMenuItemCount() const {
  int count = 5;  // File Browser, Recents, File transfer, Nearby Flights, Settings
  if (!recentBooks.empty()) count += recentBooks.size();
  if (hasOpdsServers) count++;
  return count;
}
```

Find the dispatch switch:

```cpp
  switch (indexToMenuItem(menuIndex, hasOpdsServers)) {
    case HomeMenuItem::FILE_BROWSER: onFileBrowserOpen(); break;
    case HomeMenuItem::RECENTS: onRecentsOpen(); break;
    case HomeMenuItem::OPDS_BROWSER: onOpdsBrowserOpen(); break;
    case HomeMenuItem::FILE_TRANSFER: onFileTransferOpen(); break;
    case HomeMenuItem::SETTINGS_MENU: onSettingsOpen(); break;
    default: break;
  }
```

and add a case for the new item:

```cpp
  switch (indexToMenuItem(menuIndex, hasOpdsServers)) {
    case HomeMenuItem::FILE_BROWSER: onFileBrowserOpen(); break;
    case HomeMenuItem::RECENTS: onRecentsOpen(); break;
    case HomeMenuItem::OPDS_BROWSER: onOpdsBrowserOpen(); break;
    case HomeMenuItem::FILE_TRANSFER: onFileTransferOpen(); break;
    case HomeMenuItem::NEARBY_FLIGHTS: onNearbyFlightsOpen(); break;
    case HomeMenuItem::SETTINGS_MENU: onSettingsOpen(); break;
    default: break;
  }
```

Find the `onSettingsOpen`/`onFileTransferOpen` definitions:

```cpp
void HomeActivity::onFileTransferOpen() { activityManager.goToFileTransfer(); }
```

and add immediately after it:

```cpp
void HomeActivity::onNearbyFlightsOpen() { activityManager.goToNearbyFlights(); }
```

Find the render() menu-list construction:

```cpp
  std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), tr(STR_MENU_RECENT_BOOKS), tr(STR_FILE_TRANSFER), tr(STR_SETTINGS_TITLE)};
  std::vector<UIIcon> menuIcons = {Folder, Recent, Transfer, Settings};
```

and change to:

```cpp
  std::vector<const char*> menuItems = {tr(STR_BROWSE_FILES), tr(STR_MENU_RECENT_BOOKS), tr(STR_FILE_TRANSFER),
                                        tr(STR_NEARBY_FLIGHTS), tr(STR_SETTINGS_TITLE)};
  std::vector<UIIcon> menuIcons = {Folder, Recent, Transfer, Wifi, Settings};
```

(The `hasOpdsServers` insert-at-index-2 and the "continue reading" prepend-at-begin() logic that follow are unaffected — both still insert relative to a fixed offset that remains valid since Nearby Flights was appended near the end, not in the middle.)

- [ ] **Step 6: Verify the firmware builds**

Run: `pio run`
Expected: builds cleanly. This is also the point where Task 6's build check (Step 4 there) should now pass — go back and confirm that if it wasn't already checked.

- [ ] **Step 7: Manual on-device verification (flag for human tester)**

🔲 **Device**: Confirm "Nearby Flights" appears on the Home screen menu between "File Transfer" and "Settings", with the Wi-Fi icon. Confirm selecting it launches `NearbyFlightsActivity`. Confirm backing out of it (via the WiFi-teardown reboot path, if Wi-Fi was used) lands back on Home with "Nearby Flights" highlighted.

- [ ] **Step 8: Commit**

```bash
git add src/activities/ActivityManager.h src/activities/ActivityManager.cpp src/activities/home/HomeActivity.h src/activities/home/HomeActivity.cpp
git commit -m "feat: add Nearby Flights entry to the Home menu"
```

---

## Self-Review Notes

- **Spec coverage:** All decisions from `docs/superpowers/specs/2026-08-04-nearby-flights-design.md` are implemented: OpenSky anonymous source (Task 2/3), fixed home location via Settings (Task 4/5), manual refresh only (Task 6's "Refresh" button, no polling), imperial units (Task 2/6), compact list + tap-for-detail (Task 6), Home menu entry (Task 7), default 30mi radius (Task 4).
- **Bounded memory:** confirmed end-to-end — `OpenSkyStatesParser` never allocates; its `matches[MAX_MATCHES]` is a fixed member array, and `OpenSkyClient`/`HttpDownloader` stream the response without buffering the full body.
- **Type consistency:** `FlightMatch` fields (`hasAltitudeFeet`, `altitudeFeet`, `hasSpeedMph`, `speedMph`, `hasHeading`, `headingDeg`, `hasVerticalRate`, `verticalRateMs`, `distanceMiles`, `bearingDeg`, `callsign`, `originCountry`, `icao24`) are used identically across Task 2 (producer) and Task 6 (consumer) — verified by re-reading both against each other while writing this plan.
- **Task ordering caveat:** Task 6 and Task 7 have a circular-looking build dependency (`NearbyFlightsActivity` needs `HomeMenuItem::NEARBY_FLIGHTS`, and it's natural to wire the menu after the activity exists). Both tasks' "verify build" steps call this out explicitly with a pointer to do Task 7 Step 1 first if executing strictly in order.
