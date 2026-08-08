# Zip Code Home Location Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the user set their Flight Tracker home location by typing a 5-digit US zip code, geocoded once via Zippopotam.us into the existing lat/lon settings fields.

**Architecture:** A `ZipGeocodeParser` (SAX consumer of `StreamingJsonParser`, same shape as `AircraftInfoParser`) plus a `ZipGeocodeClient` (thin network wrapper, same shape as `AdsbdbClient`) do the lookup. `FlightTrackerSettingsActivity` gains a 4th row and a `CHECK_WIFI → WIFI_SELECTION → LOADING` state machine copied from `NearbyFlightsActivity`, reusing `WifiSelectionActivity` for the connect UI. On success the geocoded lat/lon overwrite the existing `flightTrackerHomeLat`/`Lon` fields; nothing downstream of `AlmanacSettings` changes.

**Tech Stack:** ESP32-C3 / Arduino-ESP32 / PlatformIO, C++20, hand-rolled `StreamingJsonParser`, GoogleTest (host tests via CMake/CTest).

## Global Constraints

- Country scope: US only. Zip must be exactly 5 ASCII digits; hardcoded to Zippopotam's `/us/` endpoint (spec: Decisions table).
- Geocoding source: `https://api.zippopotam.us/us/<zip>` — free, keyless, HTTPS. Verified live 2026-08-07: `GET /us/90210` → HTTP 200, `GET /us/00000` → HTTP 404 with empty `{}` body.
- **Load-bearing:** `latitude`/`longitude` in the response are JSON **strings** (`"34.0901"`), not numbers. Object keys contain spaces (`"place name"`, `"state abbreviation"`) — ordinary valid JSON keys, matched verbatim, not `snake_case`.
- No host test for `ZipGeocodeClient` or the activity changes — matches the established convention (`HttpDownloader.h` pulls Arduino/FreeRTOS headers unavailable on the host; no `Activity` in this codebase has a host test). Verified by firmware build instead.
- `.clang-format` `ColumnLimit` is 120. Format with `./bin/clang-format-fix -g` (picks `clang-format-21`; do not use a bare/Apple `clang-format`) before every commit.
- `pio` lives at `~/.platformio/penv/bin/pio` (not on PATH in this environment).
- Host test baseline before this plan: **197/197 passing** (`cmake -S test -B build/test && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j`).
- i18n: add new keys only to `lib/I18n/translations/english.yaml` (the reference; other languages fall back to English automatically). Regenerate with `python3 scripts/gen_i18n.py --verbose` — the three generated files are gitignored, never hand-edited, never committed.

---

### Task 1: `ZipGeocodeParser` + host tests

**Files:**
- Create: `lib/JsonParser/ZipGeocodeParser.h`
- Create: `lib/JsonParser/ZipGeocodeParser.cpp`
- Create: `test/zip_geocode_parser/CMakeLists.txt`
- Create: `test/zip_geocode_parser/ZipGeocodeParserTest.cpp`
- Modify: `test/CMakeLists.txt:53` (append `add_subdirectory(zip_geocode_parser)` after the existing `add_subdirectory(tesserae_frame)` line)

**Interfaces:**
- Produces:
  ```cpp
  struct ZipGeocodeResult {
    bool found = false;
    double latitude = 0;
    double longitude = 0;
    char placeName[32] = {0};
    char stateAbbrev[4] = {0};
  };

  class ZipGeocodeParser {
   public:
    ZipGeocodeParser();
    ZipGeocodeParser(const ZipGeocodeParser&) = delete;
    ZipGeocodeParser& operator=(const ZipGeocodeParser&) = delete;
    void reset();
    void feed(const char* data, size_t len);
    bool hasError() const;
    const ZipGeocodeResult& geocode() const;
  };
  ```
  Task 2 (`ZipGeocodeClient`) and Task 4 (`FlightTrackerSettingsActivity`) consume this exact API — note the accessor is named `geocode()` (not `info()`, to avoid colliding with the private `result` field, same trick `AircraftInfoParser` plays with `info()`).

- [ ] **Step 1: Write the test file first**

Create `test/zip_geocode_parser/ZipGeocodeParserTest.cpp`:

```cpp
#include "ZipGeocodeParser.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace {
// Captured verbatim via `curl https://api.zippopotam.us/us/90210` on 2026-08-07.
constexpr char SUCCESS_JSON[] =
    R"({"country": "United States", "country abbreviation": "US", "post code": "90210", )"
    R"("places": [{"place name": "Beverly Hills", "longitude": "-118.4065", "latitude": "34.0901", )"
    R"("state": "California", "state abbreviation": "CA"}]})";

// Captured verbatim via `curl https://api.zippopotam.us/us/00000` on 2026-08-07 (HTTP 404).
constexpr char NOT_FOUND_JSON[] = "{}";

void feedAll(ZipGeocodeParser& p, const char* json) { p.feed(json, strlen(json)); }
}  // namespace

TEST(ZipGeocodeParserTest, ParsesSuccessResponse) {
  ZipGeocodeParser p;
  feedAll(p, SUCCESS_JSON);
  ASSERT_FALSE(p.hasError());
  const auto& r = p.geocode();
  EXPECT_TRUE(r.found);
  EXPECT_DOUBLE_EQ(r.latitude, 34.0901);
  EXPECT_DOUBLE_EQ(r.longitude, -118.4065);
  EXPECT_STREQ(r.placeName, "Beverly Hills");
  EXPECT_STREQ(r.stateAbbrev, "CA");
}

TEST(ZipGeocodeParserTest, EmptyObjectIsNotFoundNotError) {
  ZipGeocodeParser p;
  feedAll(p, NOT_FOUND_JSON);
  EXPECT_FALSE(p.hasError());
  EXPECT_FALSE(p.geocode().found);
}

TEST(ZipGeocodeParserTest, IgnoresIrrelevantTopLevelAndPlaceFields) {
  ZipGeocodeParser p;
  // "country", "country abbreviation", "post code" at top level and "state"
  // (the full name, not the abbreviation) inside the place object are all
  // present in the real payload but never captured -- this just confirms
  // their presence doesn't break parsing of the fields that matter.
  feedAll(p, SUCCESS_JSON);
  ASSERT_FALSE(p.hasError());
  EXPECT_TRUE(p.geocode().found);
}

TEST(ZipGeocodeParserTest, MissingOptionalFieldsNoError) {
  ZipGeocodeParser p;
  constexpr char json[] = R"({"places": [{"latitude": "34.0901", "longitude": "-118.4065"}]})";
  feedAll(p, json);
  ASSERT_FALSE(p.hasError());
  const auto& r = p.geocode();
  EXPECT_TRUE(r.found);
  EXPECT_DOUBLE_EQ(r.latitude, 34.0901);
  EXPECT_DOUBLE_EQ(r.longitude, -118.4065);
  EXPECT_STREQ(r.placeName, "");
  EXPECT_STREQ(r.stateAbbrev, "");
}

TEST(ZipGeocodeParserTest, MissingCoordinatesNotFound) {
  ZipGeocodeParser p;
  // A place object that closes cleanly but never carried lat/lon must not
  // present as found -- guards against writing 0,0 (Null Island) into the
  // home location.
  constexpr char json[] = R"({"places": [{"place name": "X"}]})";
  feedAll(p, json);
  ASSERT_FALSE(p.hasError());
  EXPECT_FALSE(p.geocode().found);
}

TEST(ZipGeocodeParserTest, TruncatedBodyNeverPresentsAsFound) {
  ZipGeocodeParser p;
  // Cuts off mid-object -- places[0] never closes.
  constexpr char json[] = R"({"places": [{"place name": "Beverly Hills", "latitude": "34.0901")";
  feedAll(p, json);
  EXPECT_FALSE(p.geocode().found);
}

TEST(ZipGeocodeParserTest, OverlongValueTruncatesWithoutOverrun) {
  ZipGeocodeParser p;
  const std::string longName(100, 'A');
  const std::string json =
      R"({"places": [{"place name": ")" + longName + R"(", "latitude": "1", "longitude": "2"}]})";
  feedAll(p, json.c_str());
  ASSERT_FALSE(p.hasError());
  const auto& r = p.geocode();
  EXPECT_TRUE(r.found);
  EXPECT_EQ(strlen(r.placeName), sizeof(r.placeName) - 1);
}

TEST(ZipGeocodeParserTest, ChunkedFeedProducesSameResult) {
  ZipGeocodeParser p;
  const size_t len = strlen(SUCCESS_JSON);
  for (size_t i = 0; i < len; i += 7) {
    const size_t chunk = std::min<size_t>(7, len - i);
    p.feed(SUCCESS_JSON + i, chunk);
  }
  ASSERT_FALSE(p.hasError());
  const auto& r = p.geocode();
  EXPECT_TRUE(r.found);
  EXPECT_DOUBLE_EQ(r.latitude, 34.0901);
  EXPECT_DOUBLE_EQ(r.longitude, -118.4065);
  EXPECT_STREQ(r.placeName, "Beverly Hills");
}

TEST(ZipGeocodeParserTest, ResetClearsPreviousResult) {
  ZipGeocodeParser p;
  feedAll(p, SUCCESS_JSON);
  ASSERT_TRUE(p.geocode().found);
  p.reset();
  EXPECT_FALSE(p.geocode().found);
  EXPECT_DOUBLE_EQ(p.geocode().latitude, 0);
  EXPECT_STREQ(p.geocode().placeName, "");
}
```

Create `test/zip_geocode_parser/CMakeLists.txt`:

```cmake
add_executable(ZipGeocodeParserTest
  ZipGeocodeParserTest.cpp
  ${REPO_ROOT}/lib/JsonParser/ZipGeocodeParser.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
)

target_include_directories(ZipGeocodeParserTest PRIVATE
  ${REPO_ROOT}/lib/JsonParser
)

target_link_libraries(ZipGeocodeParserTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ZipGeocodeParserTest)
```

Modify `test/CMakeLists.txt` — append after line 53 (`add_subdirectory(tesserae_frame)`):

```cmake
add_subdirectory(zip_geocode_parser)
```

- [ ] **Step 2: Run the build to verify it fails**

```bash
cmake -S test -B build/test && cmake --build build/test
```

Expected: FAIL — `ZipGeocodeParser.h`/`.cpp` don't exist yet (`fatal error: 'ZipGeocodeParser.h' file not found` or a missing-source-file CMake error).

- [ ] **Step 3: Write `ZipGeocodeParser.h`**

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

#include "StreamingJsonParser.h"

// One zip code's geocoded location, as returned by api.zippopotam.us.
struct ZipGeocodeResult {
  bool found = false;
  double latitude = 0;
  double longitude = 0;
  char placeName[32] = {0};   // e.g. "Beverly Hills" -- for the confirmation message
  char stateAbbrev[4] = {0};  // e.g. "CA"
};

// Streams api.zippopotam.us's /us/<zip> response.
//
// The endpoint returns {"places":[{...}], ...} on success; an unknown zip
// answers HTTP 404 with an empty `{}` body, which this parser also handles
// cleanly (found stays false, no error). Only malformed JSON sets hasError().
//
// Load-bearing detail: `latitude`/`longitude` are JSON *strings*
// ("latitude": "34.0901"), not numbers -- consumed via the string callback
// and converted with strtod, not the number callback. Object keys in this
// API also contain spaces ("place name", "state abbreviation") -- ordinary,
// valid JSON keys, matched verbatim.
//
// Fixed-size buffers throughout: no allocation. Same shape as
// AircraftInfoParser -- dominated by the embedded StreamingJsonParser (a
// 512-byte token buffer plus a 32-byte nesting stack); this class's own
// fields (ZipGeocodeResult + a few bytes of parse-position state) add well
// under 100 bytes on top of that.
class ZipGeocodeParser {
 public:
  ZipGeocodeParser();

  ZipGeocodeParser(const ZipGeocodeParser&) = delete;
  ZipGeocodeParser& operator=(const ZipGeocodeParser&) = delete;

  // Clears all state. Call before each lookup.
  void reset();

  // Feeds a chunk of the HTTP response body.
  void feed(const char* data, size_t len);

  bool hasError() const { return parser.hasError(); }

  const ZipGeocodeResult& geocode() const { return result; }

 private:
  enum class Position : uint8_t { TOP_LEVEL, IN_PLACES, IN_PLACE };
  enum class LastKey : uint8_t { NONE, PLACES, PLACE_NAME, LONGITUDE, LATITUDE, STATE_ABBREV };

  static void sOnKey(void* ctx, const char* key, size_t len);
  static void sOnString(void* ctx, const char* value, size_t len);
  static void sOnNumber(void* ctx, const char* value, size_t len);
  static void sOnBool(void* ctx, bool value);
  static void sOnNull(void* ctx);
  static void sOnObjectStart(void* ctx);
  static void sOnObjectEnd(void* ctx);
  static void sOnArrayStart(void* ctx);
  static void sOnArrayEnd(void* ctx);

  StreamingJsonParser parser;

  Position position = Position::TOP_LEVEL;
  LastKey lastKey = LastKey::NONE;
  uint8_t depth = 0;
  bool sawFirstPlace = false;  // true once places[0] has been entered -- later array entries are ignored
  bool sawLatitude = false;    // both required for `found` -- see sOnObjectEnd
  bool sawLongitude = false;

  ZipGeocodeResult result;
};
```

- [ ] **Step 4: Write `ZipGeocodeParser.cpp`**

```cpp
#include "ZipGeocodeParser.h"

#include <cstdlib>
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

ZipGeocodeParser::ZipGeocodeParser()
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                            sOnArrayStart, sOnArrayEnd}) {}

void ZipGeocodeParser::reset() {
  parser.reset();
  position = Position::TOP_LEVEL;
  lastKey = LastKey::NONE;
  depth = 0;
  sawFirstPlace = false;
  sawLatitude = false;
  sawLongitude = false;
  result = ZipGeocodeResult{};
}

void ZipGeocodeParser::feed(const char* data, size_t len) { parser.feed(data, len); }

void ZipGeocodeParser::sOnKey(void* ctx, const char* key, size_t len) {
  auto* self = static_cast<ZipGeocodeParser*>(ctx);
  switch (self->position) {
    case Position::TOP_LEVEL:
      self->lastKey = keyIs(key, len, "places") ? LastKey::PLACES : LastKey::NONE;
      break;
    case Position::IN_PLACES:
      // Arrays don't have keys of their own; a key callback here would only
      // fire for something unexpected nested inside a non-first element.
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_PLACE:
      if (keyIs(key, len, "place name")) {
        self->lastKey = LastKey::PLACE_NAME;
      } else if (keyIs(key, len, "longitude")) {
        self->lastKey = LastKey::LONGITUDE;
      } else if (keyIs(key, len, "latitude")) {
        self->lastKey = LastKey::LATITUDE;
      } else if (keyIs(key, len, "state abbreviation")) {
        self->lastKey = LastKey::STATE_ABBREV;
      } else {
        // Deliberately ignores "state" (the full name) and any other field
        // -- stateAbbrev is the compact one the settings row needs.
        self->lastKey = LastKey::NONE;
      }
      break;
  }
}

void ZipGeocodeParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<ZipGeocodeParser*>(ctx);
  switch (self->position) {
    case Position::TOP_LEVEL:
      self->depth++;
      break;
    case Position::IN_PLACES:
      if (!self->sawFirstPlace) {
        self->position = Position::IN_PLACE;
        self->sawFirstPlace = true;
        // Reset: depth now tracks nesting within IN_PLACE, not the
        // leftover count from the array.
        self->depth = 0;
      } else {
        // A second (or later) places[] entry -- ignored, but still needs
        // balanced depth tracking so its close doesn't miscount.
        self->depth++;
      }
      break;
    case Position::IN_PLACE:
      self->depth++;
      break;
  }
  self->lastKey = LastKey::NONE;
}

void ZipGeocodeParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<ZipGeocodeParser*>(ctx);
  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_PLACES:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_PLACE:
      if (self->depth > 0) {
        self->depth--;
      } else {
        // A complete places[0] object closed -- but only trustworthy if it
        // actually carried both coordinates. A record missing lat/lon must
        // not silently write 0,0 (Null Island) into the home location.
        self->result.found = self->sawLatitude && self->sawLongitude;
        self->position = Position::IN_PLACES;
      }
      break;
  }
  self->lastKey = LastKey::NONE;
}

void ZipGeocodeParser::sOnArrayStart(void* ctx) {
  auto* self = static_cast<ZipGeocodeParser*>(ctx);
  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->lastKey == LastKey::PLACES) {
        self->position = Position::IN_PLACES;
        self->depth = 0;
      } else {
        self->depth++;
      }
      break;
    case Position::IN_PLACES:
    case Position::IN_PLACE:
      self->depth++;
      break;
  }
  self->lastKey = LastKey::NONE;
}

void ZipGeocodeParser::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<ZipGeocodeParser*>(ctx);
  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_PLACES:
      if (self->depth > 0) {
        self->depth--;
      } else {
        // The places array closed -- nothing further to capture.
        self->position = Position::TOP_LEVEL;
      }
      break;
    case Position::IN_PLACE:
      if (self->depth > 0) self->depth--;
      break;
  }
  self->lastKey = LastKey::NONE;
}

void ZipGeocodeParser::sOnString(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<ZipGeocodeParser*>(ctx);
  if (self->position == Position::IN_PLACE && self->depth == 0) {
    switch (self->lastKey) {
      case LastKey::PLACE_NAME:
        safeCopy(self->result.placeName, sizeof(self->result.placeName), value, len);
        break;
      case LastKey::LONGITUDE: {
        char buf[24];
        safeCopy(buf, sizeof(buf), value, len);
        self->result.longitude = strtod(buf, nullptr);
        self->sawLongitude = true;
        break;
      }
      case LastKey::LATITUDE: {
        char buf[24];
        safeCopy(buf, sizeof(buf), value, len);
        self->result.latitude = strtod(buf, nullptr);
        self->sawLatitude = true;
        break;
      }
      case LastKey::STATE_ABBREV:
        safeCopy(self->result.stateAbbrev, sizeof(self->result.stateAbbrev), value, len);
        break;
      default:
        break;
    }
  }
  self->lastKey = LastKey::NONE;
}

void ZipGeocodeParser::sOnNumber(void* ctx, const char* /*value*/, size_t /*len*/) {
  static_cast<ZipGeocodeParser*>(ctx)->lastKey = LastKey::NONE;
}

void ZipGeocodeParser::sOnBool(void* ctx, bool /*value*/) {
  static_cast<ZipGeocodeParser*>(ctx)->lastKey = LastKey::NONE;
}

void ZipGeocodeParser::sOnNull(void* ctx) { static_cast<ZipGeocodeParser*>(ctx)->lastKey = LastKey::NONE; }
```

Note on `value`/`len` in `sOnString`: per this codebase's `string_view`/C-API rule, the callback's `value` pointer is not guaranteed null-terminated, so `latitude`/`longitude` are copied into a local null-terminated `buf` before `strtod` — never call `strtod` on `value` directly.

- [ ] **Step 5: Run the build and tests, verify they pass**

```bash
cmake -S test -B build/test && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j
```

Expected: all `ZipGeocodeParserTest.*` cases PASS, and the total count is **206/206** (197 baseline + 9 new). If any fail, fix `ZipGeocodeParser.cpp` and re-run — do not adjust the tests to match a wrong implementation.

- [ ] **Step 6: Format and commit**

```bash
./bin/clang-format-fix -g
git add lib/JsonParser/ZipGeocodeParser.h lib/JsonParser/ZipGeocodeParser.cpp \
        test/zip_geocode_parser/CMakeLists.txt test/zip_geocode_parser/ZipGeocodeParserTest.cpp \
        test/CMakeLists.txt
git commit -m "feat: add ZipGeocodeParser for Zippopotam responses"
```

---

### Task 2: `ZipGeocodeClient`

**Files:**
- Create: `src/network/ZipGeocodeClient.h`
- Create: `src/network/ZipGeocodeClient.cpp`

**Interfaces:**
- Consumes: `ZipGeocodeParser` (Task 1) — `reset()`, `feed(const char*, size_t)`, `hasError()`, `geocode()`. `HttpDownloader::fetchUrl(const std::string& url, const DataCallback& onData, const std::string& username = "", const std::string& password = "", int* outStatus = nullptr)` from `src/network/HttpDownloader.h:48-49`.
- Produces:
  ```cpp
  class ZipGeocodeClient {
   public:
    enum class Result { Ok, NotFound, Error };
    static Result geocode(const char* zip, ZipGeocodeParser& parser);
  };
  ```
  Task 4 consumes this exact `Result` enum and `geocode()` signature.

- [ ] **Step 1: Write `ZipGeocodeClient.h`**

```cpp
#pragma once

#include "ZipGeocodeParser.h"

// Geocodes a 5-digit US zip code to a lat/lon via api.zippopotam.us's free,
// keyless API.
class ZipGeocodeClient {
 public:
  enum class Result {
    // HTTP 200. Check parser.hasError() for a JSON parse failure and
    // parser.geocode().found for a "found" record (should always be true on
    // a 200 for this API, but the parser makes no such assumption).
    Ok,
    // Zippopotam answered HTTP 404 (an empty `{}` body): no record for this
    // zip. A normal, expected outcome for a well-formed but nonexistent zip
    // -- not a transport or parse failure. parser is left in its reset()
    // state (the 404 body is never fed to it).
    NotFound,
    // Transport/HTTP failure: DNS, TLS, timeout, or any status other than
    // 200/404. parser is left in its reset() state.
    Error,
  };

  // zip must be exactly 5 ASCII digits (validated by the caller). parser
  // must already be reset() before calling.
  static Result geocode(const char* zip, ZipGeocodeParser& parser);
};
```

- [ ] **Step 2: Write `ZipGeocodeClient.cpp`**

```cpp
#include "ZipGeocodeClient.h"

#include <Logging.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "network/HttpDownloader.h"

ZipGeocodeClient::Result ZipGeocodeClient::geocode(const char* zip, ZipGeocodeParser& parser) {
  if (!zip || strlen(zip) != 5) {
    LOG_ERR("ZIPGEO", "geocode called with a malformed zip");
    return Result::Error;
  }

  char url[64];
  snprintf(url, sizeof(url), "https://api.zippopotam.us/us/%s", zip);

  LOG_DBG("ZIPGEO", "Fetching: %s", url);

  int status = 0;
  const bool ok = HttpDownloader::fetchUrl(
      std::string(url),
      [&parser](const uint8_t* data, size_t len) {
        parser.feed(reinterpret_cast<const char*>(data), len);
        return !parser.hasError();
      },
      "", "", &status);

  if (ok) return Result::Ok;

  if (status == 404) {
    // Zippopotam's documented "no record" response -- a normal outcome for
    // a well-formed but nonexistent zip, not an error.
    LOG_DBG("ZIPGEO", "No record for zip %s (HTTP 404)", zip);
    return Result::NotFound;
  }
  return Result::Error;
}
```

- [ ] **Step 3: Verify it compiles as part of the firmware**

```bash
~/.platformio/penv/bin/pio run -e default
```

Expected: build succeeds with no errors or warnings touching `ZipGeocodeClient.*`. Nothing calls `ZipGeocodeClient::geocode` yet (Task 4 wires it in) — PlatformIO compiles every `.cpp` under `src/` regardless of whether it's referenced elsewhere, so this alone verifies the file is syntactically and semantically correct against `HttpDownloader.h`'s real signature.

- [ ] **Step 4: Format and commit**

```bash
./bin/clang-format-fix -g
git add src/network/ZipGeocodeClient.h src/network/ZipGeocodeClient.cpp
git commit -m "feat: add ZipGeocodeClient for zip-to-latlon lookups"
```

---

### Task 3: `AlmanacSettings` field, `SettingsList.h` registration, i18n keys

**Files:**
- Modify: `src/AlmanacSettings.h:242` (insert new field after `flightTrackerHomeLon`)
- Modify: `src/SettingsList.h:326` (insert new registration after `flightTrackerHomeLon`'s)
- Modify: `lib/I18n/translations/english.yaml` (insert new keys)

**Interfaces:**
- Produces: `SETTINGS.flightTrackerHomeZip` (`char[6]`), and i18n keys `StrId::STR_FLIGHT_TRACKER_HOME_ZIP`, `StrId::STR_INVALID_ZIP_CODE`, `StrId::STR_ZIP_NOT_FOUND`, `StrId::STR_ZIP_LOOKUP_ERROR`, `StrId::STR_ZIP_LOOKUP_LOADING_FORMAT`. Task 4 consumes all of these by exact name.

- [ ] **Step 1: Add the settings field**

In `src/AlmanacSettings.h`, the current block at lines 238-242 reads:

```cpp
  // Flight tracker home location, decimal degrees as text ("" = unset). Manually
  // edited via FlightTrackerSettingsActivity; kept out of the generic Settings UI
  // (category-less), same pattern as opdsDownloadFolder.
  char flightTrackerHomeLat[16] = "";
  char flightTrackerHomeLon[16] = "";
```

Change it to:

```cpp
  // Flight tracker home location, decimal degrees as text ("" = unset). Manually
  // edited via FlightTrackerSettingsActivity; kept out of the generic Settings UI
  // (category-less), same pattern as opdsDownloadFolder.
  char flightTrackerHomeLat[16] = "";
  char flightTrackerHomeLon[16] = "";
  // 5-digit US zip code, geocoded to Lat/Lon above on submit ("" = unset).
  // Persisted so the settings row shows the last entry rather than resetting
  // to blank; Lat/Lon remain the values NearbyFlightsActivity actually reads.
  char flightTrackerHomeZip[6] = "";
```

(Leave the interleaved Tesserae block that follows at lines 243-249 untouched.)

- [ ] **Step 2: Register the field for persistence**

In `src/SettingsList.h`, the current block at lines 323-326 reads:

```cpp
        SettingInfo::String(StrId::STR_FLIGHT_TRACKER_HOME_LAT, &SETTINGS.flightTrackerHomeLat[0],
                            sizeof(SETTINGS.flightTrackerHomeLat), "flightTrackerHomeLat"),
        SettingInfo::String(StrId::STR_FLIGHT_TRACKER_HOME_LON, &SETTINGS.flightTrackerHomeLon[0],
                            sizeof(SETTINGS.flightTrackerHomeLon), "flightTrackerHomeLon"),
```

Change it to:

```cpp
        SettingInfo::String(StrId::STR_FLIGHT_TRACKER_HOME_LAT, &SETTINGS.flightTrackerHomeLat[0],
                            sizeof(SETTINGS.flightTrackerHomeLat), "flightTrackerHomeLat"),
        SettingInfo::String(StrId::STR_FLIGHT_TRACKER_HOME_LON, &SETTINGS.flightTrackerHomeLon[0],
                            sizeof(SETTINGS.flightTrackerHomeLon), "flightTrackerHomeLon"),
        SettingInfo::String(StrId::STR_FLIGHT_TRACKER_HOME_ZIP, &SETTINGS.flightTrackerHomeZip[0],
                            sizeof(SETTINGS.flightTrackerHomeZip), "flightTrackerHomeZip"),
```

- [ ] **Step 3: Add i18n keys**

In `lib/I18n/translations/english.yaml`, the current block at lines 382-385 reads:

```yaml
STR_FLIGHT_TRACKER: "Flight Tracker"
STR_FLIGHT_TRACKER_HOME_LAT: "Home Latitude"
STR_FLIGHT_TRACKER_HOME_LON: "Home Longitude"
STR_FLIGHT_TRACKER_RADIUS: "Search Radius (mi)"
```

Change it to:

```yaml
STR_FLIGHT_TRACKER: "Flight Tracker"
STR_FLIGHT_TRACKER_HOME_ZIP: "Home Zip Code"
STR_FLIGHT_TRACKER_HOME_LAT: "Home Latitude"
STR_FLIGHT_TRACKER_HOME_LON: "Home Longitude"
STR_FLIGHT_TRACKER_RADIUS: "Search Radius (mi)"
```

The current block at line 413 reads:

```yaml
STR_INVALID_COORDINATE: "Invalid coordinate"
```

Change it to:

```yaml
STR_INVALID_COORDINATE: "Invalid coordinate"
STR_INVALID_ZIP_CODE: "Invalid zip code"
STR_ZIP_NOT_FOUND: "Zip code not found"
STR_ZIP_LOOKUP_ERROR: "Zip lookup failed"
STR_ZIP_LOOKUP_LOADING_FORMAT: "Looking up %s..."
```

- [ ] **Step 4: Regenerate i18n and verify**

```bash
python3 scripts/gen_i18n.py --verbose
grep -n "STR_FLIGHT_TRACKER_HOME_ZIP\|STR_INVALID_ZIP_CODE\|STR_ZIP_NOT_FOUND\|STR_ZIP_LOOKUP_ERROR\|STR_ZIP_LOOKUP_LOADING_FORMAT" lib/I18n/I18nKeys.h
```

Expected: all 5 keys present in the grep output. `I18nKeys.h`, `I18nStrings.h`, `I18nStrings.cpp` are gitignored — do not `git add` them.

- [ ] **Step 5: Verify the firmware still builds**

```bash
~/.platformio/penv/bin/pio run -e default
```

Expected: succeeds. `SettingsList.h` now references the new `StrId` and `SETTINGS.flightTrackerHomeZip` — this build is what actually proves both the field and the i18n key exist and match.

- [ ] **Step 6: Format and commit**

```bash
./bin/clang-format-fix -g
git add src/AlmanacSettings.h src/SettingsList.h lib/I18n/translations/english.yaml
git commit -m "feat: add flightTrackerHomeZip setting and zip-code i18n strings"
```

---

### Task 4: `FlightTrackerSettingsActivity` — 4th row, Wi-Fi flow, wiring

**Files:**
- Modify: `src/activities/settings/FlightTrackerSettingsActivity.h` (full replacement below)
- Modify: `src/activities/settings/FlightTrackerSettingsActivity.cpp` (full replacement below)

**Interfaces:**
- Consumes: `ZipGeocodeParser` (Task 1), `ZipGeocodeClient::Result`/`geocode()` (Task 2), `SETTINGS.flightTrackerHomeZip` and the 5 new `StrId` keys (Task 3), `WifiSelectionActivity(GfxRenderer&, MappedInputManager&, bool autoConnect = true)` from `src/activities/network/WifiSelectionActivity.h:123-124`, `WifiResult{bool connected; std::string ssid; std::string ip;}` from `src/activities/ActivityResult.h:10-14`, `silentRestart()` from `src/SilentRestart.h`.
- Produces: no new public interface — this is the final consumer in the chain.

- [ ] **Step 1: Replace `FlightTrackerSettingsActivity.h`**

```cpp
#pragma once

#include <string>

#include "ZipGeocodeParser.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Four-row settings screen: home zip code (geocoded via Zippopotam on
// submit), home latitude, home longitude (both free-text decimal degrees
// via the keyboard, and the fallback if the zip lookup fails or the user is
// outside the US), and search radius (tap-to-cycle).
class FlightTrackerSettingsActivity final : public Activity {
 public:
  explicit FlightTrackerSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("FlightTrackerSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr int ITEM_COUNT = 4;
  // How long a rejected-input or lookup-result message stays on screen
  // before loop() clears it.
  static constexpr unsigned long ERROR_MESSAGE_DURATION_MS = 3000;

  enum class ZipLookupState { IDLE, CHECK_WIFI, WIFI_SELECTION, LOADING };

  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  std::string errorMessage;
  unsigned long errorShownAt = 0;

  ZipLookupState zipLookupState = ZipLookupState::IDLE;
  char pendingZip[6] = {0};  // zip currently being looked up, set on commit
  ZipGeocodeParser zipParser;
  // Set true only inside performZipLookup(), i.e. only once this screen has
  // actually made an HTTP/TLS request. Gates onExit()'s teardown -- see its
  // comment for why WiFi.getMode() alone is the wrong condition here.
  bool wifiUsedThisSession = false;

  void handleSelection();
  void rejectCoordinate(const std::string& text);
  void rejectZip(const std::string& text);
  void checkAndConnectWifi();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void performZipLookup();
};
```

- [ ] **Step 2: Replace `FlightTrackerSettingsActivity.cpp`**

```cpp
#include "FlightTrackerSettingsActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "AlmanacSettings.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "network/ZipGeocodeClient.h"

namespace {
bool parseCoordinate(const std::string& text, double minValue, double maxValue, double& outValue) {
  if (text.empty()) return false;
  char* end = nullptr;
  const double value = strtod(text.c_str(), &end);
  if (end == text.c_str() || *end != '\0') return false;
  // strtod() accepts "nan"/"-nan" as a valid parse with a NaN result; every
  // comparison against a NaN is false, so the range check below would let it
  // slip through as "in range". Reject it explicitly.
  if (std::isnan(value)) return false;
  if (value < minValue || value > maxValue) return false;
  outValue = value;
  return true;
}

bool isValidZip(const std::string& text) {
  if (text.size() != 5) return false;
  for (const char c : text) {
    if (c < '0' || c > '9') return false;
  }
  return true;
}
}  // namespace

void FlightTrackerSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  errorMessage.clear();
  requestUpdate();
}

void FlightTrackerSettingsActivity::onExit() {
  Activity::onExit();
  // Unlike NearbyFlightsActivity (a top-level Home entry, where landing back
  // on Home after a reboot is exactly where Back would go anyway), this
  // activity is reached via Settings -> Flight Tracker. silentRestart() only
  // knows how to land on Home or the reader -- rebooting there on every exit
  // would strand a user who only edited Lat/Lon/Radius by hand, bouncing
  // them out of the settings hierarchy they were navigating. So the teardown
  // is gated on wifiUsedThisSession (set only inside performZipLookup(), the
  // one place this screen actually makes an HTTP/TLS request) rather than on
  // raw WiFi state -- matching SilentRestart.h's stated purpose (clearing
  // fragmentation from a WiFi *session*, not merely "WiFi is on"). A zip
  // lookup still lands the user on Home rather than back in Settings; that
  // residual is a real tradeoff, not a bug, given the destinations
  // silentRestart() offers.
  if (wifiUsedThisSession && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void FlightTrackerSettingsActivity::loop() {
  // CHECK_WIFI and LOADING run to completion synchronously inside the call
  // that set them (checkAndConnectWifi()/performZipLookup() are blocking,
  // same as NearbyFlightsActivity's fetchFlights()); WIFI_SELECTION hands
  // the screen to a pushed WifiSelectionActivity. None of the three should
  // process row navigation or Back/Confirm here.
  if (zipLookupState != ZipLookupState::IDLE) return;

  if (!errorMessage.empty() && millis() - errorShownAt >= ERROR_MESSAGE_DURATION_MS) {
    errorMessage.clear();
    requestUpdate();
  }

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
        if (isValidZip(kb.text)) {
          strncpy(pendingZip, kb.text.c_str(), sizeof(pendingZip) - 1);
          pendingZip[sizeof(pendingZip) - 1] = '\0';
          zipLookupState = ZipLookupState::CHECK_WIFI;
          requestUpdate();
          checkAndConnectWifi();
        } else {
          rejectZip(kb.text);
        }
      }
      requestUpdate();
    };
    startActivityForResult(
        std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_FLIGHT_TRACKER_HOME_ZIP),
                                                 std::string(SETTINGS.flightTrackerHomeZip), 5, InputType::Text),
        handler);
    return;
  }

  if (selectedIndex == 1) {
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        double parsed = 0;
        if (parseCoordinate(kb.text, -90.0, 90.0, parsed)) {
          strncpy(SETTINGS.flightTrackerHomeLat, kb.text.c_str(), sizeof(SETTINGS.flightTrackerHomeLat) - 1);
          SETTINGS.flightTrackerHomeLat[sizeof(SETTINGS.flightTrackerHomeLat) - 1] = '\0';
          SETTINGS.saveToFile();
        } else {
          rejectCoordinate(kb.text);
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

  if (selectedIndex == 2) {
    auto handler = [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        const auto& kb = std::get<KeyboardResult>(result.data);
        double parsed = 0;
        if (parseCoordinate(kb.text, -180.0, 180.0, parsed)) {
          strncpy(SETTINGS.flightTrackerHomeLon, kb.text.c_str(), sizeof(SETTINGS.flightTrackerHomeLon) - 1);
          SETTINGS.flightTrackerHomeLon[sizeof(SETTINGS.flightTrackerHomeLon) - 1] = '\0';
          SETTINGS.saveToFile();
        } else {
          rejectCoordinate(kb.text);
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

  // Search radius: tap cycles through the allowed range. `next` is int (not
  // uint8_t) purely as a defensive habit, not because it's load-bearing here:
  // AlmanacSettings::fromJson() already clamps every SettingType::VALUE
  // field -- including this one -- to [min,max] on load (see
  // AlmanacSettings.cpp:159-164), so flightTrackerRadiusMiles can never
  // actually reach this point above MAX. The int widening is harmless and
  // guards against that invariant changing later.
  const int next = static_cast<int>(SETTINGS.flightTrackerRadiusMiles) + AlmanacSettings::FLIGHT_TRACKER_RADIUS_STEP;
  SETTINGS.flightTrackerRadiusMiles = next > AlmanacSettings::FLIGHT_TRACKER_RADIUS_MAX
                                          ? AlmanacSettings::FLIGHT_TRACKER_RADIUS_MIN
                                          : static_cast<uint8_t>(next);
  SETTINGS.saveToFile();
  requestUpdate();
}

void FlightTrackerSettingsActivity::rejectCoordinate(const std::string& text) {
  errorMessage = tr(STR_INVALID_COORDINATE);
  errorShownAt = millis();
  LOG_ERR("FTS", "Rejected coordinate input: %s", text.c_str());
}

void FlightTrackerSettingsActivity::rejectZip(const std::string& text) {
  errorMessage = tr(STR_INVALID_ZIP_CODE);
  errorShownAt = millis();
  LOG_ERR("FTS", "Rejected zip input: %s", text.c_str());
}

void FlightTrackerSettingsActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    performZipLookup();
    return;
  }
  launchWifiSelection();
}

void FlightTrackerSettingsActivity::launchWifiSelection() {
  zipLookupState = ZipLookupState::WIFI_SELECTION;
  requestUpdate();
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                          [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void FlightTrackerSettingsActivity::onWifiSelectionComplete(const bool connected) {
  if (connected) {
    performZipLookup();
    return;
  }
  // Leave WiFi up; onExit's silent reboot handles teardown without fragmenting.
  zipLookupState = ZipLookupState::IDLE;
  errorMessage = tr(STR_WIFI_CONN_FAILED);
  errorShownAt = millis();
  requestUpdate();
}

void FlightTrackerSettingsActivity::performZipLookup() {
  zipLookupState = ZipLookupState::LOADING;
  wifiUsedThisSession = true;
  requestUpdate();

  zipParser.reset();
  const ZipGeocodeClient::Result result = ZipGeocodeClient::geocode(pendingZip, zipParser);

  switch (result) {
    case ZipGeocodeClient::Result::Ok: {
      if (zipParser.hasError() || !zipParser.geocode().found) {
        LOG_ERR("FTS", "Zip lookup for %s returned a malformed response", pendingZip);
        errorMessage = tr(STR_ZIP_LOOKUP_ERROR);
        break;
      }
      const auto& geo = zipParser.geocode();
      snprintf(SETTINGS.flightTrackerHomeLat, sizeof(SETTINGS.flightTrackerHomeLat), "%.4f", geo.latitude);
      snprintf(SETTINGS.flightTrackerHomeLon, sizeof(SETTINGS.flightTrackerHomeLon), "%.4f", geo.longitude);
      strncpy(SETTINGS.flightTrackerHomeZip, pendingZip, sizeof(SETTINGS.flightTrackerHomeZip) - 1);
      SETTINGS.flightTrackerHomeZip[sizeof(SETTINGS.flightTrackerHomeZip) - 1] = '\0';
      SETTINGS.saveToFile();
      errorMessage.clear();
      break;
    }
    case ZipGeocodeClient::Result::NotFound:
      LOG_ERR("FTS", "No record for zip %s (HTTP 404)", pendingZip);
      errorMessage = tr(STR_ZIP_NOT_FOUND);
      break;
    case ZipGeocodeClient::Result::Error:
      LOG_ERR("FTS", "Zip lookup transport/parse failure for %s", pendingZip);
      errorMessage = tr(STR_ZIP_LOOKUP_ERROR);
      break;
  }

  zipLookupState = ZipLookupState::IDLE;
  errorShownAt = millis();
  requestUpdate();
}

void FlightTrackerSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();

  std::string subtitle;
  if (zipLookupState == ZipLookupState::LOADING) {
    char buf[48];
    snprintf(buf, sizeof(buf), tr(STR_ZIP_LOOKUP_LOADING_FORMAT), pendingZip);
    subtitle = buf;
  } else if (!errorMessage.empty()) {
    subtitle = errorMessage;
  }
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_FLIGHT_TRACKER),
                 subtitle.empty() ? nullptr : subtitle.c_str());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, ITEM_COUNT, selectedIndex,
      [](int index) -> std::string {
        switch (index) {
          case 0:
            return I18n::getInstance().get(StrId::STR_FLIGHT_TRACKER_HOME_ZIP);
          case 1:
            return I18n::getInstance().get(StrId::STR_FLIGHT_TRACKER_HOME_LAT);
          case 2:
            return I18n::getInstance().get(StrId::STR_FLIGHT_TRACKER_HOME_LON);
          default:
            return I18n::getInstance().get(StrId::STR_FLIGHT_TRACKER_RADIUS);
        }
      },
      nullptr, nullptr,
      [](int index) -> std::string {
        switch (index) {
          case 0:
            return SETTINGS.flightTrackerHomeZip[0] ? std::string(SETTINGS.flightTrackerHomeZip)
                                                     : std::string(I18n::getInstance().get(StrId::STR_NOT_SET));
          case 1:
            return SETTINGS.flightTrackerHomeLat[0] ? std::string(SETTINGS.flightTrackerHomeLat)
                                                     : std::string(I18n::getInstance().get(StrId::STR_NOT_SET));
          case 2:
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

- [ ] **Step 3: Build and run the full host test suite**

```bash
~/.platformio/penv/bin/pio run -e default
cmake -S test -B build/test && cmake --build build/test && ctest --test-dir build/test --output-on-failure -j
```

Expected: firmware build succeeds with no errors/warnings; host suite is still **206/206** (this task adds no new host tests — `Activity` classes aren't host-tested in this codebase, per Global Constraints).

- [ ] **Step 4: Format and commit**

```bash
./bin/clang-format-fix -g
git add src/activities/settings/FlightTrackerSettingsActivity.h src/activities/settings/FlightTrackerSettingsActivity.cpp
git commit -m "feat: add zip-code entry to Flight Tracker settings"
```

- [ ] **Step 5: Flag remaining verification for a human tester**

Per the design spec's Testing section and this codebase's established human-tester scope (CLAUDE.md's Testing Checklist item 6-9), the following need real hardware and are out of scope for an AI agent to run:
- A known-good zip (e.g. `90210`) resolves and overwrites Lat/Lon.
- A well-formed but nonexistent zip (e.g. `00000`) shows "Zip code not found" and leaves prior settings untouched.
- Malformed input (`abc12`, `9021`) is rejected locally with no network activity.
- The Wi-Fi prompt appears only when not already connected, and a pure Lat/Lon/Radius editing session (no zip lookup attempted) never triggers a reboot on Back.
- After a successful zip lookup, confirm the reboot-to-Home tradeoff is acceptable in practice (Back no longer returns to Settings in that case — see `onExit()`'s comment).
- Lat/Lon rows remain independently editable after a zip lookup.

---

## Self-Review

**Spec coverage:**
- UI placement (4th row above Lat/Lon) — Task 4, `render()`'s row-index switches and `handleSelection()`'s index-0 branch. ✓
- Zip persistence — Task 3 field + Task 4's `performZipLookup()` writes `SETTINGS.flightTrackerHomeZip`. ✓
- Wi-Fi flow reusing `WifiSelectionActivity` — Task 4 `checkAndConnectWifi`/`launchWifiSelection`/`onWifiSelectionComplete`. ✓
- US-only 5-digit validation, no network call on bad format — Task 4 `isValidZip`/`rejectZip`. ✓
- Zippopotam as source, string-typed lat/lon, 404 handling — Task 1 parser + Task 2 client. ✓
- `AlmanacSettings`/`SettingsList.h` field — Task 3. ✓
- `ZipLookupState` enum exact shape from spec — Task 4 header. ✓
- Ok/NotFound/Error handling with distinct logging — Task 4 `performZipLookup()`. ✓
- `onExit()` teardown — gated on `wifiUsedThisSession` (set only inside `performZipLookup()`), not raw `WiFi.getMode()`, so a pure hand-edit session never reboots. Caught in advisor review: this activity is pushed from `SettingsActivity` (confirmed via `grep` — `src/activities/settings/SettingsActivity.cpp:388`), not a Home top-level entry like `NearbyFlightsActivity`, so an unconditional reboot-to-Home on every exit would have stranded users navigating Settings. ✓
- `ZipGeocodeResult.found` requires both `sawLatitude` and `sawLongitude`, not just a clean `places[0]` close — caught in advisor review: a record missing coordinates would otherwise present as `found=true` with both doubles at their `0` default, and `performZipLookup()`'s `!found` guard would miss it, silently writing Null Island into the home location. Covered by Task 1's `MissingCoordinatesNotFound` test. ✓
- Lat/Lon rows remain independently editable — Task 4 preserves the existing index-1/2 branches verbatim. ✓
- `test/zip_geocode_parser` host tests; no host test for client/activity — Task 1 / Tasks 2 & 4. ✓
- Out-of-scope items (non-US codes, reverse geocoding, auto-detection, dedicated clear action) — none implemented by this plan. ✓

**Placeholder scan:** no TBD/TODO, no "add error handling"-style steps, no "similar to Task N" — every step has full code. ✓

**Type consistency:** `ZipGeocodeResult`/`ZipGeocodeParser::geocode()` (Task 1) match Task 2's and Task 4's usage exactly; `ZipGeocodeClient::Result`/`geocode()` (Task 2) match Task 4's `switch` exactly; `SETTINGS.flightTrackerHomeZip` size (`char[6]`, Task 3) matches Task 4's `strncpy` bound and `pendingZip[6]`; all 5 new `StrId` names (Task 3) match Task 4's `tr()` calls verbatim. ✓
