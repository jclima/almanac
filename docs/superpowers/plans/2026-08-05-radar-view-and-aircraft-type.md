# Radar View & Aircraft Type Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a polar radar view toggled from the existing Nearby Flights list, and show aircraft manufacturer/type/registration on the detail screen via an on-demand adsbdb.com lookup.

**Architecture:** `RADAR` joins the existing `FlightsState` enum in `NearbyFlightsActivity` with its own `renderRadar()`, reading the same already-fetched `OpenSkyStatesParser` — no new activity, no new fetch. Two pure functions (`polarToScreen`, `headingTriangle`) go into `lib/Geo/GeoMath` so the plot geometry is host-testable. Aircraft type comes from a new `AircraftInfoParser` (a `StreamingJsonParser` consumer, same shape as `OpenSkyStatesParser`) fed by a new `AdsbdbClient` modeled on `OpenSkyClient`.

**Tech Stack:** C++20 (Arduino/ESP-IDF via pioarduino), hand-rolled `StreamingJsonParser` (not ArduinoJson), GoogleTest via CMake/CTest for host tests.

**Spec:** [docs/superpowers/specs/2026-08-05-radar-view-and-aircraft-type-design.md](../specs/2026-08-05-radar-view-and-aircraft-type-design.md)

## Global Constraints

- Target: Xteink X4, ESP32-C3, ~380KB RAM, no PSRAM. Measured live during the base feature: ~101 KB free at idle, ~68 KB at the tightest moment during a TLS fetch.
- **No heap allocation in the render path** per `.claude/skills/heap-discipline`. Fixed-size stack arrays only. See the `fillPolygon` note in Task 4 — it is the one deliberate exception and must be justified in the code.
- Every user-facing string goes through `tr(STR_*)`. Log lines stay hardcoded English. New keys go in `lib/I18n/translations/english.yaml`; `scripts/gen_i18n.py` hard-fails the build on a referenced-but-missing key, and other languages fall back to English with a warning until translated.
- All rendering through the `GUI` macro and `UITheme::getInstance().getMetrics()`. **Never hardcode layout numbers** — metrics are per-theme (`BaseTheme` `headerHeight=45`, but `LyraTheme` `headerHeight=84`, `listRowHeight=40`; `RoundedRaffTheme` `topPadding=0`, `listRowHeight=42`).
- Enum dispatch uses exhaustive `switch` with no catch-all `default:`.
- Input via `MappedInputManager::Button` logical enums, never raw GPIO.
- Host tests live under `test/<name>/`, registered with an explicit `add_subdirectory(<name>)` line appended to `test/CMakeLists.txt`.

**Verification commands** (run from the worktree root):
```bash
cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release && cmake --build build/test && ctest --test-dir build/test --output-on-failure
```
```bash
~/.platformio/penv/bin/pio run
```
PlatformIO **is** installed at `~/.platformio/penv/bin/pio` (not on PATH). The firmware build works and takes ~40s incremental. Baseline before this plan: **153/153 host tests, firmware builds clean, RAM 15.5%, Flash 85.0%**.

i18n validation standalone:
```bash
python3 scripts/gen_i18n.py lib/I18n/translations /tmp/i18n-check --verbose
```

---

## Task 1: GeoMath polar-plot helpers + tests

**Files:**
- Modify: `lib/Geo/GeoMath.h`
- Modify: `lib/Geo/GeoMath.cpp`
- Modify: `test/geo_math/GeoMathTest.cpp`

**Interfaces:**
- Produces: `GeoMath::ScreenPoint`, `GeoMath::polarToScreen(...)`, `GeoMath::headingTriangle(...)` — consumed by `renderRadar()` in Task 4.

- [ ] **Step 1: Write the failing tests**

Append to `test/geo_math/GeoMathTest.cpp`:

```cpp
// -- polarToScreen ----------------------------------------------------------
// Screen coords: +x right, +y DOWN. Bearing 0 = north = up = -y, increasing
// clockwise, so bearing 90 = east = +x.

TEST(GeoMath, PolarToScreenCentreForZeroDistance) {
  const auto p = GeoMath::polarToScreen(0.0, 137.0, 30.0, 100, 100, 90);
  EXPECT_EQ(p.x, 100);
  EXPECT_EQ(p.y, 100);
}

TEST(GeoMath, PolarToScreenNorthIsUp) {
  const auto p = GeoMath::polarToScreen(30.0, 0.0, 30.0, 100, 100, 90);
  EXPECT_EQ(p.x, 100);
  EXPECT_EQ(p.y, 10);  // cy - radiusPx
}

TEST(GeoMath, PolarToScreenEastIsRight) {
  const auto p = GeoMath::polarToScreen(30.0, 90.0, 30.0, 100, 100, 90);
  EXPECT_EQ(p.x, 190);  // cx + radiusPx
  EXPECT_EQ(p.y, 100);
}

TEST(GeoMath, PolarToScreenSouthIsDown) {
  const auto p = GeoMath::polarToScreen(30.0, 180.0, 30.0, 100, 100, 90);
  EXPECT_EQ(p.x, 100);
  EXPECT_EQ(p.y, 190);  // cy + radiusPx
}

TEST(GeoMath, PolarToScreenWestIsLeft) {
  const auto p = GeoMath::polarToScreen(30.0, 270.0, 30.0, 100, 100, 90);
  EXPECT_EQ(p.x, 10);  // cx - radiusPx
  EXPECT_EQ(p.y, 100);
}

TEST(GeoMath, PolarToScreenDistanceScalesLinearly) {
  // A third of max range lands a third of the way out.
  const auto p = GeoMath::polarToScreen(10.0, 0.0, 30.0, 100, 100, 90);
  EXPECT_EQ(p.x, 100);
  EXPECT_EQ(p.y, 70);  // cy - 90/3
}

TEST(GeoMath, PolarToScreenClampsBeyondMaxRange) {
  // Over-range aircraft pin to the outer ring rather than escaping the plot.
  const auto p = GeoMath::polarToScreen(500.0, 90.0, 30.0, 100, 100, 90);
  EXPECT_EQ(p.x, 190);
  EXPECT_EQ(p.y, 100);
}

TEST(GeoMath, PolarToScreenNormalizesBearing) {
  const auto wrapped = GeoMath::polarToScreen(30.0, 450.0, 30.0, 100, 100, 90);
  const auto plain = GeoMath::polarToScreen(30.0, 90.0, 30.0, 100, 100, 90);
  EXPECT_EQ(wrapped.x, plain.x);
  EXPECT_EQ(wrapped.y, plain.y);
}

TEST(GeoMath, PolarToScreenHandlesZeroMaxRange) {
  // Degenerate config must not divide by zero; everything collapses to centre.
  const auto p = GeoMath::polarToScreen(10.0, 45.0, 0.0, 100, 100, 90);
  EXPECT_EQ(p.x, 100);
  EXPECT_EQ(p.y, 100);
}

TEST(GeoMath, PolarToScreenIntercardinalIsDiagonal) {
  // NE: equal +x and -y offsets.
  const auto p = GeoMath::polarToScreen(30.0, 45.0, 30.0, 100, 100, 90);
  EXPECT_EQ(p.x - 100, 100 - p.y);
  EXPECT_GT(p.x, 100);
  EXPECT_LT(p.y, 100);
}

// -- headingTriangle --------------------------------------------------------
// xs[0]/ys[0] is the nose vertex; the remaining three form the tail.

TEST(GeoMath, HeadingTriangleNoseUpForZeroHeading) {
  int xs[4];
  int ys[4];
  GeoMath::headingTriangle(100, 100, 0.0, 12, xs, ys);
  EXPECT_EQ(xs[0], 100);
  EXPECT_EQ(ys[0], 88);  // cy - size
}

TEST(GeoMath, HeadingTriangleNoseRightForEastHeading) {
  int xs[4];
  int ys[4];
  GeoMath::headingTriangle(100, 100, 90.0, 12, xs, ys);
  EXPECT_EQ(xs[0], 112);  // cx + size
  EXPECT_EQ(ys[0], 100);
}

TEST(GeoMath, HeadingTriangleNoseDownForSouthHeading) {
  int xs[4];
  int ys[4];
  GeoMath::headingTriangle(100, 100, 180.0, 12, xs, ys);
  EXPECT_EQ(xs[0], 100);
  EXPECT_EQ(ys[0], 112);  // cy + size
}

TEST(GeoMath, HeadingTriangleNoseLeftForWestHeading) {
  int xs[4];
  int ys[4];
  GeoMath::headingTriangle(100, 100, 270.0, 12, xs, ys);
  EXPECT_EQ(xs[0], 88);  // cx - size
  EXPECT_EQ(ys[0], 100);
}

TEST(GeoMath, HeadingTriangleVerticesAreDistinct) {
  int xs[4];
  int ys[4];
  GeoMath::headingTriangle(100, 100, 33.0, 12, xs, ys);
  // A degenerate glyph (all points coincident) would render as nothing.
  bool anyDifferent = false;
  for (int i = 1; i < 4; ++i) {
    if (xs[i] != xs[0] || ys[i] != ys[0]) anyDifferent = true;
  }
  EXPECT_TRUE(anyDifferent);
}

TEST(GeoMath, HeadingTriangleStaysWithinSizeBounds) {
  int xs[4];
  int ys[4];
  GeoMath::headingTriangle(100, 100, 217.0, 12, xs, ys);
  for (int i = 0; i < 4; ++i) {
    EXPECT_LE(std::abs(xs[i] - 100), 13);  // size + 1 for rounding
    EXPECT_LE(std::abs(ys[i] - 100), 13);
  }
}

TEST(GeoMath, HeadingTriangleNormalizesHeading) {
  int a[4];
  int b[4];
  int ay[4];
  int by[4];
  GeoMath::headingTriangle(100, 100, 450.0, 12, a, ay);
  GeoMath::headingTriangle(100, 100, 90.0, 12, b, by);
  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(a[i], b[i]);
    EXPECT_EQ(ay[i], by[i]);
  }
}
```

Add `#include <cstdlib>` at the top of the test file if not already present (for `std::abs`).

- [ ] **Step 2: Declare the new API in the header**

Add to `lib/Geo/GeoMath.h`, inside `namespace GeoMath`, after `computeBoundingBox`'s declaration:

```cpp
struct ScreenPoint {
  int x;
  int y;
};

// Maps a (distance, bearing) polar pair onto a radar plot. Screen coords are
// +x right and +y DOWN; bearing 0 is north (up) and increases clockwise, so
// bearing 90 lands due right of centre.
//
// Distances at or beyond maxRangeMiles clamp to the outer ring instead of
// escaping the plot. A maxRangeMiles of 0 collapses everything to the centre
// rather than dividing by zero.
ScreenPoint polarToScreen(double distanceMiles, double bearingDeg, double maxRangeMiles, int cx, int cy,
                          int radiusPx);

// Fills xs[4]/ys[4] with an arrow-like quadrilateral centred on (cx,cy),
// rotated so its nose points along headingDeg (0 = up/north, clockwise).
// xs[0]/ys[0] is always the nose. `size` is the centre-to-nose distance in
// pixels. Output feeds GfxRenderer::fillPolygon directly.
void headingTriangle(int cx, int cy, double headingDeg, int size, int xs[4], int ys[4]);
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release && cmake --build build/test`
Expected: FAIL — linker errors, undefined references to `GeoMath::polarToScreen` and `GeoMath::headingTriangle`.

- [ ] **Step 4: Implement**

Add to `lib/Geo/GeoMath.cpp`, inside `namespace GeoMath`, after `computeBoundingBox`:

```cpp
ScreenPoint polarToScreen(const double distanceMiles, const double bearingDeg, const double maxRangeMiles,
                          const int cx, const int cy, const int radiusPx) {
  if (maxRangeMiles <= 0.0 || radiusPx <= 0) {
    return ScreenPoint{cx, cy};
  }

  double ratio = distanceMiles / maxRangeMiles;
  if (ratio < 0.0) ratio = 0.0;
  if (ratio > 1.0) ratio = 1.0;  // clamp over-range aircraft to the outer ring

  const double r = ratio * radiusPx;
  const double theta = normalizeDegrees(bearingDeg) * DEG_TO_RAD;

  // Bearing 0 = north = up, so it maps to -y; bearing 90 = east = +x.
  const double dx = r * std::sin(theta);
  const double dy = -r * std::cos(theta);

  return ScreenPoint{cx + static_cast<int>(std::lround(dx)), cy + static_cast<int>(std::lround(dy))};
}

void headingTriangle(const int cx, const int cy, const double headingDeg, const int size, int xs[4], int ys[4]) {
  // Unrotated glyph, nose pointing up (-y), expressed as fractions of `size`:
  // nose, right-rear, tail notch, left-rear. The notch is what makes it read
  // as an arrow rather than a plain triangle at ~12px on e-ink.
  static constexpr double SHAPE_X[4] = {0.0, 0.64, 0.0, -0.64};
  static constexpr double SHAPE_Y[4] = {-1.0, 0.82, 0.36, 0.82};

  const double theta = normalizeDegrees(headingDeg) * DEG_TO_RAD;
  const double c = std::cos(theta);
  const double s = std::sin(theta);

  for (int i = 0; i < 4; ++i) {
    const double px = SHAPE_X[i] * size;
    const double py = SHAPE_Y[i] * size;
    // Clockwise rotation in screen coords (+y down).
    const double rx = px * c - py * s;
    const double ry = px * s + py * c;
    xs[i] = cx + static_cast<int>(std::lround(rx));
    ys[i] = cy + static_cast<int>(std::lround(ry));
  }
}
```

Note: `normalizeDegrees` and `DEG_TO_RAD` already exist in the anonymous namespace at the top of this file — both new functions use them, so nothing needs to be exported.

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build/test && ctest --test-dir build/test --output-on-failure -R GeoMath`
Expected: PASS — all `GeoMath.*` tests green, including the 15 pre-existing ones.

- [ ] **Step 6: Run the full suite and commit**

Run: `ctest --test-dir build/test --output-on-failure`
Expected: PASS (153 pre-existing + 17 new = 170).

```bash
git add lib/Geo/GeoMath.h lib/Geo/GeoMath.cpp test/geo_math/GeoMathTest.cpp
git commit -m "feat: add polarToScreen and headingTriangle to GeoMath"
```

---

## Task 2: AircraftInfoParser + tests

**Files:**
- Create: `lib/JsonParser/AircraftInfoParser.h`
- Create: `lib/JsonParser/AircraftInfoParser.cpp`
- Create: `test/aircraft_info_parser/CMakeLists.txt`
- Create: `test/aircraft_info_parser/AircraftInfoParserTest.cpp`
- Modify: `test/CMakeLists.txt`

**Interfaces:**
- Consumes: `StreamingJsonParser` / `JsonCallbacks` (existing, `lib/JsonParser/StreamingJsonParser.h`).
- Produces: `struct AircraftInfo { char icao24[7]; char manufacturer[24]; char icaoType[8]; char registration[12]; bool found; };` and `class AircraftInfoParser { void reset(); void feed(const char*, size_t); bool hasError() const; const AircraftInfo& info() const; };` — consumed by `AdsbdbClient` (Task 3) and `NearbyFlightsActivity` (Task 5).

**Critical behaviour:** adsbdb returns `{"response":{"aircraft":{...}}}` on success but `{"response":"unknown aircraft"}` when it has no record — `response` changes from **object to string**. This is not hypothetical; it was hit on the first four aircraft sampled from the test location. `found` must be false for the string case, and `hasError()` must stay false (a missing record is a normal outcome, not a parse failure).

- [ ] **Step 1: Write the failing tests**

Create `test/aircraft_info_parser/AircraftInfoParserTest.cpp`:

```cpp
#include <gtest/gtest.h>

#include <string>

#include "lib/JsonParser/AircraftInfoParser.h"

namespace {
// Captured verbatim from api.adsbdb.com/v0/aircraft/a25b3f on 2026-08-05.
const char* const REAL_SUCCESS = R"({"response":{"aircraft":{
"type":"737NG 990ER/W","icao_type":"B739","manufacturer":"Boeing",
"mode_s":"A25B3F","registration":"N251AK","registered_owner_country_iso_name":"US",
"registered_owner_country_name":"United States","registered_owner_operator_flag_code":"ASA",
"registered_owner":"Alaska Airlines",
"url_photo":"https://airport-data.com/images/aircraft/001/419/001419608.jpg",
"url_photo_thumbnail":"https://airport-data.com/images/aircraft/thumbnails/001/419/001419608.jpg"}}})";

// The not-found shape: `response` is a STRING here, not an object.
const char* const REAL_NOT_FOUND = R"({"response":"unknown aircraft"})";

void feedAll(AircraftInfoParser& p, const char* json) { p.feed(json, strlen(json)); }
}  // namespace

TEST(AircraftInfoParser, ParsesRealSuccessPayload) {
  AircraftInfoParser p;
  p.reset();
  feedAll(p, REAL_SUCCESS);

  ASSERT_FALSE(p.hasError());
  ASSERT_TRUE(p.info().found);
  EXPECT_STREQ(p.info().manufacturer, "Boeing");
  EXPECT_STREQ(p.info().icaoType, "B739");
  EXPECT_STREQ(p.info().registration, "N251AK");
}

TEST(AircraftInfoParser, UnknownAircraftStringIsNotFoundButNotAnError) {
  AircraftInfoParser p;
  p.reset();
  feedAll(p, REAL_NOT_FOUND);

  EXPECT_FALSE(p.hasError());  // a missing record is a normal outcome
  EXPECT_FALSE(p.info().found);
  EXPECT_STREQ(p.info().manufacturer, "");
  EXPECT_STREQ(p.info().icaoType, "");
}

TEST(AircraftInfoParser, IgnoresTheVerboseTypeFieldAndUsesIcaoType) {
  // "type" is verbose and inconsistent ("737NG 990ER/W"); icao_type is the
  // clean 4-char designator. Only the latter should land in icaoType.
  AircraftInfoParser p;
  p.reset();
  feedAll(p, REAL_SUCCESS);

  ASSERT_TRUE(p.info().found);
  EXPECT_STREQ(p.info().icaoType, "B739");
}

TEST(AircraftInfoParser, MissingFieldsLeaveEmptyStringsWithoutError) {
  AircraftInfoParser p;
  p.reset();
  feedAll(p, R"({"response":{"aircraft":{"icao_type":"A320"}}})");

  EXPECT_FALSE(p.hasError());
  EXPECT_TRUE(p.info().found);
  EXPECT_STREQ(p.info().icaoType, "A320");
  EXPECT_STREQ(p.info().manufacturer, "");
  EXPECT_STREQ(p.info().registration, "");
}

TEST(AircraftInfoParser, TruncatedBodyDoesNotReportFound) {
  AircraftInfoParser p;
  p.reset();
  feedAll(p, R"({"response":{"aircraft":{"manufacturer":"Boei)");

  // Whether the streaming parser flags an error here is its business; what
  // matters is that a half-delivered record is never presented as complete.
  EXPECT_FALSE(p.info().found);
}

TEST(AircraftInfoParser, OverlongValuesTruncateWithoutOverrun) {
  AircraftInfoParser p;
  p.reset();
  std::string json = R"({"response":{"aircraft":{"manufacturer":")";
  json += std::string(200, 'X');
  json += R"(","icao_type":"B739"}}})";
  feedAll(p, json.c_str());

  EXPECT_FALSE(p.hasError());
  // Fits the fixed buffer and stays NUL-terminated.
  EXPECT_LT(strlen(p.info().manufacturer), sizeof(AircraftInfo::manufacturer));
  EXPECT_EQ(p.info().manufacturer[sizeof(AircraftInfo::manufacturer) - 1], '\0');
}

TEST(AircraftInfoParser, FeedInChunksProducesSameResult) {
  AircraftInfoParser p;
  p.reset();
  const std::string json(REAL_SUCCESS);
  const size_t mid = json.size() / 2;
  p.feed(json.c_str(), mid);
  p.feed(json.c_str() + mid, json.size() - mid);

  ASSERT_FALSE(p.hasError());
  ASSERT_TRUE(p.info().found);
  EXPECT_STREQ(p.info().icaoType, "B739");
}

TEST(AircraftInfoParser, ResetClearsPreviousResult) {
  AircraftInfoParser p;
  p.reset();
  feedAll(p, REAL_SUCCESS);
  ASSERT_TRUE(p.info().found);

  p.reset();
  EXPECT_FALSE(p.info().found);
  EXPECT_STREQ(p.info().icaoType, "");

  feedAll(p, REAL_NOT_FOUND);
  EXPECT_FALSE(p.info().found);
}
```

Create `test/aircraft_info_parser/CMakeLists.txt`:

```cmake
add_executable(AircraftInfoParserTest
  AircraftInfoParserTest.cpp
  ${REPO_ROOT}/lib/JsonParser/AircraftInfoParser.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)

target_include_directories(AircraftInfoParserTest PRIVATE
  ${REPO_ROOT}/lib/JsonParser
)

target_link_libraries(AircraftInfoParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(AircraftInfoParserTest)
```

Append to `test/CMakeLists.txt` (after `add_subdirectory(opensky_states_parser)`):

```cmake
add_subdirectory(aircraft_info_parser)
```

- [ ] **Step 2: Write the header**

Create `lib/JsonParser/AircraftInfoParser.h`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

#include "StreamingJsonParser.h"

// One aircraft's registry record, as returned by adsbdb.com.
struct AircraftInfo {
  char icao24[7] = {0};
  char manufacturer[24] = {0};
  char icaoType[8] = {0};       // clean 4-char ICAO designator, e.g. "B739"
  char registration[12] = {0};  // tail number, e.g. "N251AK"
  bool found = false;
};

// Streams adsbdb.com's /v0/aircraft/<icao24> response.
//
// The endpoint returns {"response":{"aircraft":{...}}} on success but
// {"response":"unknown aircraft"} when it has no record -- `response` changes
// from an object to a string. A missing record sets found=false and is NOT an
// error; only malformed JSON sets hasError().
//
// Fixed-size buffers throughout: no allocation, ~54 bytes of state.
class AircraftInfoParser {
 public:
  AircraftInfoParser();

  AircraftInfoParser(const AircraftInfoParser&) = delete;
  AircraftInfoParser& operator=(const AircraftInfoParser&) = delete;

  // Clears all state. Call before each lookup.
  void reset();

  // Feeds a chunk of the HTTP response body.
  void feed(const char* data, size_t len);

  bool hasError() const { return parser.hasError(); }

  const AircraftInfo& info() const { return result; }

 private:
  enum class Position : uint8_t { TOP_LEVEL, IN_RESPONSE, IN_AIRCRAFT };
  enum class LastKey : uint8_t { NONE, RESPONSE, AIRCRAFT, MANUFACTURER, ICAO_TYPE, REGISTRATION };

  static void sOnKey(void* ctx, const char* key, size_t len);
  static void sOnString(void* ctx, const char* value, size_t len);
  static void sOnNumber(void* ctx, const char* value, size_t len);
  static void sOnBool(void* ctx, bool value);
  static void sOnNull(void* ctx);
  static void sOnObjectStart(void* ctx);
  static void sOnObjectEnd(void* ctx);

  StreamingJsonParser parser;

  Position position = Position::TOP_LEVEL;
  LastKey lastKey = LastKey::NONE;
  uint8_t depth = 0;
  bool sawAircraftObject = false;

  AircraftInfo result;
};
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `cmake -S test -B build/test -DCMAKE_BUILD_TYPE=Release && cmake --build build/test`
Expected: FAIL — linker errors, undefined references to `AircraftInfoParser` methods.

- [ ] **Step 4: Implement**

Create `lib/JsonParser/AircraftInfoParser.cpp`:

```cpp
#include "AircraftInfoParser.h"

#include <cstring>

namespace {
void safeCopy(char* dst, size_t dstSize, const char* src, size_t srcLen) {
  const size_t n = srcLen < dstSize - 1 ? srcLen : dstSize - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

bool keyIs(const char* key, size_t len, const char* literal) {
  const size_t litLen = strlen(literal);
  return len == litLen && memcmp(key, literal, litLen) == 0;
}
}  // namespace

AircraftInfoParser::AircraftInfoParser()
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                           nullptr, nullptr}) {}

void AircraftInfoParser::reset() {
  parser.reset();
  position = Position::TOP_LEVEL;
  lastKey = LastKey::NONE;
  depth = 0;
  sawAircraftObject = false;
  result = AircraftInfo{};
}

void AircraftInfoParser::feed(const char* data, size_t len) { parser.feed(data, len); }

void AircraftInfoParser::sOnKey(void* ctx, const char* key, size_t len) {
  auto* self = static_cast<AircraftInfoParser*>(ctx);
  switch (self->position) {
    case Position::TOP_LEVEL:
      self->lastKey = keyIs(key, len, "response") ? LastKey::RESPONSE : LastKey::NONE;
      break;
    case Position::IN_RESPONSE:
      self->lastKey = keyIs(key, len, "aircraft") ? LastKey::AIRCRAFT : LastKey::NONE;
      break;
    case Position::IN_AIRCRAFT:
      if (keyIs(key, len, "manufacturer")) {
        self->lastKey = LastKey::MANUFACTURER;
      } else if (keyIs(key, len, "icao_type")) {
        self->lastKey = LastKey::ICAO_TYPE;
      } else if (keyIs(key, len, "registration")) {
        self->lastKey = LastKey::REGISTRATION;
      } else {
        // Deliberately ignores "type" -- verbose and inconsistent
        // ("737NG 990ER/W"); icao_type is the clean designator.
        self->lastKey = LastKey::NONE;
      }
      break;
  }
}

void AircraftInfoParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<AircraftInfoParser*>(ctx);
  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->lastKey == LastKey::RESPONSE) {
        self->position = Position::IN_RESPONSE;
      } else {
        self->depth++;
      }
      break;
    case Position::IN_RESPONSE:
      if (self->lastKey == LastKey::AIRCRAFT) {
        self->position = Position::IN_AIRCRAFT;
        self->sawAircraftObject = true;
      } else {
        self->depth++;
      }
      break;
    case Position::IN_AIRCRAFT:
      self->depth++;
      break;
  }
  self->lastKey = LastKey::NONE;
}

void AircraftInfoParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<AircraftInfoParser*>(ctx);
  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_RESPONSE:
      if (self->depth > 0) {
        self->depth--;
      } else {
        self->position = Position::TOP_LEVEL;
      }
      break;
    case Position::IN_AIRCRAFT:
      if (self->depth > 0) {
        self->depth--;
      } else {
        // A complete aircraft object closed: the record is trustworthy now.
        self->result.found = self->sawAircraftObject;
        self->position = Position::IN_RESPONSE;
      }
      break;
  }
  self->lastKey = LastKey::NONE;
}

void AircraftInfoParser::sOnString(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<AircraftInfoParser*>(ctx);
  if (self->position == Position::IN_AIRCRAFT && self->depth == 0) {
    switch (self->lastKey) {
      case LastKey::MANUFACTURER:
        safeCopy(self->result.manufacturer, sizeof(self->result.manufacturer), value, len);
        break;
      case LastKey::ICAO_TYPE:
        safeCopy(self->result.icaoType, sizeof(self->result.icaoType), value, len);
        break;
      case LastKey::REGISTRATION:
        safeCopy(self->result.registration, sizeof(self->result.registration), value, len);
        break;
      default:
        break;
    }
  }
  // A string value for the top-level "response" key is adsbdb's
  // {"response":"unknown aircraft"} miss -- normal, not an error.
  self->lastKey = LastKey::NONE;
}

void AircraftInfoParser::sOnNumber(void* ctx, const char* /*value*/, size_t /*len*/) {
  static_cast<AircraftInfoParser*>(ctx)->lastKey = LastKey::NONE;
}

void AircraftInfoParser::sOnBool(void* ctx, bool /*value*/) {
  static_cast<AircraftInfoParser*>(ctx)->lastKey = LastKey::NONE;
}

void AircraftInfoParser::sOnNull(void* ctx) {
  static_cast<AircraftInfoParser*>(ctx)->lastKey = LastKey::NONE;
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run: `cmake --build build/test && ctest --test-dir build/test --output-on-failure -R AircraftInfoParser`
Expected: PASS — all 8 `AircraftInfoParser.*` tests green.

- [ ] **Step 6: Run the full suite and commit**

Run: `ctest --test-dir build/test --output-on-failure`
Expected: 178 passing (170 after Task 1 + 8 new).

```bash
git add lib/JsonParser/AircraftInfoParser.h lib/JsonParser/AircraftInfoParser.cpp test/aircraft_info_parser test/CMakeLists.txt
git commit -m "feat: add AircraftInfoParser for adsbdb aircraft registry lookups"
```

---

## Task 3: AdsbdbClient

**Files:**
- Create: `src/network/AdsbdbClient.h`
- Create: `src/network/AdsbdbClient.cpp`

**Interfaces:**
- Consumes: `AircraftInfoParser` (Task 2), `HttpDownloader::fetchUrl(const std::string&, const DataCallback&, ...)` (existing).
- Produces: `class AdsbdbClient { static bool fetchAircraftInfo(const char* icao24, AircraftInfoParser& parser); };` — consumed by `NearbyFlightsActivity` (Task 5).

No host test: this depends on `HttpDownloader.h`, which pulls ESP-IDF/Arduino headers unavailable in the host toolchain — the same reason `OpenSkyClient` has no host test. Verified by the firmware build plus on-device use.

- [ ] **Step 1: Write the header**

Create `src/network/AdsbdbClient.h`:

```cpp
#pragma once

#include "AircraftInfoParser.h"

// Looks up one aircraft's registry record (manufacturer, ICAO type,
// registration) from adsbdb.com's free, keyless API.
//
// Deliberately one aircraft at a time, called only when a detail screen opens:
// looking up a whole 20-match list would mean 20 sequential TLS handshakes,
// each peaking ~36KB transient heap (measured on device).
class AdsbdbClient {
 public:
  // parser must already be reset() before calling. Returns false on
  // HTTP/transport failure; check parser.hasError() for a JSON parse failure
  // and parser.info().found for "no record" (a normal outcome, not an error).
  static bool fetchAircraftInfo(const char* icao24, AircraftInfoParser& parser);
};
```

- [ ] **Step 2: Implement**

Create `src/network/AdsbdbClient.cpp`:

```cpp
#include "AdsbdbClient.h"

#include <Logging.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "network/HttpDownloader.h"

bool AdsbdbClient::fetchAircraftInfo(const char* icao24, AircraftInfoParser& parser) {
  if (!icao24 || icao24[0] == '\0') {
    LOG_ERR("ADSBDB", "fetchAircraftInfo called with an empty icao24");
    return false;
  }

  char url[80];
  snprintf(url, sizeof(url), "https://api.adsbdb.com/v0/aircraft/%s", icao24);

  LOG_DBG("ADSBDB", "Fetching: %s", url);

  return HttpDownloader::fetchUrl(std::string(url), [&parser](const uint8_t* data, size_t len) {
    parser.feed(reinterpret_cast<const char*>(data), len);
    return !parser.hasError();
  });
}
```

- [ ] **Step 3: Verify it compiles into the firmware**

Run: `~/.platformio/penv/bin/pio run`
Expected: SUCCESS. The file isn't referenced by any activity yet, but PlatformIO compiles everything under `src/`, so this proves the includes resolve and the signatures match.

**Include-path warning:** `"network/HttpDownloader.h"` must be path-qualified from `src/`, not bare — PlatformIO puts `src/` on the include path but **not** `src/` subdirectories. A bare `"HttpDownloader.h"` happens to work here only because the file is in the same directory; `"AircraftInfoParser.h"` resolves via the `lib/JsonParser` root. This exact class of mistake broke the build once already in the base feature.

- [ ] **Step 4: Run the host suite as a regression check and commit**

Run: `ctest --test-dir build/test --output-on-failure`
Expected: 178 passing, unchanged.

```bash
git add src/network/AdsbdbClient.h src/network/AdsbdbClient.cpp
git commit -m "feat: add AdsbdbClient for on-demand aircraft type lookup"
```

---

## Task 4: Radar view — state, rendering, and button remap

**Files:**
- Modify: `src/activities/flights/NearbyFlightsActivity.h`
- Modify: `src/activities/flights/NearbyFlightsActivity.cpp`
- Modify: `lib/I18n/translations/english.yaml`

**Interfaces:**
- Consumes: `GeoMath::polarToScreen`, `GeoMath::headingTriangle`, `GeoMath::compassPoint` (Task 1 + existing); `OpenSkyStatesParser::matchCount()/matchAt()` (existing).
- Produces: `FlightsState::RADAR` and `renderRadar()`; the new button layout that Task 5's detail screen inherits.

No host test — no `Activity` subclass in this codebase has one. The geometry it depends on is covered by Task 1; the rest is verified by the firmware build and on-device checks.

### Button layout

```
LIST      « Back | Select | Radar | Down
RADAR     « Back | Detail | List  | Next
```

`Refresh` moves to a **long-press of Confirm** on both views.

- [ ] **Step 1: Add the i18n keys**

Add to `lib/I18n/translations/english.yaml`, in the flights block near `STR_NEARBY_FLIGHTS`:

```yaml
STR_RADAR: "Radar"
STR_LIST_VIEW: "List"
STR_NEXT_AIRCRAFT: "Next"
STR_FLIGHT_DETAIL: "Detail"
```

`STR_REFRESH`, `STR_BACK`, `STR_SELECT`, `STR_DIR_DOWN` already exist and are reused. Only `english.yaml` needs editing — the other 30 language files fall back to English with a build-time warning until translated.

- [ ] **Step 2: Add state, members, and the render declaration**

In `src/activities/flights/NearbyFlightsActivity.h`:

Change the enum (adding `RADAR`):
```cpp
  enum class FlightsState { NO_LOCATION, CHECK_WIFI, WIFI_SELECTION, LOADING, LIST, RADAR, DETAIL, ERROR };
```

Add to the private members, after `unsigned long fetchCompletedMs = 0;`:
```cpp
  // Long-press Confirm = Refresh, short press = select/detail. getHeldTime()
  // is global rather than per-button, so confirmHeld is what attributes the
  // elapsed time to Confirm -- dropping it makes the check misfire on any
  // other button. Same pattern as KeyboardEntryActivity.
  static constexpr uint16_t LONG_PRESS_MS = 500;
  bool confirmHeld = false;
  bool confirmLongHandled = false;
```

Add to the private methods, after `void renderList();`:
```cpp
  void renderRadar();
  // Shared by LIST and RADAR: short press acts, long press refreshes.
  // Returns true when the caller should stop processing input this frame.
  bool handleConfirmPressOrRefresh(bool hasMatches);
```

- [ ] **Step 3: Implement the shared confirm/long-press handler**

Add to `src/activities/flights/NearbyFlightsActivity.cpp`, before `loop()`:

```cpp
bool NearbyFlightsActivity::handleConfirmPressOrRefresh(const bool hasMatches) {
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    confirmHeld = true;
    confirmLongHandled = false;
  }

  if (confirmHeld && !confirmLongHandled && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() > LONG_PRESS_MS) {
    confirmLongHandled = true;
    confirmHeld = false;
    checkAndConnectWifi();  // "Refresh"
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const bool wasShortPress = confirmHeld && !confirmLongHandled;
    confirmHeld = false;
    confirmLongHandled = false;
    if (wasShortPress && hasMatches) {
      state = FlightsState::DETAIL;
      requestUpdate();
    }
    return true;
  }

  return false;
}
```

- [ ] **Step 4: Rewire the LIST case and add the RADAR case in `loop()`**

Replace the LIST case's Confirm and Left handling. The LIST case currently reads:

```cpp
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
```

Replace those two blocks with:

```cpp
      if (handleConfirmPressOrRefresh(matchCount > 0)) {
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
        state = FlightsState::RADAR;  // toggle to radar
        requestUpdate();
        return;
      }
```

Then add a new `case FlightsState::RADAR:` to the same switch, immediately after the LIST case's closing brace:

```cpp
    case FlightsState::RADAR: {
      const auto matchCount = static_cast<int>(parser.matchCount());

      if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        onGoHome(HomeMenuItem::NEARBY_FLIGHTS);
        return;
      }
      if (handleConfirmPressOrRefresh(matchCount > 0)) {
        return;
      }
      if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
        state = FlightsState::LIST;  // toggle back to the list
        requestUpdate();
        return;
      }

      if (matchCount > 0) {
        buttonNavigator.onNextRelease([this, matchCount] {
          selectedIndex = ButtonNavigator::nextIndex(selectedIndex, matchCount);
          requestUpdate();
        });
        buttonNavigator.onPreviousRelease([this, matchCount] {
          selectedIndex = ButtonNavigator::previousIndex(selectedIndex, matchCount);
          requestUpdate();
        });
      }
      return;
    }
```

Note `selectedIndex` is deliberately shared with LIST — toggling views preserves the selection, and it already resets to 0 on each successful fetch, which is what keeps the unchecked `matchAt()` in bounds.

- [ ] **Step 5: Add the RADAR case to `render()`**

In `render()`'s switch, after the `LIST` case:

```cpp
    case FlightsState::RADAR:
      renderRadar();
      return;
```

The switch has no `default:`, so the compiler will have been erroring on both switches since Step 2 — that is the intended safety net.

- [ ] **Step 6: Update the LIST button hints**

In `renderList()`, change:
```cpp
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_REFRESH), tr(STR_DIR_DOWN));
```
to:
```cpp
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_RADAR), tr(STR_DIR_DOWN));
```

- [ ] **Step 7: Implement `renderRadar()`**

Add to `src/activities/flights/NearbyFlightsActivity.cpp`, after `renderList()`:

```cpp
void NearbyFlightsActivity::renderRadar() {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_NEARBY_FLIGHTS));

  const auto matchCount = static_cast<int>(parser.matchCount());
  const double maxRange = SETTINGS.flightTrackerRadiusMiles;

  // Reserve four rows at the bottom for the selected-aircraft readout, above
  // the button hints. Everything derives from theme metrics -- headerHeight
  // alone varies 45..84 across the shipped themes.
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int stripHeight = metrics.listRowHeight * 3;
  const int plotBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing * 2 - stripHeight;
  const int plotHeight = plotBottom - contentTop;

  if (matchCount == 0) {
    char message[48];
    snprintf(message, sizeof(message), tr(STR_NO_FLIGHTS_FORMAT), static_cast<int>(SETTINGS.flightTrackerRadiusMiles));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, message);
    const auto emptyLabels = mappedInput.mapLabels(tr(STR_BACK), "", tr(STR_LIST_VIEW), "");
    GUI.drawButtonHints(renderer, emptyLabels.btn1, emptyLabels.btn2, emptyLabels.btn3, emptyLabels.btn4);
    renderer.displayBuffer();
    return;
  }

  const int cx = pageWidth / 2;
  const int cy = contentTop + plotHeight / 2;
  // Fit the circle to whichever axis is tighter, so landscape orientations
  // shrink the plot instead of clipping it.
  const int radiusPx = std::min(pageWidth / 2, plotHeight / 2) - metrics.contentSidePadding;

  // Range rings. drawArc renders ONE QUADRANT per call, selected by the
  // xDir/yDir signs: (-1,-1) top-left, (1,-1) top-right, (1,1) bottom-right,
  // (-1,1) bottom-left. Four calls = one full circle. lineWidth must stay
  // well below the radius or the ring fills into a solid disc.
  static constexpr int RING_COUNT = 3;
  for (int ring = 1; ring <= RING_COUNT; ++ring) {
    const int r = radiusPx * ring / RING_COUNT;
    const int lineWidth = (ring == RING_COUNT) ? 2 : 1;
    drawCircle(renderer, r, cx, cy, lineWidth);

    char ringLabel[12];
    if (ring == RING_COUNT) {
      snprintf(ringLabel, sizeof(ringLabel), tr(STR_RADAR_RANGE_OUTER_FORMAT), static_cast<int>(maxRange));
    } else {
      snprintf(ringLabel, sizeof(ringLabel), "%d", static_cast<int>(maxRange * ring / RING_COUNT));
    }
    renderer.drawText(UI_10_FONT_ID, cx + 6, cy - r - 2, ringLabel, true);
  }

  // Crosshair and compass letters.
  renderer.drawLine(cx, cy - radiusPx, cx, cy + radiusPx, true);
  renderer.drawLine(cx - radiusPx, cy, cx + radiusPx, cy, true);
  renderer.drawText(UI_10_FONT_ID, cx - 4, cy - radiusPx - 22, "N", true);
  renderer.drawText(UI_10_FONT_ID, cx - 4, cy + radiusPx + 6, "S", true);
  renderer.drawText(UI_10_FONT_ID, cx + radiusPx + 6, cy - 8, "E", true);
  renderer.drawText(UI_10_FONT_ID, cx - radiusPx - 14, cy - 8, "W", true);

  // Home location.
  renderer.fillRect(cx - 2, cy - 2, 5, 5, true);

  // Aircraft. fillPolygon malloc()s a small node buffer per call, so this is
  // up to MAX_MATCHES(20) 16-byte alloc/free pairs per frame. They are tiny,
  // immediately freed, and identically sized, so the allocator reuses the same
  // block rather than fragmenting -- and radar frames only render on user
  // input, never continuously. Accepted deliberately; revisit if heap
  // instrumentation shows otherwise.
  static constexpr int MARK_SIZE = 11;
  static constexpr int SELECTED_MARK_SIZE = 15;
  for (int i = 0; i < matchCount; ++i) {
    const auto& m = parser.matchAt(static_cast<size_t>(i));
    const auto p = GeoMath::polarToScreen(m.distanceMiles, m.bearingDeg, maxRange, cx, cy, radiusPx);
    const bool isSelected = (i == selectedIndex);
    // Aircraft with no reported track draw nose-up rather than vanishing.
    const double heading = m.hasHeading ? static_cast<double>(m.headingDeg) : 0.0;

    int xs[4];
    int ys[4];
    GeoMath::headingTriangle(p.x, p.y, heading, isSelected ? SELECTED_MARK_SIZE : MARK_SIZE, xs, ys);
    renderer.fillPolygon(xs, ys, 4, true);

    if (isSelected) {
      drawCircle(renderer, SELECTED_MARK_SIZE + 7, p.x, p.y, 1);
    }
  }

  // Selected-aircraft readout.
  const auto& sel = parser.matchAt(static_cast<size_t>(selectedIndex));
  const int textX = metrics.contentSidePadding;
  int textY = plotBottom + metrics.verticalSpacing;
  char line[64];

  renderer.drawText(UI_10_FONT_ID, textX, textY, sel.callsign[0] ? sel.callsign : tr(STR_UNKNOWN_CALLSIGN), true);
  snprintf(line, sizeof(line), tr(STR_FLIGHT_DISTANCE_FORMAT), sel.distanceMiles, GeoMath::compassPoint(sel.bearingDeg));
  renderer.drawText(UI_10_FONT_ID, textX, textY + metrics.listRowHeight, line, true);

  if (sel.hasAltitudeFeet) {
    snprintf(line, sizeof(line), tr(STR_FLIGHT_ALTITUDE_FORMAT), static_cast<long>(sel.altitudeFeet));
    renderer.drawText(UI_10_FONT_ID, textX, textY + metrics.listRowHeight * 2, line, true);
  }

  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), tr(STR_FLIGHT_DETAIL), tr(STR_LIST_VIEW), tr(STR_NEXT_AIRCRAFT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
```

- [ ] **Step 8: Add the circle helper and its i18n key**

`GfxRenderer` has **no circle primitive** — `drawArc` is the only curve API and renders one quadrant per call. Add a file-local helper.

At the top of `src/activities/flights/NearbyFlightsActivity.cpp`, after the includes, add an anonymous namespace (or extend one if present):

```cpp
namespace {
// GfxRenderer has no circle primitive; drawArc renders a single quadrant
// selected by the xDir/yDir signs. Four calls make one full circle. The axis
// row/column is drawn twice (once by each adjacent quadrant), which is
// harmless because fillRect sets pixels rather than toggling them.
void drawCircle(const GfxRenderer& renderer, int radius, int cx, int cy, int lineWidth) {
  if (radius <= 0) return;
  renderer.drawArc(radius, cx, cy, -1, -1, lineWidth, true);
  renderer.drawArc(radius, cx, cy, 1, -1, lineWidth, true);
  renderer.drawArc(radius, cx, cy, 1, 1, lineWidth, true);
  renderer.drawArc(radius, cx, cy, -1, 1, lineWidth, true);
}
}  // namespace
```

Step 7's code already calls `drawCircle(renderer, ...)` with this signature. Because it is a file-local helper in an anonymous namespace, it must be **defined above `renderRadar()`** in the file — put the namespace block right after the includes.

Add the remaining i18n key to `lib/I18n/translations/english.yaml`:
```yaml
STR_RADAR_RANGE_OUTER_FORMAT: "%dmi"
```

Also add `#include <algorithm>` to the `.cpp` for `std::min` if not already present, and confirm `UI_10_FONT_ID` exists in `src/fontIds.h` — if it does not, use `UI_10_FONT_ID` for the ring and compass labels instead.

- [ ] **Step 9: Verify i18n, build, and column limits**

```bash
python3 scripts/gen_i18n.py lib/I18n/translations /tmp/i18n-check --verbose
```
Expected: zero missing keys.

```bash
~/.platformio/penv/bin/pio run
```
Expected: SUCCESS, no new warnings.

```bash
awk 'length > 120 {print FILENAME":"FNR}' src/activities/flights/NearbyFlightsActivity.cpp src/activities/flights/NearbyFlightsActivity.h
```
Expected: no output.

- [ ] **Step 10: Run the host suite and commit**

Run: `ctest --test-dir build/test --output-on-failure`
Expected: 178 passing, unchanged.

```bash
git add src/activities/flights/NearbyFlightsActivity.h src/activities/flights/NearbyFlightsActivity.cpp lib/I18n/translations/english.yaml
git commit -m "feat: add radar view to Nearby Flights"
```

- [ ] **Step 11: Flag for on-device verification**

🔲 **Device**: Flash and check — from the list, does the third button open the radar? Are the rings circular (not elliptical or filled solid)? Do triangles point plausibly? Does the selected one stand out? Does Next cycle and wrap? Does long-press Confirm refresh while short press opens detail? Try all four orientations, since `radiusPx` fits to the tighter axis.

---

## Task 5: Aircraft type on the detail screen

**Files:**
- Modify: `src/activities/flights/NearbyFlightsActivity.h`
- Modify: `src/activities/flights/NearbyFlightsActivity.cpp`
- Modify: `lib/I18n/translations/english.yaml`

**Interfaces:**
- Consumes: `AircraftInfoParser` (Task 2), `AdsbdbClient::fetchAircraftInfo` (Task 3).

- [ ] **Step 1: Add i18n keys**

Add to `lib/I18n/translations/english.yaml`:
```yaml
STR_AIRCRAFT_TYPE_FORMAT: "%s %s"
STR_AIRCRAFT_REG_FORMAT: "Reg: %s"
STR_AIRCRAFT_TYPE_UNKNOWN: "Type: unknown"
STR_AIRCRAFT_TYPE_UNAVAILABLE: "Type: unavailable"
```

- [ ] **Step 2: Add members and the lookup declaration**

In `src/activities/flights/NearbyFlightsActivity.h`, add the include at the top:
```cpp
#include "AircraftInfoParser.h"
```

Add to the private members:
```cpp
  // Single-slot cache: going detail -> back -> same detail must not re-fetch.
  // Deliberately not an N-entry LRU -- one slot covers the common
  // back-and-forth and the bookkeeping for more is not earned.
  AircraftInfoParser aircraftParser;
  char aircraftInfoIcao24[7] = {0};  // which icao24 aircraftParser currently holds
  bool aircraftLookupFailed = false;
```

Add to the private methods:
```cpp
  // Fetches the selected aircraft's registry record unless it is already cached.
  void ensureAircraftInfo();
```

- [ ] **Step 3: Implement the lookup**

Add to `src/activities/flights/NearbyFlightsActivity.cpp`, before `renderDetail()`:

```cpp
void NearbyFlightsActivity::ensureAircraftInfo() {
  if (parser.matchCount() == 0) return;
  const auto& m = parser.matchAt(static_cast<size_t>(selectedIndex));
  if (m.icao24[0] == '\0') return;

  // Already cached for this aircraft.
  if (strcmp(aircraftInfoIcao24, m.icao24) == 0) return;

  aircraftParser.reset();
  aircraftLookupFailed = false;
  strncpy(aircraftInfoIcao24, m.icao24, sizeof(aircraftInfoIcao24) - 1);
  aircraftInfoIcao24[sizeof(aircraftInfoIcao24) - 1] = '\0';

  // Logged separately so the console distinguishes a transport failure from a
  // JSON parse failure -- both otherwise render the same on screen.
  const bool ok = AdsbdbClient::fetchAircraftInfo(m.icao24, aircraftParser);
  if (!ok) {
    LOG_ERR("FLIGHTS", "AdsbdbClient::fetchAircraftInfo transport failure for %s", m.icao24);
  }
  if (aircraftParser.hasError()) {
    LOG_ERR("FLIGHTS", "AircraftInfoParser reported a JSON parse error for %s", m.icao24);
  }
  aircraftLookupFailed = !ok || aircraftParser.hasError();
  if (!aircraftLookupFailed) {
    LOG_DBG("FLIGHTS", "Aircraft %s: found=%d type=%s", m.icao24, aircraftParser.info().found ? 1 : 0,
            aircraftParser.info().icaoType);
  }
}
```

Add `#include <cstring>` to the `.cpp` if not already present, and `#include "network/AdsbdbClient.h"`.

- [ ] **Step 4: Call the lookup on entering DETAIL**

There are two places `state = FlightsState::DETAIL;` is set — inside `handleConfirmPressOrRefresh()` (added in Task 4) and in the LIST case's `handleListTouch` `Activated` branch. Add `ensureAircraftInfo();` immediately before each `requestUpdate()` that follows those assignments.

In `handleConfirmPressOrRefresh`:
```cpp
    if (wasShortPress && hasMatches) {
      state = FlightsState::DETAIL;
      ensureAircraftInfo();
      requestUpdate();
    }
```

In the LIST case's touch handling:
```cpp
          case ListTouchResult::Activated:
            state = FlightsState::DETAIL;
            ensureAircraftInfo();
            requestUpdate();
            return;
```

- [ ] **Step 5: Render the type line**

`renderDetail()` is declared `const`, and `ensureAircraftInfo()` mutates — which is why the lookup happens at the transition, not during render. Keep `renderDetail()` const.

In `renderDetail()`, after the ICAO24 line and before the data-age line, add:

```cpp
  const auto& acInfo = aircraftParser.info();
  if (aircraftLookupFailed) {
    renderer.drawText(UI_10_FONT_ID, x, y, tr(STR_AIRCRAFT_TYPE_UNAVAILABLE), true);
    y += metrics.listRowHeight;
  } else if (!acInfo.found) {
    renderer.drawText(UI_10_FONT_ID, x, y, tr(STR_AIRCRAFT_TYPE_UNKNOWN), true);
    y += metrics.listRowHeight;
  } else {
    if (acInfo.manufacturer[0] || acInfo.icaoType[0]) {
      snprintf(line, sizeof(line), tr(STR_AIRCRAFT_TYPE_FORMAT), acInfo.manufacturer, acInfo.icaoType);
      renderer.drawText(UI_10_FONT_ID, x, y, line, true);
      y += metrics.listRowHeight;
    }
    if (acInfo.registration[0]) {
      snprintf(line, sizeof(line), tr(STR_AIRCRAFT_REG_FORMAT), acInfo.registration);
      renderer.drawText(UI_10_FONT_ID, x, y, line, true);
      y += metrics.listRowHeight;
    }
  }
```

- [ ] **Step 6: Verify i18n, build, and columns**

```bash
python3 scripts/gen_i18n.py lib/I18n/translations /tmp/i18n-check --verbose
```
Expected: zero missing keys.

```bash
~/.platformio/penv/bin/pio run
```
Expected: SUCCESS, no new warnings.

```bash
awk 'length > 120 {print FILENAME":"FNR}' src/activities/flights/NearbyFlightsActivity.cpp src/activities/flights/NearbyFlightsActivity.h
```
Expected: no output.

- [ ] **Step 7: Run the host suite and commit**

Run: `ctest --test-dir build/test --output-on-failure`
Expected: 178 passing, unchanged.

```bash
git add src/activities/flights/NearbyFlightsActivity.h src/activities/flights/NearbyFlightsActivity.cpp lib/I18n/translations/english.yaml
git commit -m "feat: show aircraft manufacturer, type, and registration on detail screen"
```

- [ ] **Step 8: Flag for on-device verification**

🔲 **Device**: Open a detail screen — does the type line appear within ~1s? Try an aircraft adsbdb does not know (military or GA often miss) and confirm "Type: unknown" rather than a hang or a blank. Confirm going back and re-opening the same aircraft does **not** re-fetch (watch the serial log for a second `[ADSBDB] Fetching:` line — there should not be one). Confirm a different aircraft does fetch.

---

## Self-Review Notes

- **Spec coverage:** radar placement as a toggle (Task 4), heading triangles (Tasks 1+4), fixed ⅓/⅔/full rings (Task 4), selection cycling with the detail screen reused (Tasks 4+5), entry still on the list (Task 4 — `fetchFlights` still sets `LIST`), adsbdb on the detail screen only (Task 5), `manufacturer`+`icao_type` (Tasks 2+5), object-vs-string response handling (Task 2), single-slot cache (Task 5), button layout (Task 4).
- **Type consistency:** `GeoMath::ScreenPoint`, `polarToScreen`, `headingTriangle`, `AircraftInfo` field names, and `AdsbdbClient::fetchAircraftInfo` are used identically in the tasks that define and consume them.
- **Known gaps carried from the spec, deliberately not fixed here:** no "Up" button (all four slots spent); no decluttering of overlapping marks; `fillPolygon`'s per-call malloc accepted with documented reasoning.
- **Ordering:** Tasks 1–3 are independent and testable alone. Task 4 depends on Task 1. Task 5 depends on Tasks 2, 3, and 4 (it edits code Task 4 introduces).
